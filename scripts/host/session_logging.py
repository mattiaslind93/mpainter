# SPDX-License-Identifier: GPL-3.0-or-later
"""Timestamped stdout/stderr tee; bounded logs, retaining active session files."""
import argparse
from datetime import datetime
import fcntl
import os
from pathlib import Path
import sys

def open_session(directory,keep=20):
    directory.mkdir(parents=True,exist_ok=True)
    stem='session-'+datetime.now().strftime('%Y%m%d-%H%M')
    target=directory/(stem+'.log');index=0
    while True:
        try:stream=target.open('x',encoding='utf-8',buffering=1);break
        except FileExistsError:
            index+=1;target=directory/(stem+f'-{os.getpid()}-{index}.log')
    fcntl.flock(stream,fcntl.LOCK_EX|fcntl.LOCK_NB)
    files=sorted((p for p in directory.glob('session-*.log') if p.is_file() and not p.is_symlink()),key=lambda p:p.stat().st_mtime_ns)
    count=len(files)
    for path in files:
        if count<=keep:break
        if path==target:continue
        try:
            with path.open('a') as old:
                fcntl.flock(old,fcntl.LOCK_EX|fcntl.LOCK_NB)
                path.unlink();count-=1
        except (BlockingIOError,FileNotFoundError):continue
    return target,stream

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--directory',type=Path,required=True);args=parser.parse_args()
    target,log=open_session(args.directory)
    output=True
    def tee(line):
        nonlocal output
        stamped=datetime.now().astimezone().isoformat(timespec='milliseconds')+' | '+line
        log.write(stamped)
        if output:
            try:sys.stdout.write(stamped);sys.stdout.flush()
            except BrokenPipeError:output=False
    tee('BTS_SESSION_LOG '+str(target)+'\n')
    for line in sys.stdin:tee(line if line.endswith('\n') else line+'\n')
    log.close()
if __name__=='__main__':main()
