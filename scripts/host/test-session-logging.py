# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util,os,re,subprocess,sys,tempfile,time
from pathlib import Path
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='session-logging-') as temp:
 d=Path(temp);logs=d/'logs';logs.mkdir()
 for i in range(25):
  p=logs/f'session-20000101-{i:04}.log';p.write_text('old\n');os.utime(p,(i+1,i+1))
 fake=d/'fake-app';fake.write_text('#!/usr/bin/env python3\nimport sys\nprint("stdout line",flush=True)\nprint("stderr line",file=sys.stderr,flush=True)\nraise SystemExit(23)\n');fake.chmod(0o755)
 env=dict(os.environ,BTS_HOST_EXECUTABLE=str(fake),BTS_SESSION_LOG_DIR=str(logs),BTS_ROOT=str(d),QT_QPA_PLATFORM='offscreen',XDG_CONFIG_HOME=str(d/'config'),BTS_RECOVERY_DIR=str(d/'recovery'))
 result=subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--cpu-export-test',str(d/'unused')],env=env,capture_output=True,text=True,timeout=15)
 assert result.returncode==23,(result.returncode,result.stderr)
 files=list(logs.glob('session-*.log'));assert len(files)==20,len(files)
 text=max(files,key=lambda p:p.stat().st_mtime_ns).read_text();assert 'stdout line' in text and 'stderr line' in text
 assert all(re.match(r'^\d{4}-\d\d-\d\dT.*\| ',line) for line in text.splitlines()),text
 # A blocked stdin has no periodic timer or polling work in the logger.
 logger=subprocess.Popen([sys.executable,str(root/'scripts/host/session_logging.py'),'--directory',str(d/'idle')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
 assert 'BTS_SESSION_LOG' in logger.stdout.readline()
 def ticks():return sum(int(x) for x in Path(f'/proc/{logger.pid}/stat').read_text().split()[13:15])
 before=ticks();time.sleep(1);used=(ticks()-before)/os.sysconf('SC_CLK_TCK');logger.stdin.close();logger.wait(timeout=5)
 assert used<.01,used
 unit=Path(os.environ.get('BTS_TEST_BUILD_DIR',str(Path(__file__).resolve().parents[3]/'build/linux-native')))/'bin/bts-ui-stall-tests'
 if unit.is_file():
  native_env=dict(env,BTS_HOST_EXECUTABLE=str(unit),BTS_SESSION_LOG_DIR=str(d/'native-logs'))
  native=subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--cpu-export-test',str(d/'unused')],env=native_env,capture_output=True,text=True,timeout=15)
  assert native.returncode==0,(native.returncode,native.stderr)
  warning=''.join(p.read_text() for p in (d/'native-logs').glob('session-*.log'))
  assert 'FRYSVARNING' in warning and 'artificial_150ms_block=' in warning,warning
 print('session logging PASS: timestamps, stdout+stderr, exit23, retention20, idle CPU seconds='+str(used))
