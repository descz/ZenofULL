# ZenoC

ZenoC is the compact C11 runtime for the Zeno agent. It keeps the behavior that
matters in production while making process boundaries, ownership, and effect
classification explicit.

## Scope

Implemented in the portable core:

- ReAct loop with structured OpenAI-compatible tool calls, textual fallbacks and SSE streaming;
- turn limits, repeated-call loop detection, history compaction, checkpoints and resume;
- tool registry with required-argument validation, effects, retries, timeouts and schemas;
- workspace-safe file operations, search, globbing and backups, with traversal and symlink/reparse-point defenses;
- sandbox policy, destructive/network command blocking, opt-in shell operators, process-group cleanup, bounded output/runtime and persisted background jobs;
- memory, preferences, user profiles, session context, deterministic embeddings and Markdown persistence;
- LLM provider routing, health statistics, retries, cooldowns, streaming and cache;
- approval gates, traces in JSONL, transcripts and caveman token-economy helpers;
- Git (init/checkpoint/diff/log/status/branches/rollback), codebase indexing, assertions, read-only database and a real test runner (pytest/npm/cargo/go/ctest);
- planner, plan execution (`run_with_plan`), sub-agent/squad helpers and learned sequences;
- per-run token accounting (`tokens_in`/`tokens_out` on every result, provider-reported usage aggregated across all turns, cache hits included);
- wall-clock run budget (`ZenoRunOptions.wall_clock_budget_ms`) surfaced as the `deadline_exceeded` status at safe turn boundaries;
- consolidation gateways: `zenoc_eval` (offline behavioral scenarios with JSON metrics per run) and `zenoc_fuzz` (seeded deterministic fuzzer over the JSON/completion/escape surfaces) both wired into CTest; `ZENO_WERROR` for warning-as-error CI builds;
- senior coding discipline enforced by the runtime: read-before-edit ledger (existing files must be read in-session before mutation), bounded `+N/-M` change summaries on every write, and a verification gate that gives one structured reminder plus extra budget when a run modified files without verifying them afterwards;
- self-correction loop: oversized tool output is truncated keeping head **and** tail (where build/test errors live), exact-repeat loops receive one corrective runtime notice before the run is stopped before side effects, and handler retries back off linearly;
- orchestration tools callable by the model itself: `update_plan` (live Markdown checklist re-injected into every system prompt) and `spawn_subagent` (isolated read-only subagent; approvals denied, nesting limited to one level);
- cooperative cancellation at turn boundaries via `zeno_agent_request_cancel` or `ZenoRunOptions.should_cancel`;
- MCP client over HTTP **and stdio**, vision calls and the bounded HTTP browser reader through `libcurl` when found by CMake;
- OS-level process containment on Windows: every sandbox command and spawned MCP server runs inside a Job Object with kill-on-close and a 2 GB per-process memory cap, so runaway children cannot outlive the run or exhaust memory;
- provider prompt caching: cached-token accounting (`tokens_cached` on every result, read from `prompt_tokens_details.cached_tokens`) plus the skills index and plan pinned to the top of the system prompt so the byte prefix stays cache-stable; reasoning models that hit `finish_reason=length` with empty content get one automatic retry with a doubled token budget;
- ecosystem layer (AI Suite-compatible): skills (`<dir>/<name>/SKILL.md` indexed into the prompt + `load_skill` tool), agent definition files (`<dir>/<name>.md` with frontmatter `name/description/tools/model` and a body used as the role system prompt) and stateless hooks (`~/.zeno/hooks.json`, `pre_tool` exit 2 blocks the call, `post_tool` observes effects).

## Squads and parallel execution

Concurrency is real and lives in the runtime (`src/zeno_parallel.c`), not in
prompts:

- `zeno_agent_run_parallel` runs independent tasks on OS threads and keeps
  results in task order. Providers are snapshotted under a lock and transports
  run outside it, so parallel agents issue LLM calls concurrently while
  health statistics stay consistent.
