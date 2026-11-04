"""Reload, validate and render the saved sideways-shell airport in the full Editor.

Use -ExecutePythonScript with -RenderOffscreen, not -run=pythonscript or -nullrhi.
Captures are written under .setup/murex-redesign; the map is not modified/saved.
"""
import json
import time
import traceback
from pathlib import Path
import unreal as u

HERE = Path(__file__).resolve().parent
OUTPUT = HERE.parents[2]/'.setup'/'murex-redesign'
OUTPUT.mkdir(parents=True, exist_ok=True)
assert u.EditorLoadingAndSavingUtils.load_map('/Game/Murex/Maps/MUREX')
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
actors = u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()


def tagged(tag):
    return [a for a in actors if tag in [str(t) for t in a.tags]]


expected = {'MurexMainHall': 1, 'MurexShellAxis': 1, 'MurexEntranceGlazing': 1,
            'MurexGateSpine': 6, 'MurexEntrancePark': 1}
counts = {key: len(tagged(key)) for key in expected}
assert counts == expected, (counts, expected)
plane_ids = {str(t) for a in tagged('MurexScaleAircraft') for t in a.tags
             if str(t).startswith('MurexAircraft')}
assert len(plane_ids) == 6, plane_ids
assert len(tagged('MurexTowerSpine')) == 3  # shaft plus its two end caps
assert not any(a.get_actor_label() == 'Murex murex spire shaft' for a in actors)
assert not any('terminal roof bay' in a.get_actor_label() for a in actors)


def trace_height(x, y):
    hit = u.SystemLibrary.line_trace_single(world, u.Vector(x,y,26000), u.Vector(x,y,-1000),
        u.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], u.DrawDebugTrace.NONE, True)
    if isinstance(hit, tuple):
        hit = next((v for v in hit if isinstance(v,u.HitResult)),None)
    assert hit is not None, (x,y,'missing hit')
    parts = hit.to_tuple()
    assert parts[0], (x,y,'not blocking')
    return parts[5].z


probes = {
    'tower_roof': (-121200,-35700,11700,11900),
    'tower_gallery': (-121300,-37200,8400,8500),
    'first_gate_roof': (-114990,-28467,1100,1300),
    'entrance_walk': (-127200,-55000,0,80),
    'runway': (0,0,-10,10),
}
heights = {}

report = dict(reloaded=True, actor_count=len(actors), counts=counts,
              aircraft=sorted(plane_ids), collision_height_cm=heights, screenshots=[])
views = [tagged('MurexView%d'%i)[0] for i in range(6)]
names = ['01-shell-overview','02-aperture-and-park','03-airside-terminals',
         '04-integrated-tower','05-small-gates','06-entrance-human-scale']
state = dict(index=0, task=None, validated=False, busy=False, after=time.monotonic()+12, started=time.monotonic())
u.EditorPythonScripting.set_keep_python_script_alive(True)


def finish():
    report['passed'] = not report.get('collision_errors') and all(
        Path(p).is_file() and Path(p).stat().st_size > 10000 for p in report['screenshots'])
    (OUTPUT/'saved-map-validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    if report['passed']:
        u.log('MUREX_SAVED_MAP_VALIDATED '+json.dumps(report))
    else:
        u.log_error('MUREX_SAVED_MAP_FAILED '+json.dumps(report))
    u.unregister_slate_post_tick_callback(state['handle'])
    u.EditorPythonScripting.set_keep_python_script_alive(False)
    u.SystemLibrary.execute_console_command(world,'quit')


def tick(delta):
    # Screenshot preparation can pump Slate while compiling render resources.
    # Never schedule another capture from that nested tick.
    if state['busy']:
        return
    state['busy'] = True
    try:
        now=time.monotonic()
        if now-state['started'] > 900:
            raise RuntimeError('Murex capture timed out')
        if now < state['after']:
            return
        if not state['validated']:
            report['collision_errors']=[]
            for name,(x,y,lo,hi) in probes.items():
                try:
                    heights[name]=trace_height(x,y)
                    assert lo <= heights[name] <= hi, (name,heights[name],lo,hi)
                except Exception as e:
                    report['collision_errors'].append(repr(e))
            entry_hit = u.SystemLibrary.line_trace_single(world,
                u.Vector(-127200,-55000,180), u.Vector(-127200,-48000,180),
                u.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], u.DrawDebugTrace.NONE, True)
            report['entrance_clear'] = entry_hit is None
            if entry_hit is not None:
                report['collision_errors'].append('Promenade-to-hall entrance is blocked')
            for a in tagged('MurexTowerDeck'):
                u.log('MUREX_DECK_DEBUG '+str(a.get_actor_location())+' '+str(a.get_actor_bounds(False)))
            u.log('MUREX_COLLISION_CHECK '+json.dumps(dict(heights=heights,errors=report['collision_errors'])))
            state['validated']=True
        task=state['task']
        if task is not None:
            if not task.is_task_done():
                return
            report['screenshots'].append(str(OUTPUT/(names[state['index']]+'.png')))
            state['index']+=1
            state['task']=None
            state['after']=now+3
            if state['index']==len(views):
                finish()
            return
        i=state['index']
        camera=views[i]
        u.EditorLevelLibrary.set_level_viewport_camera_info(camera.get_actor_location(),camera.get_actor_rotation())
        state['task']=u.AutomationLibrary.take_high_res_screenshot(1600,1000,
            str(OUTPUT/(names[i]+'.png')),camera=camera,delay=3.0)
        assert state['task'].is_valid_task()
    except Exception as e:
        u.log_error('MUREX_CAPTURE_FAILED '+traceback.format_exc())
        u.unregister_slate_post_tick_callback(state['handle'])
        u.EditorPythonScripting.set_keep_python_script_alive(False)
        u.SystemLibrary.execute_console_command(world,'quit')
    finally:
        state['busy'] = False


state['handle']=u.register_slate_post_tick_callback(tick)
u.log('MUREX_CAPTURE_STARTED')
