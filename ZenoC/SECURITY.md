# Security model

ZenoC is an embeddable runtime, not a complete operating-system sandbox. Applications
must run it with a least-privilege account and a workspace owned by that account.

## Runtime guarantees

- Workspace operations reject parent traversal, resolve existing parents, and do not
  follow POSIX symlinks or Windows reparse points while walking. Writes use a temporary
  file plus an atomic replacement.
- Process, external-write, browser, and explicitly approval-gated tools require a
  runtime approval callback. A tool declaration cannot be downgraded by a permissive
  run option.
- HTTP tools accept only HTTP(S) URLs, reject credentials and common loopback/private/
  link-local/metadata destinations, disable redirects, and cap response sizes. DNS
  resolution is checked and pinned for public HTTP(S) requests when libcurl is enabled.
  Network access is still subject to the host application's egress policy.
- Shell operators are disabled by default. Commands are checked by the sandbox, output
  and runtime are bounded, and POSIX child process groups are terminated on timeout or
  shutdown. This is defense in depth, not a substitute for OS isolation.
- Tool arguments and streamed responses have finite size limits; JSON schemas validate
  required fields and primitive types before handlers run.

## Deployment requirements

Use a dedicated service account, deny unnecessary network egress, keep libcurl/TLS and
the compiler toolchain current, and configure an explicit executable allowlist when
running model-controlled commands. Do not expose the raw registry execution API to an
untrusted caller; route model actions through `ZenoAgent` so approval and effect policy
are applied.

`absolute_mode` is an explicit trusted-host escape hatch for effect-based approval;
never derive it from model/user input or expose it in a multi-tenant service. The
registered `requires_approval` capability remains a hard floor even in permissive
run options.

Report vulnerabilities privately to the project maintainers with a reproducible case,
impact, affected platform, and build configuration. Do not include live API keys or
other secrets in reports.
