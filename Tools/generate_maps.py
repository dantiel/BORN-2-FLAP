#!/usr/bin/env python3
"""Generate editor maps from versioned inputs on Windows/macOS (stdlib only)."""
import argparse
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[1]
PROJECT_REL = Path('Unreal/Born2Flap')
MANIFEST_REL = Path('Tools/generated-content.json')
LFS_HEADER = b'version https://git-lfs.github.com/spec/v1'


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def manifest(root):
    value = read_json(root / MANIFEST_REL)
    if value['schema'] != 1:
        raise RuntimeError('Unsupported generated-content manifest')
    for spec in value['maps'].values():
        for output in spec['outputs']:
            if Path(output).is_absolute() or '..' in Path(output).parts or '\\' in output:
                raise RuntimeError('Unsafe output path: ' + output)
    return value


def generated(relative, spec):
    return any(fnmatch.fnmatchcase(relative, pattern)
               for entry in spec['maps'].values() for pattern in entry['outputs'])


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b''):
            h.update(chunk)
    return h.hexdigest()


def engine_paths(engine, system=None):
    system = system or platform.system()
    if system == 'Windows':
        return engine/'Engine/Binaries/Win64/UnrealEditor-Cmd.exe', engine/'Engine/Build/BatchFiles/Build.bat', 'Win64'
    if system == 'Darwin':
        return engine/'Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor', engine/'Engine/Build/BatchFiles/Mac/Build.sh', 'Mac'
    raise RuntimeError('Map generation supports Windows and macOS; got ' + system)


def locate_engine(explicit, version):
    options = [explicit, os.environ.get('UE_ROOT')]
    if platform.system() == 'Windows':
        options += [f'V:/UE_{version}', f'C:/Program Files/Epic Games/UE_{version}']
    elif platform.system() == 'Darwin':
        options += [f'/Users/Shared/Epic Games/UE_{version}', f'/Volumes/Ouranos/Games/UE_{version}']
    for option in options:
        if option and engine_paths(Path(option))[0].is_file():
            return Path(option).resolve()
        if option and option == explicit:
            raise RuntimeError('Editor executable not found under --engine-root ' + option)
    raise RuntimeError('Unreal Engine not found. Set UE_ROOT or pass --engine-root.')


def input_fingerprint(root, spec, engine):
    project = root/PROJECT_REL
    files = set()
    for folder in ('Source', 'Config', 'Tools'):
        files.update(p for p in (project/folder).rglob('*') if p.is_file()
                     and '__pycache__' not in p.parts and p.suffix in ('.py', '.cpp', '.h', '.cs', '.ini', '.json'))
    files.update([project/'Born2Flap.uproject', root/MANIFEST_REL,
                  root/'Tools/generate_maps.py'])
    for p in (project/'Content').rglob('*'):
        if p.is_file() and p.suffix in ('.uasset', '.umap', '.json') and not generated(p.relative_to(project/'Content').as_posix(), spec):
            if p.stat().st_size < 1024 and p.read_bytes().startswith(LFS_HEADER):
                raise RuntimeError('Unresolved Git LFS pointer: %s. Run git lfs pull.' % p)
            files.add(p)
    h = hashlib.sha256()
    for p in sorted(files):
        h.update(p.relative_to(root).as_posix().encode())
        h.update(digest(p).encode())
    h.update(platform.system().encode())
    h.update(platform.machine().encode())
    h.update((engine/'Engine/Build/Build.version').read_bytes())
    return h.hexdigest()


def output_hashes(root, entry):
    content = root/PROJECT_REL/'Content'
    result = {}
    for pattern in entry['outputs']:
        paths = sorted(content.glob(pattern))
        if not paths:
            raise RuntimeError('Missing generated output: ' + pattern)
        for path in paths:
            if path.stat().st_size < 1024:
                raise RuntimeError('Invalid generated output (possibly an LFS pointer): ' + str(path))
            result[path.relative_to(content).as_posix()] = digest(path)
    return result


def is_current(root, entry, saved, fingerprint):
    if not saved or saved.get('input') != fingerprint:
        return False
    try:
        return saved['outputs'] == output_hashes(root, entry)
    except (OSError, RuntimeError, KeyError):
        return False


def editor_command(engine, project, bootstrap, log, request):
    return [str(engine_paths(engine)[0]), str(project), '-ExecutePythonScript='+str(bootstrap),
            '-B2FGenerationRequest='+str(request), '-unattended', '-nosplash', '-nullrhi', '-nosound',
            '-abslog='+str(log)]


