import subprocess, sys, shutil
from pathlib import Path
import os
sys.path.insert(0, os.environ['ZENO_BENCH_ROOT'] + '/bench')
import tbench_adapter as A

bs = chr(92)
tasks_root = Path(r'C:\Users\GMMETE~1\AppData\Local\Temp\tbench\original-tasks')
root = Path(os.environ['ZENO_BENCH_ROOT'])

for name in ('jsonl-aggregator', 'grid-pattern-transform', 'mahjong-winninghand'):
    ws = root / 'bench/workspaces' / name
    if ws.exists(): shutil.rmtree(ws)
    err = A.setup_workspace(tasks_root / name, ws)
    print(f'=== {name}: setup err={err}')
    if err:
        continue
    sol_path = tasks_root / name / 'solution.sh'
    if not sol_path.exists():
        print('    no solution.sh'); continue
    sol = sol_path.read_text(encoding='utf-8')
    ws_posix = str(ws.resolve()).replace(bs, '/')
    sol2 = sol.replace('/app/', ws_posix + '/')
    script = ws / '_solution.sh'
    script.write_text(sol2, encoding='utf-8')
    rc = subprocess.run(['C:/Program Files/Git/bin/bash.exe', str(script).replace(bs, '/')],
                        capture_output=True, text=True, encoding="utf-8",
                        errors="replace", timeout=600)
    print(f'    solution rc={rc.returncode} stderr={rc.stderr[:150]!r}')
    print('    judge:', A.judge(tasks_root / name, ws)['verdict'])
