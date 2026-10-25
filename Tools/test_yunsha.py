#!/usr/bin/env python3
"""Render Yunsha and check native collision/soaring assertions on Windows/macOS."""
import argparse
from pathlib import Path
import subprocess
import sys
import time
from generate_maps import locate_engine, engine_paths, REPO, PROJECT_REL, generation_environment


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--engine-root')
    p.add_argument('--time',choices=['dawn','morning','noon','sunset','night'],default='sunset')
    p.add_argument('--season',choices=['dry','monsoon','cool'],default='dry')
    p.add_argument('--no-render',action='store_true')
    p.add_argument('--timeout',type=int,default=900,help='Allow first-launch shader compilation')
    args=p.parse_args()
    engine=locate_engine(args.engine_root,'5.8');project=REPO/PROJECT_REL
    log=project/'Saved/Logs'/('yunsha-%s-%s.log'%(args.time,args.season))
    log.parent.mkdir(parents=True,exist_ok=True)
    command=[str(engine_paths(engine)[0]),str(project/'Born2Flap.uproject'),'/Game/Yunsha/Maps/YUNSHA',
             '-game','-B2FLevel=Yunsha','-B2FYunshaTest','-B2FNoMenu','-unattended','-nosplash',
             '-B2FDayTime='+args.time,'-B2FSeason='+args.season,
             '-B2FWeather='+('rain' if args.season=='monsoon' else 'sunny'),'-abslog='+str(log)]
    command+=['-nullrhi'] if args.no_render else ['-RenderOffscreen','-ResX=1600','-ResY=900']
    started=time.time()
    result=subprocess.run(command,cwd=REPO,env=generation_environment(project),timeout=args.timeout)
    text=log.read_text(encoding='utf-8',errors='replace')
    errors=[line for line in text.splitlines() if any(x in line for x in ('YunshaCheck','YunshaTest','Failed to compile Material'))]
    print('\n'.join(errors))
    if result.returncode or 'YunshaTest PASS' not in text or 'Failed to compile Material' in text:
        raise RuntimeError('Yunsha verification failed: '+str(log))
    if not args.no_render:
        shots=project/'Saved/Screenshots'/('WindowsEditor' if sys.platform=='win32' else 'MacEditor')
        missing=[str(shots/('YUNSHA_%s_%s_%d.png'%(args.time,args.season,i))) for i in range(8)
                 if not (shots/('YUNSHA_%s_%s_%d.png'%(args.time,args.season,i))).is_file()
                 or (shots/('YUNSHA_%s_%s_%d.png'%(args.time,args.season,i))).stat().st_mtime < started]
        if missing:raise RuntimeError('Missing captures: '+', '.join(missing))
    return 0

if __name__=='__main__':
    sys.exit(main())