def generation_environment(project_dir):
    env = os.environ.copy()
    # Keep generation's legacy audio cache beside its other disposable outputs.
    # This also avoids exhausting a small system drive on Windows.
    if 'UE-LocalDataCachePath' not in env:
        env['UE-LocalDataCachePath'] = str(project_dir/'Saved/DerivedDataCache')
        # Moving the legacy cache must not override an explicitly configured
        # Zen store. Ignore paths belonging to another host (e.g. V: on macOS).
        config = project_dir/'Config/DefaultEngine.ini'
        section = ''
        if config.is_file():
            for line in config.read_text(encoding='utf-8-sig').splitlines():
                line = line.strip()
                if line.startswith('['):
                    section = line
                elif section == '[Zen.AutoLaunch]' and line.startswith('DataPath='):
                    configured = Path(line.split('=', 1)[1].strip())
                    if configured.is_absolute() and configured.is_dir():
                        env.setdefault('UE-ZenDataPath', str(configured))
    if platform.system() == 'Windows':
        temporary = project_dir/'Saved/GenerationTemp'
        temporary.mkdir(parents=True, exist_ok=True)
        env['TEMP'] = env['TMP'] = str(temporary)
    return env


def build_command(engine, project, system=None):
    _, build, target = engine_paths(engine, system)
    args = [str(build), 'Born2FlapEditor', target, 'Development', '-Project='+str(project),
            '-WaitMutex', '-NoHotReloadFromIDE']
    return ['bash'] + args if target == 'Mac' else args


def write_json(path, data):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(data, indent=2)+'\n', encoding='utf-8')
    temporary.replace(path)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine-root', help='UE install root; alternatively UE_ROOT')
    parser.add_argument('--maps', nargs='+', help='Default: all manifest maps')
    parser.add_argument('--force', action='store_true', help='Regenerate even with a valid cache')
    parser.add_argument('--skip-build', action='store_true', help='Use an already built Born2FlapEditor')
    parser.add_argument('--plan', action='store_true', help='Print commands without building or changing assets')
    parser.add_argument('--timeout', type=int, default=3600, help='Seconds allowed per editor process')
    args = parser.parse_args(argv)
    spec = manifest(REPO)
    names = args.maps or list(spec['maps'])
    if len(set(names)) != len(names) or any(n not in spec['maps'] for n in names):
        parser.error('--maps must contain unique names from '+', '.join(spec['maps']))
    engine = locate_engine(args.engine_root, spec['engine_version'])
    project_dir = REPO/PROJECT_REL
    project = project_dir/'Born2Flap.uproject'
    state_dir = project_dir/'Saved/MapGeneration'
    bootstrap = project_dir/'Tools/generate_maps_editor.py'
    if args.plan:
        print(json.dumps({'build': build_command(engine, project), 'maps': {
            name: editor_command(engine, project, bootstrap, state_dir/(name+'.log'), state_dir/(name+'.request.json'))
            for name in names}}, indent=2))
        return 0
    state_dir.mkdir(parents=True, exist_ok=True)
    # Prevent simultaneous writers, including a second terminal on macOS.
    lock = state_dir/'generation.lock'
    try:
        fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        raise RuntimeError('Generation is already running. If interrupted, verify no generator is running before removing '+str(lock))
    os.close(fd)
    try:
        state_path = state_dir/'state.json'
        state = read_json(state_path) if state_path.exists() else {}
        fingerprint = input_fingerprint(REPO, spec, engine)
        pending = [n for n in names if args.force or not is_current(REPO, spec['maps'][n], state.get(n), fingerprint)]
        for name in names:
            print(name+(': regenerate' if name in pending else ': cached (inputs and outputs verified)'), flush=True)
        if not pending:
            return 0
        if not args.skip_build:
            subprocess.run(build_command(engine, project), cwd=REPO, check=True, timeout=args.timeout)
        completed = {}
        for name in pending:
            log = state_dir/(name+'.log')
            report = state_dir/(name+'.result.json')
            request = state_dir/(name+'.request.json')
            token = str(time.time_ns())
            write_json(request, dict(name=name, token=token, report=str(report), **spec['maps'][name]))
            state.pop(name, None)
            write_json(state_path, state)  # A failed attempt must never retain a valid stamp.
            start = time.monotonic()
            subprocess.run(editor_command(engine, project, bootstrap, log, request), cwd=REPO,
                           env=generation_environment(project_dir), check=True, timeout=args.timeout)
            result = read_json(report) if report.exists() else {}
            if result.get('token') != token or not result.get('success'):
                raise RuntimeError('Map generation failed; see '+str(log))
            errors = [line for line in log.read_text(encoding='utf-8', errors='replace').splitlines()
                      if any(s in line for s in ('LogPython: Error:', 'Error Code 32', 'Failed to save', 'Failed to compile Material'))]
            if errors:
                raise RuntimeError('Generation reported errors: '+'\n'.join(errors[:8]))
            completed[name] = dict(outputs=output_hashes(REPO, spec['maps'][name]), seconds=round(time.monotonic()-start, 2))
            print('%s: saved and checked in %.1fs' % (name, completed[name]['seconds']), flush=True)
        # Some generators also refresh versioned materials. Hash their final
        # state once; old map stamps stay invalid if these shared inputs changed.
        fingerprint = input_fingerprint(REPO, spec, engine)
        for name, result in completed.items():
            state[name] = dict(input=fingerprint, **result)
        write_json(state_path, state)
        return 0
    finally:
        lock.unlink()


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print('Map generation failed: '+str(exc), file=sys.stderr)
        sys.exit(1)