- `zeno_agent_run_squad` runs a real squad: every role (default
  `coder,reviewer,tester`) gets its own agent session with a role brief, roles
  execute in parallel, and a synthesis pass integrates the outputs into one
  final answer. The legacy `zeno_squad_run` remains as the offline helper.
- Role tool access is enforced by the runtime, not by prompt wording: each
  role receives a scoped clone of the registry (`registry_scope_clone`), so a
  reviewer physically has no write tools to call — the attempt fails with
  "not available for this role" before any handler runs. The default scope is
  `squad_coder_tools` (read/write/edit/run + `mcp_call`) versus
  `squad_reviewer_tools` (read-only tools plus assertions); custom role tool
  lists are accepted via `zeno_agent_run_squad`.
- `zeno_agent_run_squad_ex` (v1.3.0) loads AI Suite-style agent definitions
  from `<agents_dir>/*.md`: frontmatter `name`, `description`, `tools` (comma
  list that can only **narrow** the role's default scope, never widen it),
  `model` (honored per role) and the Markdown body as the role system prompt.
  Explicit `roles_csv` selects a subset; with no CSV every definition runs.
- Inside a single turn, tool calls run concurrently when the set is safe:
  read-only tools or writes/edits to distinct files (same rule as
  `zeno_agent_can_parallelize`). Anything else falls back to the sequential
  path with approval gates intact. Logs and events stay deterministic because
  they are emitted after the join, in order.
- Shared state is synchronized: memory, cache, trace, approval store, sandbox
  job registry, router stats and transcript appends all use recursive mutexes
  with lazy initialization.

## Agent modes

`ZenoAgentOptions.mode` selects the operating profile:

- **full** (default): every builtin tool, 20-turn budget, 120k history.
- **minimal**: the precision coding profile. `zeno_registry_register_minimal`
  registers exactly four tools (`read_text_file`, `write_text_file`,
  `replace_in_file`, `run_command`), budgets tighten (12 turns, 40k history,
  8k tool output, 2048 max tokens, temperature 0.1) and the system prompt
  enforces smallest-correct-change discipline with mandatory verification.
  The goal is speed, low token cost and a small error margin.

The mode also comes from the `ZENO_AGENT_MODE=minimal|full` environment
variable and the CLI `--minimal` flag; `zeno_agent_health_json` reports it.


## External integrations: MCP first

MCP (Model Context Protocol, JSON-RPC 2.0) is the general bridge to external
tool servers — not a Composio exclusive:

- the `mcp_call` builtin tool calls any tool on any MCP server over HTTP
  (`{"server_url": "...", "tool": "...", "arguments": {...}}`) or by spawning
  a stdio server (`{"command": "python server.py", ...}`); both are classified
  as write-external effects and go through the approval gate;
- stdio transport (v1.3.0): the runtime performs the `initialize` handshake,
  sends `notifications/initialized`, then the target request, and matches the
  response by id — compatible with any newline-delimited JSON-RPC server;
- `zeno_mcp_list_tools` / `zeno_mcp_list_tools_stdio` perform `tools/list`
  discovery against a server URL or command;
- Composio rides on the same bridge: `zeno_composio_call` and the status
  report (`"bridge": "mcp"`) are thin wrappers over the MCP client, so any
  MCP-compatible server (Composio or otherwise) is a first-class integration.

Browser automation remains an explicit adapter point: ZenoC provides a
small HTTP reader for `browser_navigate` when libcurl is available, but does not
pretend to be a JavaScript-capable browser. This keeps the offline build small and
makes the security boundary live in C instead of in a prompt.
`zeno_router_complete_stream` falls back to non-streaming delivery when libcurl is
absent, so agent code can always request streaming.

## Ecosystem: skills, agent definitions and hooks

The v1.3.0 ecosystem layer follows the AI Suite conventions so existing agent
packs work unchanged (`examples/ecosystem` ships a working sample):

- **Skills** — a directory of subdirectories each holding a `SKILL.md` with
  YAML frontmatter (`name`, `description`). Attach with
  `zeno_agent_attach_skills` or the CLI `--skills-dir <dir>`: the index joins
  the system prompt (top, cache-stable prefix) and a `load_skill` tool loads
  the full instructions on demand. Path traversal (`..`, absolute paths,
  separators) is rejected by the runtime.
- **Agent definitions** — Markdown files with frontmatter `name`,
  `description`, `tools`, `model`; the body is the role system prompt. Used by
  `zeno_agent_run_squad_ex` / CLI `--squad-ex <agents_dir> <session> <task>
  [roles_csv]`; defs narrow (never widen) the role's tool scope and may pin a
  per-role model.
- **Hooks** — `~/.zeno/hooks.json` with
  `{"hooks":[{"event":"pre_tool|post_tool","command":"...","timeout_ms":3000}]}`
  . Hooks run as processes with a JSON payload on stdin and are re-read per
  call (stateless, parallel-safe). A `pre_tool` hook that exits with code 2
  blocks the tool call (`hook_denied`); `post_tool` runs after every executed
  tool for side effects such as audit logging.

## Benchmarks

Two benchmark layers live in `bench/`. Both run the CLI unattended
(`ZENO_AUTO_APPROVE=1`) and judge with deterministic, official tests — no LLM
judging anywhere.

**Terminal-Bench (official tasks, local adapter).** `bench/tbench_adapter.py`
runs official tasks from
[laude-institute/terminal-bench](https://github.com/laude-institute/terminal-bench)
without Docker: it prepares the workspace from the Dockerfile COPY lines and
task generators, rewrites the tests' `/app` paths to the local workspace, runs
the agent with the task instruction, then judges with the official pytest suite.
Four portable tasks were run live against `glm-5.3-flash` (b.ai); the judges
were validated beforehand with the official `solution.sh` scripts (4/4 pass).
Result: **2/4**.

| Task | Verdict | Notes |
|------|---------|-------|
| jsonl-aggregator | pass | 12 turns; aggregates + verification; 151.6s |
| analyze-access-logs | pass | 10 turns; report.txt built and verified; 88.1s |
| grid-pattern-transform | fail | wrote `transform.py`; tests import `grid_transform.py` (name given in instruction) |
| mahjong-winninghand | fail | wrote and ran a solver but never produced `result.txt` over the 8 protected hands |

Both failures are model shortfalls, not harness issues — the same judges pass
with the official solutions. Results: `bench/tbench_results.json`.

**zeno-bench (proprietary mini benchmark).** `bench/zeno_bench.py` is 12
deterministic tasks (exact file creation, config edit, CSV arithmetic, JSON
transform, Python bug fix, regex extraction, sorting, multi-file module,
dedup-with-counts, C edit, computed write, write-and-run) judged by content
equality or executing the produced code. Live run against `glm-5.3-flash`
(b.ai): **12/12**, 949.8s total wall, 126,341 tokens in (74.7% served from
prompt cache), 5,882 out. Per-task tokens/duration are in
`bench/zbench_results.json`.

Reproduce:

```text
# Terminal-Bench via the local adapter
python bench/tbench_adapter.py --task analyze-access-logs --task jsonl-aggregator ...

# zeno-bench (all 12 tasks)
python bench/zeno_bench.py
```

## Build and test

From the repository root (portable offline preset):

```text
cmake --preset offline-debug
cmake --build --preset offline-debug
ctest --preset offline-debug --output-on-failure
```

For a Release build with optional libcurl (and an installed dependency/toolchain
when needed), use the `curl-release` preset or equivalent `cmake -S/-B` commands.

When using vcpkg, set `CMAKE_TOOLCHAIN_FILE` to a local
[vcpkg](https://vcpkg.io) installation that provides curl (for example,
`vcpkg install curl:x64-windows`). On Windows the vcpkg port uses native Schannel
TLS, so no OpenSSL is needed.
Without libcurl the build remains deterministic and offline (HTTP, streaming, MCP
and vision report that the adapter is not enabled). The repository also ships a
Linux/Windows build-and-test workflow in `.github/workflows/ci.yml`.

The CLI can inspect the runtime without an API key:

```text
ZenoC\build\Release\zenoc_cli.exe --check
ZenoC\build\Release\zenoc_cli.exe --tools
ZenoC\build\Release\zenoc_cli.exe --minimal --tools
ZenoC\build\Release\zenoc_cli.exe --squad session "task" coder,reviewer,tester
ZenoC\build\Release\zenoc_cli.exe --skills-dir skills --squad-ex agents session "task" explorer,implementer
```

When `OPENAI_API_KEY`, `FIREWORKS_API_KEY` or `USE_OLLAMA` are configured the
CLI builds a router with failover automatically; `--run` and `--squad` go
through it.

## Design rules

- C11, no global mutable singleton is required by the public API.
- Every returned string is heap-owned by the caller and released with
  `zeno_free` or the matching result destructor.
- Tool effects are explicit: local read/write, external read/write, process and
  browser. Approval is decided by effect, not by model text.
- All child processes go through the sandbox and are started without
  `system()`; shell operators are disabled by default and the shell is used only
  after policy validation because the Zeno tool contract deliberately accepts
  command strings. POSIX process groups are cleaned up on timeout/shutdown.
- Durable state uses the same fenced-JSON Markdown format as the TypeScript
  runtime where compatibility is useful. Secrets are read from the environment
  and are redacted from public configuration output.
- `libcurl` is optional. HTTP, MCP, vision and the lightweight browser reader
  activate automatically when it is present; JavaScript browser automation and
  Composio remain documented adapter points. HTTP(S) tools reject
  private/loopback/metadata targets, pin DNS resolutions, disable redirects, and
  cap responses; see [`SECURITY.md`](SECURITY.md) for deployment assumptions.

## Parity with the TypeScript runtime

The core runtime is at parity with `src/` (TS): agent loop, loop detection,
checkpoint/resume, approval by effect, router with failover, cache, traces,
memory with user profiles, transcript, planner and plan execution, streaming,
sandbox with persisted jobs, Git, codebase index, test runner and all
workspace/shell/http/db/assertion tools. Not ported (kept as adapters or out of
scope): the HTTP server + web UI (`app_server.ts`), Playwright browser
automation, GitHub skill installer and the `ci.ts` pipeline. The Composio
bridge is superseded by the general MCP client (Composio is reachable through
it, as is any MCP server).
Background jobs loaded after a restart are marked `failed` (same rule as the
TypeScript sandbox); jobs are also terminated and persisted as `killed` during
runtime shutdown.

## C API

The stable public surface is `include/zeno.h`. It is intentionally small: the
runtime is composed by passing explicit `ZenoRegistry`, `ZenoRouter`,
`ZenoMemory`, `ZenoSandbox`, `ZenoCache`, `ZenoTrace` and `ZenoApproval`
instances to `ZenoAgent`.

The test suite is deterministic and offline. It covers path containment,
sandbox hardening, registry validation, retries, memory/context behavior,
user profiles, cache expiry, trace persistence, planner parsing, tool-call
parsing, streaming fallback, plan execution, job persistence and the agent
approval/loop/checkpoint lifecycle. It also proves concurrency for real:
parallel tool batches and parallel/squad runs assert overlap (transports
executing simultaneously) without relying on wall-clock thresholds, plus the
minimal-mode contract (four tools, tight budgets, precision prompt) and the
squad role-scoping contract (a reviewer cannot write even when the model
asks, MCP-unreachable errors stay well-formed, Composio reports its MCP
bridge). The OpenAI-compatible tool-call history (assistant `tool_calls` plus
per-call `tool_call_id` tool messages) is validated live against DeepSeek's
strict endpoint.
