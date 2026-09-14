#!/usr/bin/env python3
"""zeno-bench: proprietary mini benchmark for ZenoC.

12 tasks with deterministic objective judges (content equality / test-run
outcome). No LLM judging. Each task: setup() prepares a workspace, judge()
checks the workspace after the agent run. Runs the agent via
`zenoc_cli --minimal --run zeno-bench <instruction>` with ZENO_AUTO_APPROVE=1.

Usage:
  python bench/zeno_bench.py [--task NAME ...] [--json-out PATH]
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(os.environ.get("ZENO_BENCH_ROOT", str(Path(__file__).resolve().parents[1])))
CLI = ROOT / "build" / "Release" / "zenoc_cli.exe"
if not CLI.exists():
    CLI = ROOT / "build" / "Release" / "zenoc_cli"
WS_ROOT = ROOT / "bench" / "zbench_workspaces"
AGENT_TIMEOUT = 900  # seconds per task


# ---------------------------------------------------------------- task setup helpers

def w(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


# ---------------------------------------------------------------- tasks
# each: name, instruction, setup(ws), judge(ws) -> (ok: bool, detail: str)

TASKS = []

def task(name, instruction, setup, judge):
    TASKS.append({"name": name, "instruction": instruction, "setup": setup, "judge": judge})


# 1. exact file creation
def t1_setup(ws):
    w(ws / "target.txt", "")
task("file-create-exact",
     "Create a file named target.txt in the workspace with exactly this content (no extra whitespace, single trailing newline):\nZENO=ready\nlines=3\nok=yes",
     t1_setup,
     lambda ws: _eq(ws / "target.txt", "ZENO=ready\nlines=3\nok=yes\n"))


def _eq(path: Path, expected: str):
    if not path.exists():
        return False, f"{path.name} missing"
    got = path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
    return (got == expected), f"expected={expected!r} got={got!r}"


# 2. in-place edit
def t2_setup(ws):
    w(ws / "config.ini", "host=localhost\nport=8080\ndebug=true\nretries=3\n")
task("config-edit",
     "In config.ini, change the value of port to 9090 and debug to false. Leave every other line untouched, keep the same order.",
     t2_setup,
     lambda ws: _eq(ws / "config.ini", "host=localhost\nport=9090\ndebug=false\nretries=3\n"))


# 3. csv arithmetic
CSV3 = "item,qty,price\napple,4,1.5\npear,2,2.25\nkiwi,10,0.4\n"
def t3_setup(ws):
    w(ws / "sales.csv", CSV3)
task("csv-total",
     "Read sales.csv and write the grand total of qty*price for all rows to a file named total.txt containing only the number with exactly 2 decimal places (e.g. 12.34) and a trailing newline.",
     t3_setup,
     lambda ws: _eq(ws / "total.txt", "11.50\n"))  # 4*1.5=6 + 2*2.25=4.5 + 10*0.4=4 = 14.5 -> fix below
# correct total: 6 + 4.5 + 4 = 14.50
TASKS[-1]["judge"] = lambda ws: _eq(ws / "total.txt", "14.50\n")


# 4. json transform
def t4_setup(ws):
    w(ws / "people.json", json.dumps(
        [{"name": "ana", "age": 31}, {"name": "bruno", "age": 25}, {"name": "carla", "age": 40}], indent=1))
task("json-transform",
     "Read people.json (an array of {name, age} objects) and write people_out.json containing a JSON object with two keys: \"adults\" (names of people aged 30 or more, in the same order as the input) and \"average_age\" (the mean age, a number with at most 2 decimal places, e.g. 32.0).",
     t4_setup,
     lambda ws: _json_eq(ws / "people_out.json", {"adults": ["ana", "carla"], "average_age": 32.0}))


def _json_eq(path: Path, expected):
    if not path.exists():
        return False, "people_out.json missing"
    try:
        got = json.loads(path.read_text(encoding="utf-8", errors="replace"))
    except Exception as e:
        return False, f"invalid JSON: {e}"
    avg = got.get("average_age")
    if isinstance(avg, (int, float)):
        got["average_age"] = round(float(avg), 2)
    ok = got == expected
    return ok, f"expected={expected!r} got={got!r}"


# 5. fix a python bug
BUGGY = 'def add_items(items):\n    total = 0\n    for it in items:\n        total = it\n    return total\n\nif __name__ == "__main__":\n    print(add_items([2, 3, 5]))\n'
def t5_setup(ws):
    w(ws / "sum.py", BUGGY)
    w(ws / "SUM_EXPECTED", "10\n")
def t5_judge(ws):
    p = ws / "sum.py"
    if not p.exists():
        return False, "sum.py missing"
    r = subprocess.run([sys.executable, str(p)], capture_output=True, text=True, timeout=60)
    ok = r.returncode == 0 and r.stdout.strip() == "10"
    return ok, f"rc={r.returncode} out={r.stdout.strip()!r} err={r.stderr.strip()[:200]!r}"
task("fix-python-bug",
     "sum.py has a bug: add_items must return the SUM of the numbers in the list, but it returns the last one. Fix the bug in sum.py (keep the function name and print at the bottom). Verify with: python sum.py  (must print 10).",
     t5_setup,
     t5_judge)


# 6. regex extraction
TXT6 = "contact alice@example.com or bob.smith@mail.org; cc=fake@not-an-email; also carol99@sub.domain.io thanks\n"
def t6_setup(ws):
    w(ws / "notes.txt", TXT6)
task("regex-extract",
     "Extract every email address from notes.txt and write one per line (in order of appearance) to emails.txt. An email is localpart@domain.tld; 'fake@not-an-email' is NOT a valid email (domain has no dot).",
     t6_setup,
     lambda ws: _eq(ws / "emails.txt", "alice@example.com\nbob.smith@mail.org\ncarol99@sub.domain.io\n"))


# 7. sort lines
def t7_setup(ws):
    w(ws / "words.txt", "banana\napple\ncherry\nalien\n")
task("sort-lines",
     "Sort the lines of words.txt alphabetically and overwrite words.txt with the sorted lines (keep one word per line, trailing newline).",
     t7_setup,
     lambda ws: _eq(ws / "words.txt", "alien\napple\nbanana\ncherry\n"))


# 8. multi-file module
def t8_judge(ws):
    r = subprocess.run([sys.executable, "-c",
                        "import sys; sys.path.insert(0, r'%s'); import app; print(app.run(2, 3))" % str(ws)],
                       capture_output=True, text=True, timeout=60)
    ok = r.returncode == 0 and r.stdout.strip() == "56"
    return ok, f"rc={r.returncode} out={r.stdout.strip()!r} err={r.stderr.strip()[:300]!r}"
task("multi-file-module",
     "Create two Python files: utils.py defining add(a, b) -> a + b and mul(a, b) -> a * b; and app.py importing from utils and defining run(a, b) that returns the string f\"{add(a,b)}{mul(a,b)}\" (concatenation of the two results as text).",
     lambda ws: None,
     t8_judge)


# 9. dedupe with counts
def t9_setup(ws):
    w(ws / "log.txt", "GET /a\nGET /b\nPOST /a\nGET /a\nDELETE /c\nPOST /a\n")
task("count-unique",
     "Analyze log.txt (one HTTP line per request). Write report.json as a JSON object mapping each unique line to the number of times it appears (keys in order of first appearance).",
     t9_setup,
     lambda ws: _json_eq2(ws / "report.json", {"GET /a": 2, "GET /b": 1, "POST /a": 2, "DELETE /c": 1}))


def _json_eq2(path: Path, expected):
    if not path.exists():
        return False, "report.json missing"
    try:
        got = json.loads(path.read_text(encoding="utf-8", errors="replace"))
    except Exception as e:
        return False, f"invalid JSON: {e}"
    return got == expected, f"expected={expected!r} got={got!r}"


# 10. patch a C file
C10 = "#include <stdio.h>\nint main(void) {\n    for (int i = 1; i <= 3; i++)\n        printf(\"%d \", i);\n    return 0;\n}\n"
def t10_setup(ws):
    w(ws / "count.c", C10)
task("edit-c-file",
     "In count.c, change the loop so it prints numbers 1 through 5 inclusive instead of 1 through 3. Keep everything else identical.",
     t10_setup,
     lambda ws: _eq(ws / "count.c", "#include <stdio.h>\nint main(void) {\n    for (int i = 1; i <= 5; i++)\n        printf(\"%d \", i);\n    return 0;\n}\n"))


# 11. two-step chain (read then transform)
def t11_setup(ws):
    w(ws / "seed.txt", "FACTOR=7\nOFFSET=2\n")
task("computed-write",
     "Read seed.txt (key=value lines). Compute FACTOR*OFFSET+100 and write only that number to result.txt with a trailing newline.",
     t11_setup,
     lambda ws: _eq(ws / "result.txt", "114\n"))


# 12. multi-tool: write, execute, verify
def t12_setup(ws):
    pass
def t12_judge(ws):
    p = ws / "fib.py"
    if not p.exists():
        return False, "fib.py missing"
    r = subprocess.run([sys.executable, str(p)], capture_output=True, text=True, timeout=60)
    ok = r.returncode == 0 and r.stdout.strip() == "55"
    return ok, f"rc={r.returncode} out={r.stdout.strip()!r} err={r.stderr.strip()[:200]!r}"
task("write-and-run",
     "Create fib.py that prints the 10th Fibonacci number (fib(1)=1, fib(2)=1, fib(10)=55) when run with python fib.py. Run it to verify it prints 55.",
     t12_setup,
     t12_judge)


# ---------------------------------------------------------------- runner

def run_agent(name: str, instruction: str, ws: Path) -> dict:
    env = os.environ.copy()
    env.setdefault("OPENAI_API_KEY", os.environ.get("OPENAI_API_KEY", ""))
    env.setdefault("OPENAI_BASE_URL", "https://api.b.ai/v1")
    env.setdefault("MODEL_ID", "glm-5.3-flash")
    env["ZENO_AUTO_APPROVE"] = "1"
    env["REQUIRE_APPROVAL"] = "false"
    env["WORKSPACE_ROOT"] = str(ws)
    env["ZENO_RUNS_DIR"] = str(ROOT / "bench" / "runs")
    cmd = [str(CLI), "--minimal", "--run", "zeno-bench", instruction]
    t0 = time.time()
    try:
        r = subprocess.run(cmd, cwd=str(ws), env=env, capture_output=True,
                           text=True, encoding="utf-8", errors="replace",
                           timeout=AGENT_TIMEOUT)
        rc, out, err = r.returncode, r.stdout, r.stderr
    except subprocess.TimeoutExpired as e:
        rc, out, err = -1, (e.stdout or b"").decode("utf-8", "replace") if isinstance(e.stdout, bytes) else str(e.stdout or ""), "timeout"
    dt = time.time() - t0
    m = re.search(r"tokens_in=(\d+) tokens_out=(\d+) cached=(\d+)", err)
    return {"rc": rc, "duration_s": round(dt, 1),
            "tokens_in": int(m.group(1)) if m else None,
            "tokens_out": int(m.group(2)) if m else None,
            "tokens_cached": int(m.group(3)) if m else None,
            "stderr_tail": err[-500:]}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--task", action="append", default=[])
    ap.add_argument("--json-out", default=str(ROOT / "bench" / "zbench_results.json"))
    args = ap.parse_args()
    names = args.task or [t["name"] for t in TASKS]
    selected = [t for t in TASKS if t["name"] in names]
    missing = [n for n in names if n not in {t["name"] for t in TASKS}]
    if missing:
        print(f"unknown tasks: {missing}", file=sys.stderr)
        return 2
    results = []
    for t in selected:
        ws = WS_ROOT / t["name"]
        if ws.exists():
            subprocess.run(["rmdir", "/s", "/q", str(ws)], shell=True,
                           capture_output=True) if os.name == "nt" else None
            ws.mkdir(parents=True, exist_ok=True)
        else:
            ws.mkdir(parents=True, exist_ok=True)
        t["setup"](ws)
        print(f"[zeno-bench] {t['name']}: running agent...", flush=True)
        run = run_agent(t["name"], t["instruction"], ws)
        ok, detail = t["judge"](ws)
        rec = {"task": t["name"], "verdict": "pass" if ok else "fail",
               "detail": detail, **run}
        results.append(rec)
        print(f"[zeno-bench] {t['name']}: {rec['verdict']} ({rec['duration_s']}s) {detail[:160]}", flush=True)
    summary = {"total": len(results),
               "passed": sum(1 for r in results if r["verdict"] == "pass"),
               "tokens_in": sum(r["tokens_in"] or 0 for r in results),
               "tokens_out": sum(r["tokens_out"] or 0 for r in results),
               "duration_s": round(sum(r["duration_s"] for r in results), 1)}
    out = {"summary": summary, "results": results}
    Path(args.json_out).write_text(json.dumps(out, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
