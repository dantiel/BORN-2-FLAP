"""Terrain backend abstraction for the BORN 2 FLAP procedural world generators.

Two implementations of the same geological-authoring interface:

  StaticMeshBackend      — the current pipeline: baked StaticMesh assets + actors
                           in a non-World-Partition map (what create_yunsha_map.py does).
  MeshPartitionBackend   — Mesh Terrain (Mesh Partition): a World-Partition map with one
                           AMeshPartition actor, base modifiers from the same geometry,
                           non-destructive Boolean/Remesh modifiers, and compiled sections
                           built afterwards by the WorldPartitionMeshPartitionBuilder
                           commandlet (see .setup/regen-pocterrain.ps1).

The two backends are fed identical, deterministic geometry so they can be benchmarked
against each other. Nothing here depends on numpy or network access.
"""
import math
import os
import re
import time
from pathlib import Path

import unreal as u

E = u.EditorAssetLibrary
M = u.MaterialEditingLibrary

HERE = Path(__file__).resolve().parent


def sanitize(name: str) -> str:
    return re.sub(r'[^A-Za-z0-9_]', '_', name)


def material_array(items):
    arr = u.Array(u.MaterialInterface)
    for item in items:
        arr.append(item)
    return arr


def write_static_mesh(root_path, name, verts, tris, mat, nanite=False,
                      collision=False, recompute_normals=True, flip_winding=True):
    """Author a StaticMesh asset from raw geometry (shared by both backends).

    flip_winding=True is the LEGACY StaticMeshBackend recipe: UE 5.8 complex-
    as-simple collision is winding-sensitive (empirically verified: (a,b,c)
    meshes never collide, see create_yunsha_map.py) and needs the flipped
    (a,c,b) order; recompute_normals then realigns the render normals so the
    mesh still renders correctly.

    flip_winding=False is the MeshPartition convention: the
    WorldPartitionMeshPartitionBuilder ALWAYS recomputes compiled-section
    normals from the source winding (Settings.bRecomputeNormals=true), so a
    flipped base sheet faces DOWN and vanishes under backface culling. MP
    geometry must arrive CCW-from-above (a,b,c) -- the convention the WOSHI
    generator documents. MP collision is built separately with
    bFlipNormals=true and is unaffected by the render winding.
    """
    name = sanitize(name)
    path = root_path + '/Meshes/SM_' + name
    if E.does_asset_exist(path):
        # Rebuild from scratch: stale descriptions could survive an overwrite.
        assert E.delete_asset(path)
    sm = u.AssetToolsHelpers.get_asset_tools().create_asset(
        'SM_' + name, root_path + '/Meshes', u.StaticMesh, None)
    md = sm.create_static_mesh_description()
    md.reserve_new_vertices(len(verts))
    md.reserve_new_vertex_instances(len(verts))
    md.reserve_new_polygons(len(tris))
    pg = md.create_polygon_group()
    md.set_polygon_group_material_slot_name(pg, 'Surface')
    sm.set_editor_property('static_materials', [u.StaticMaterial(
        material_interface=mat, material_slot_name='Surface')])
    ids = []
    for x, y, z in verts:
        v = md.create_vertex()
        md.set_vertex_position(v, u.Vector(x, y, z))
        i = md.create_vertex_instance(v)
        md.set_vertex_instance_uv(i, u.Vector2D(x / 400.0, y / 400.0), 0)
        ids.append(i)
    # StaticMeshBackend (flip_winding=True): the Yunsha-identical recipe
    # (create_yunsha_map.py mesh()): flipped winding (a,c,b) +
    # recompute_normals. A trace bisect proved UE 5.8 complex-as-simple
    # collision IGNORES meshes wound (a,b,c) from the same verts; the flipped
    # winding renders correctly because recompute_normals realigns the normals.
    # MeshPartitionBackend (flip_winding=False): keep the authored CCW-from-
    # above winding; the MP builder derives section normals from it.
    if flip_winding:
        for a, b, c in tris:
            md.create_triangle(pg, [ids[a], ids[c], ids[b]])
    else:
        for a, b, c in tris:
            md.create_triangle(pg, [ids[a], ids[b], ids[c]])
    editor = u.get_editor_subsystem(u.StaticMeshEditorSubsystem) \
        or u.new_object(u.StaticMeshEditorSubsystem)
    bs = editor.get_lod_build_settings(sm, 0)
    bs.set_editor_property('recompute_normals', recompute_normals)
    bs.set_editor_property('recompute_tangents', True)
    bs.set_editor_property('use_mikk_t_space', True)
    editor.set_lod_build_settings(sm, 0, bs)
    sm.set_editor_property('allow_cpu_access', True)
    sm.build_from_static_mesh_descriptions([md], False, False)
    if collision:
        sm.get_editor_property('body_setup').set_editor_property(
            'collision_trace_flag', u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    sm.set_material(0, mat)
    assert E.save_loaded_asset(sm)
    if nanite:
        try:
            sm.set_editor_property('nanite_settings', u.MeshNaniteSettings(enabled=True))
            sm.build()
        except Exception as exc:  # best effort, mirrors create_yunsha_map.py
            u.log_warning('WOSHI_NANITE_SKIP ' + name + ' ' + str(exc))
    return sm


class TerrainBackend:
    """Interface implemented by all world-geometry backends."""

    name = 'terrain'
    carve_supported = False   # Boolean/subtractive carving available natively
    remesh_supported = False  # localized remeshing available natively
    world_partition = False

    def __init__(self, root_path, world_label):
        self.root = root_path
        self.world_label = world_label
        self.stats = {'backend': self.name, 'tris': 0, 'assets': 0}
        self._t0 = time.perf_counter()
        self.meshes = set()

    def tick(self, phase):
        now = time.perf_counter()
        self.stats['time_' + phase] = round(now - self._t0, 3)
        self._t0 = now

    def begin(self):
        raise NotImplementedError

    def commit_surface(self, label, verts, tris, material, transform=None, slot=0):
        raise NotImplementedError

    def commit_boolean(self, label, tool_verts, tool_tris, transform, subtract=True,
                       channel=None, weight=1.0):
        raise NotImplementedError

    def commit_remesh(self, label, location, coverage, edge_length):
        raise NotImplementedError

    def finalize(self, map_path):
        raise NotImplementedError


class StaticMeshBackend(TerrainBackend):
    """Current pipeline: baked StaticMesh assets + actors, no World Partition."""

    name = 'staticmesh'

    def begin(self):
        world = u.EditorLoadingAndSavingUtils.new_blank_map(False)
        world.get_world_settings().set_editor_property('force_no_precomputed_lighting', True)
        self.world = world
        return world

    def commit_surface(self, label, verts, tris, material, transform=None, slot=0):
        # Colliding terrain meshes stay non-Nanite: the Nanite build strips the
        # CPU LOD0 that CTF_USE_COMPLEX_AS_SIMPLE collision is generated from
        # (create_yunsha_map.py only Nanites its NoCollision foliage for the
        # same reason). 55k tris needs no Nanite anyway.
        sm = write_static_mesh(self.root, label, verts, tris, material,
                               nanite=False, collision=True)
        self.meshes.add(sm.get_path_name())
        location = u.Vector() if transform is None else transform.translation
        actor = u.EditorLevelLibrary.spawn_actor_from_class(
            u.StaticMeshActor, location, u.Rotator())
        actor.set_actor_label(self.world_label + ' ' + label)
        component = actor.static_mesh_component
        component.set_static_mesh(sm)
        component.set_collision_profile_name('BlockAll')
        if transform is not None:
            actor.set_actor_transform(transform, False, False)
        self.stats['tris'] += len(tris)
        self.stats['assets'] += 1
        return actor

    def commit_remesh(self, label, location, coverage, edge_length):
        # No localized remesh in the static-mesh pipeline: the generator bakes the
        # detail into the source geometry instead.
        u.log_warning('WOSHI_REMESH_SKIP ' + label + ' (static-mesh backend)')

    def finalize(self, map_path):
        self.tick('finalize_save')
        assert u.EditorLoadingAndSavingUtils.save_map(self.world, map_path)
        self.stats['map'] = map_path
        return self.stats


class MeshPartitionBackend(TerrainBackend):
    """Mesh Terrain backend: World Partition + AMeshPartition + modifiers."""

    name = 'meshpartition'
    carve_supported = True
    remesh_supported = True
    world_partition = True

    DEFAULT_DEFINITION = '/MeshPartition/DataAssets/MPD_Default.MPD_Default'
    CHANNELS = ('Sandstone', 'Rock')
    CHANNEL_TEXEL_SIZE = 64.0

    def begin(self):
        # UE 5.8: NewBlankMap() has no WorldPartition parameter; enable WP via
        # UWorldPartition::CreateOrRepairWorldPartition right after map creation.
        world = u.EditorLoadingAndSavingUtils.new_blank_map(False)
        assert u.Born2FlapMeshPartition.ensure_world_partition(world), 'World Partition setup failed'
        world.get_world_settings().set_editor_property('force_no_precomputed_lighting', True)

        # C++ side: create-or-replace the definition asset by deep-copying the
        # engine default (Material, build variants, transformer pipelines) and
        # re-authoring the channels. Overwrites a previous generation.
        definition = u.Born2FlapMeshPartition.create_definition(
            self.root + '/DataAssets', 'MPD_' + self.world_label,
            self.DEFAULT_DEFINITION,
            [u.Name(c) for c in self.CHANNELS], self.CHANNEL_TEXEL_SIZE)
        assert definition, 'definition creation failed'
        u.Born2FlapMeshPartition.log_definition_info(definition)
        self.definition = definition

        actor = u.Born2FlapMeshPartition.create_mesh_partition(
            world, definition, self.world_label + ' MeshPartition', u.Transform())
        assert actor, 'MeshPartition actor creation failed'
        self.world = world
        self.mesh_partition = actor
        self.editor_component = u.Born2FlapMeshPartition.get_editor_component(actor)
        assert self.editor_component, 'editor component access failed'
        self.stats['definition'] = definition.get_path_name()
        self.tick('setup')
        return world

    def _source_sm(self, label, verts, tris, mat):
        """Persistent source StaticMesh for a MeshPartition modifier.

        MeshPartition convention: CCW-from-above winding (WOSHI-verified) --
        the builder recomputes compiled-section normals from this winding.
        The asset is KEPT: the boolean modifier re-resolves it on every load
        and the build hash depends on the source package.
        """
        sm = write_static_mesh(self.root, label, verts, tris, mat,
                               nanite=False, collision=False, flip_winding=False)
        self.meshes.add(sm.get_path_name())
        return sm

    def commit_surface(self, label, verts, tris, material, transform=None, slot=0):
        # MeshPartition source meshes are NOT transient. The boolean modifier
        # keeps a hard StaticMesh reference that is re-resolved on every load
        # (UMeshBasedModifierBase::UpdateMeshInstance), the build hash depends
        # on the source package (GatherDependencies), and the
        # WorldPartitionMeshPartitionBuilder commandlet runs in a FRESH process
        # that must reload these assets. Deleting them silently breaks the
        # carve and future rebuilds; retirement happens in the generator's
        # cleanup via the protected set backend.meshes.
        sm = self._source_sm(label, verts, tris, material)
        transform = transform if transform is not None else u.Transform()
        actor = u.Born2FlapMeshPartition.add_base_modifier_from_static_mesh(
            self.editor_component, sm, transform, material_array([material]), slot)
        assert actor, 'base modifier failed for ' + label
        # KEEP the source StaticMesh: rebuilds (-ReuseMode=ByModifierHash) and
        # the build commandlet re-resolve it.
        u.log('B2F_MP_SOURCE_KEPT ' + sm.get_path_name())
        self.stats['tris'] += len(tris)
        self.stats['base_modifiers'] = self.stats.get('base_modifiers', 0) + 1
        return actor

    def commit_boolean(self, label, tool_verts, tool_tris, transform, subtract=True,
                       channel=None, weight=1.0, operation=None):
        # Tool meshes only exist to feed the modifier; use a throwaway material.
        tool = self._source_sm('Tool_' + label, tool_verts, tool_tris,
                               u.load_asset('/Engine/BasicShapes/BasicShapeMaterial'))
        transform = transform if transform is not None else u.Transform()
        channel_name = u.Name(channel) if channel else u.Name('None')
        # Boolean op code: 0=Union, 1=Subtract (capped carve), 2=Trim (open
        # hole, no cap). Subtract caps the hole into a locally-closed surface —
        # a closed tool box then leaves its top face as a floating sheet over
        # the excavation, which becomes invisible-but-colliding terrain from
        # above (the "walking on invisible ground" bug). Trim cuts the base
        # surface out and leaves the hole open for the authored excavation
        # mesh to cover.
        if operation == 'trim':
            op_code = 2
        elif operation == 'union':
            op_code = 0
        else:
            op_code = 1 if subtract else 0
        actor = u.Born2FlapMeshPartition.add_boolean_modifier(
            self.editor_component, tool, transform, op_code, channel_name, weight)
        assert actor, 'boolean modifier failed for ' + label
        # KEEP the tool mesh: the modifier re-derives its tool geometry from
        # the referenced StaticMesh on EVERY load (MeshSourceMode=StaticMesh).
        # Deleting the asset empties the carve in the build commandlet session.
        u.log('B2F_MP_SOURCE_KEPT ' + tool.get_path_name())
        self.stats['boolean_modifiers'] = self.stats.get('boolean_modifiers', 0) + 1
        return actor

    def commit_remesh(self, label, location, coverage, edge_length):
        actor = u.Born2FlapMeshPartition.add_remesh_modifier(
            self.editor_component, u.Vector(*location), u.Vector(*coverage), edge_length)
        assert actor, 'remesh modifier failed for ' + label
        self.stats['remesh_modifiers'] = self.stats.get('remesh_modifiers', 0) + 1
        return actor

    def finalize(self, map_path):
        descs_spawned = u.Born2FlapMeshPartition.count_actor_descs(self.world)
        self.tick('finalize_save')
        # Interactive-editor-compatible save order: external actor packages
        # first (registers their descriptors via OnObjectPreSave), then the
        # map package (serializes the descriptor container). Plain SaveMap
        # alone does not persist external actors in headless generation.
        assert u.Born2FlapMeshPartition.save_world_external_first(
            self.world, map_path), 'world save failed'
        self.stats['actor_descs_after_spawn'] = descs_spawned
        self.stats['actor_descs_after_save'] = u.Born2FlapMeshPartition.count_actor_descs(self.world)
        for line in u.Born2FlapMeshPartition.dump_actor_desc_state(self.world):
            u.log('WOSHI_DESC ' + line)
        self.stats['save'] = 'external-first'
        self.stats['source_meshes_kept'] = sorted(self.meshes)

        if os.environ.get('B2F_INPROCESS_BUILD') == '1':
            # Same editor session: run the WorldPartitionMeshPartitionBuilder
            # in-process instead of the separate commandlet process. Opt-in
            # only: unstable on this world's many external actors (UnrealEd AV
            # inside RunBuilder); the regen-yunsha-mp.ps1 commandlet route is
            # the verified default.
            self.tick('inprocess_build')
            assert u.Born2FlapMeshPartition.build_mesh_partition_in_process(
                self.world, True), 'in-process build failed'
            # Second save pass: persist the compiled-section descriptors the
            # builder registered plus the new external section packages.
            assert u.Born2FlapMeshPartition.save_world_external_first(
                self.world, map_path), 'post-build world save failed'
            self.stats['build'] = ('inprocess', 'WorldPartitionMeshPartitionBuilder')
            self.stats['build_executed'] = True
        else:
            # The compiled-section build runs as a SEPARATE commandlet
            # (regen-yunsha-mp.ps1 step 2). Recording this as "build done"
            # would be a lie: flag it explicitly so the pipeline verifies the
            # commandlet's "Finished Build for World" log line afterwards.
            self.stats['build'] = ('commandlet', 'WorldPartitionMeshPartitionBuilder')
            self.stats['build_executed'] = False
            u.log_warning('B2F_MP_BUILD_PENDING commandlet build NOT executed in this session; run the WorldPartitionMeshPartitionBuilder commandlet (see .setup/regen-yunsha-mp.ps1) and verify "Finished Build for World"')

        self.stats['map'] = map_path
        return self.stats


def make_backend(kind, root_path, world_label):
    if kind == 'staticmesh':
        return StaticMeshBackend(root_path, world_label)
    return MeshPartitionBackend(root_path, world_label)