#!/usr/bin/env python3
"""Terminal-Bench local adapter for ZenoC.

Runs official Terminal-Bench tasks against the ZenoC CLI on Windows without
Docker: reproduces each task's data setup locally, points the agent at the
workspace, then judges the result with the task's own pytest suite (paths
rewritten from /app to the local workspace).

Usage:
  python bench/tbench_adapter.py --task <task_dir> [--tasks dir1,dir2]
Requires: OPENAI_API_KEY, OPENAI_BASE_URL, MODEL_ID env vars.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

TBENCH_TASKS = Path(os.environ.get("TBENCH_TASKS", "")) if os.environ.get("TBENCH_TASKS") else None
ZENO_CLI = Path(os.environ.get("ZENO_CLI", "")) if os.environ.get("ZENO_CLI") else Path(__file__).resolve().parents[1] / "build" / "Release" / "zenoc_cli.exe"
ROOT = Path(os.environ.get("ZENO_BENCH_ROOT", str(Path(__file__).resolve().parents[1])))


def rewrite_app_paths(test_src: str, workspace: Path) -> str:
    """Point the official tests at the local workspace instead of /app."""
    ws = str(workspace).replace("\\", "/").rstrip("/")
    src = re.sub(r'Path\("/app', f'Path("{ws}', test_src)
    src = re.sub(r'"/app/', f'"{ws}/', src)
    src = re.sub(r"'(/app/?)'", f"'{ws}/'", src)
    src = re.sub(r'"/app"', f'"{ws}"', src)
    return src


def setup_workspace(task_dir: Path, workspace: Path) -> str | None:
    """Reproduce the task's /app data setup. Returns None on success."""
    dockerfile = task_dir / "Dockerfile"
    text = dockerfile.read_text(encoding="utf-8", errors="replace") if dockerfile.exists() else ""
    workspace.mkdir(parents=True, exist_ok=True)

    # Copy any files the Dockerfile COPYs from the task dir (best effort).
    for match in re.finditer(r"^COPY\s+(\S+)\s+/app/(\S+)", text, re.M):
        src_name, dst_name = match.group(1), match.group(2)
        src = task_dir / src_name
        if src.is_file():
            shutil.copy2(src, workspace / dst_name)
        elif src.is_dir():
            shutil.copytree(src, workspace / dst_name, dirs_exist_ok=True)

    # jsonl-aggregator: generate records from task-deps.
    gen = task_dir / "task-deps" / "generate_records.py"
    if gen.exists():
        rc = subprocess.run([sys.executable, str(gen)], cwd=workspace,
                            capture_output=True, timeout=300)
        if rc.returncode != 0:
            return f"record generator failed: {rc.stderr.decode(errors='replace')[:300]}"
        gen_copy = workspace / "generate_records.py"
        if gen_copy.exists():
            gen_copy.unlink()

    # mahjong-winninghand: protected hands live outside /app; the tests read them.
    protected = task_dir / "protected"
    if protected.exists():
        shutil.copytree(protected, workspace / "protected", dirs_exist_ok=True)

    # log-summary-date-ranges style generators.
    for gen_name in ("log_generator_deterministic.py",):
        gen2 = task_dir / gen_name
        if gen2.exists():
            rc = subprocess.run([sys.executable, str(gen2)], cwd=workspace,
                                capture_output=True, timeout=300)
            if rc.returncode != 0:
                return f"{gen_name} failed: {rc.stderr.decode(errors='replace')[:300]}"

    # Tasks whose setup is a shell command in the Dockerfile (best effort for RUN lines we understand).
    if "archive.tar" in text:
        return "task requires Dockerfile-built archive; not portable"

    # grid-pattern-transform: tests import the solver module from cwd.
    return None


def run_agent(task_dir: Path, workspace: Path, instruction: str, model_env: dict) -> dict:
    env = dict(os.environ)
    env.update(model_env)
    env["ZENO_AUTO_APPROVE"] = "1"
    env["REQUIRE_APPROVAL"] = "false"
    env["WORKSPACE_ROOT"] = str(workspace)
    env["ZENO_RUNS_DIR"] = str(ROOT / "bench" / "runs")
    started = time.time()
    proc = subprocess.run(
        [str(ZENO_CLI), "--minimal", "--run", "tbench", instruction],
        cwd=str(workspace), env=env, capture_output=True, text=True,
        encoding="utf-8", errors="replace", timeout=1500,
    )
    return {
        "stdout": proc.stdout[-4000:],
        "stderr": proc.stderr[-4000:],
        "exit": proc.returncode,
        "seconds": round(time.time() - started, 1),
    }


