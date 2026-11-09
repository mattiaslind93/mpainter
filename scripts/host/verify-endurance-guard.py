# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify that the endurance watchdog can stop only its isolated test process."""
import os,signal,tempfile,importlib.util
from pathlib import Path
spec=importlib.util.spec_from_file_location("ab_resolution",Path(__file__).with_name("verify-ab-resolution.py"))
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
stop_private_host=module.stop_private_host
root=Path(os.environ.get('BTS_ROOT','/home/mlind/bts'))/'build'
with tempfile.TemporaryDirectory(prefix='endurance-guard-',dir=root) as tmp:
 out=Path(tmp)/'test';proc=Path(tmp)/'proc';proc.mkdir();signals=[]
 for pid,config,demo in [(1,'/artist/config',None),(2,str(out/'config'),str(out/'workspace')),(3,str(out/'config'),'/other/workspace'),(4,str(out/'config'),None)]:
  entry=proc/str(pid);entry.mkdir();(entry/'comm').write_text('bts-host\n');args=['bts-host','--workspace']+(['--gui-fix-demo',demo]if demo else []);(entry/'cmdline').write_bytes(('\0'.join(args)+'\0').encode());(entry/'environ').write_bytes(('XDG_CONFIG_HOME='+config+'\0').encode())
 assert stop_private_host(out,proc,lambda pid,sig:signals.append((pid,sig)))==[2]
 assert signals==[(2,signal.SIGTERM)]
print('Endurance guard: exact config+demo ownership required; artist and other instances preserved')
