import subprocess, sys, shutil
from pathlib import Path
import os
sys.path.insert(0, os.environ['ZENO_BENCH_ROOT'] + '/bench')
import tbench_adapter as A

bs = chr(92)
tasks_root = Path(r'C:\Users\GMMETE~1\AppData\Local\Temp\tbench\original-tasks')
name = 'analyze-access-logs'
ws = Path(os.environ['ZENO_BENCH_ROOT'] + '/bench/workspaces') / name
if ws.exists(): shutil.rmtree(ws)
err = A.setup_workspace(tasks_root / name, ws)
print('setup err:', err)
print('files:', [p.name for p in ws.iterdir()])

sol = (tasks_root / name / 'solution.sh').read_text(encoding='utf-8')
ws_posix = str(ws.resolve()).replace(bs, '/')
sol2 = sol.replace('/app/', ws_posix + '/')
script = ws / '_solution.sh'
script.write_text(sol2, encoding='utf-8')
rc = subprocess.run(['C:/Program Files/Git/bin/bash.exe', str(script).replace(bs, '/')],
                    capture_output=True, text=True, timeout=120)
print('solution rc:', rc.returncode, rc.stderr[:200])

print('judge:', A.judge(tasks_root / name, ws)['verdict'])