# The CLI has no checkpoint resume; on a transient "LLM provider failure" we
# restart the whole run on a clean workspace (up to 3 attempts).
def run_agent_with_retry(task_dir: Path, workspace: Path, instruction: str,
                         model_env: dict, attempts: int = 3) -> tuple[dict, int]:
    last = {"exit": -1, "stderr": "", "stdout": "", "seconds": 0.0}
    for i in range(attempts):
        if i > 0:
            shutil.rmtree(workspace, ignore_errors=True)
            workspace.mkdir(parents=True, exist_ok=True)
            err = setup_workspace(task_dir, workspace)
            if err:
                break
            print(f"    retry {i}: workspace reset after provider failure", flush=True)
        last = run_agent(task_dir, workspace, instruction, model_env)
        if "LLM provider failure" not in last["stderr"] and "status=failed" not in last["stderr"]:
            return last, i
        time.sleep(5 * (i + 1))
    return last, attempts - 1


def judge(task_dir: Path, workspace: Path) -> dict:
    tests_dir = task_dir / "tests"
    local_tests = workspace / "_tb_tests"
    if local_tests.exists():
        shutil.rmtree(local_tests)
    local_tests.mkdir()
    for f in tests_dir.glob("*.py"):
        (local_tests / f.name).write_text(rewrite_app_paths(f.read_text(encoding="utf-8", errors="replace"), workspace),
                                          encoding="utf-8")
    rc = subprocess.run(
        [sys.executable, "-m", "pytest", "-x", "-q", str(local_tests)],
        cwd=str(workspace), capture_output=True, text=True, encoding="utf-8",
        errors="replace", timeout=600,
    )
    passed = rc.returncode == 0
    return {"verdict": "pass" if passed else "fail",
            "pytest_tail": (rc.stdout + rc.stderr)[-1500:]}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--task", action="append", default=[])
    ap.add_argument("--json-out", default=str(ROOT / "bench" / "tbench_results.json"))
    args = ap.parse_args()

    tasks_root = TBENCH_TASKS or Path("/tmp/tbench/original-tasks")
    if not tasks_root.exists():
        print(f"terminal-bench tasks not found at {tasks_root}", file=sys.stderr)
        return 2

    model_env = {k: os.environ[k] for k in ("OPENAI_API_KEY", "OPENAI_BASE_URL", "MODEL_ID") if k in os.environ}
    if "OPENAI_API_KEY" not in model_env:
        print("OPENAI_API_KEY not set", file=sys.stderr)
        return 2

    results = []
    for name in args.task:
        task_dir = tasks_root / name
        if not task_dir.is_dir():
            results.append({"task": name, "verdict": "error", "reason": "task not found"})
            continue
        workspaces = ROOT / "bench" / "workspaces"
        workspaces.mkdir(parents=True, exist_ok=True)
        workspace = workspaces / name
        if workspace.exists():
            shutil.rmtree(workspace)
        setup_err = setup_workspace(task_dir, workspace)
        if setup_err:
            results.append({"task": name, "verdict": "error", "reason": setup_err})
            continue
        instruction = ""
        m = re.search(r"^instruction: \|-?\n((?:[ \t]+.*\n)+)", task_dir.joinpath("task.yaml").read_text(encoding="utf-8", errors="replace"), re.M)
        if m:
            # Dedent the block scalar.
            lines = [ln.lstrip() for ln in m.group(1).rstrip("\n").split("\n")]
            instruction = "\n".join(lines)
        # Container-era paths in the instruction must point at the workspace.
        # The CLI runs with cwd=workspace and its read tool only accepts
        # relative paths, so strip the /app prefix rather than substituting
        # absolute paths.
        instruction = instruction.replace('"/app"', '"."')
        instruction = instruction.replace("/app/", "")
        instruction = instruction.replace("/app", ".")
        print(f"=== {name}: running agent...", flush=True)
        agent, attempts = run_agent_with_retry(task_dir, workspace, instruction, model_env)
        print(f"=== {name}: agent done in {agent['seconds']}s (exit {agent['exit']}, attempts {attempts + 1}), judging...", flush=True)
        verdict = judge(task_dir, workspace)
        results.append({"task": name, **verdict, "agent_exit": agent["exit"],
                        "attempts": attempts + 1,
                        "seconds": agent["seconds"], "stderr": agent["stderr"][-600:]})
        print(f"=== {name}: {verdict['verdict'].upper()} ({agent['seconds']}s)", flush=True)

    out = Path(args.json_out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(results, indent=2), encoding="utf-8")
    passed = sum(1 for r in results if r["verdict"] == "pass")
    print(f"\nTerminal-Bench (local adapter): {passed}/{len(results)} passed")
    for r in results:
        print(f"  {r['task']}: {r['verdict']} ({r.get('seconds', '?')}s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
