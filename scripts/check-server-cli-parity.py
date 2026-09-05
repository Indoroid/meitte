#!/usr/bin/env python3
"""Fail when meitte-server stops accepting an applicable meitte-cli option or env override."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
CLI = (ROOT / "cli/main.cpp").read_text(encoding="utf-8")
SERVER = (ROOT / "cli/server_main.cpp").read_text(encoding="utf-8")


def parsed_options(source: str) -> set[str]:
    return set(re.findall(r'a == "(-{1,2}[a-z][a-z0-9-]*)"', source))


# File-backed media flags and perplexity benchmarks are CLI-only. The server accepts the
# equivalent image/audio bytes in OpenAI content parts, while PPL scores a finite file rather
# than serving requests, so neither needs a server spelling.
cli_options = parsed_options(CLI) - {
    "--image", "--audio", "--media", "--ppl", "--ppl-skip", "--ppl-step", "--ppl-list", "--ppl-choices"
}
server_options = parsed_options(SERVER)

# --prompt remains accepted for script compatibility, but its value is intentionally ignored because
# each HTTP request supplies a prompt. --session is an accepted no-op because the server is always
# persistent; --progress retains the CLI stdout telemetry alongside HTTP/SSE responses.
missing_options = sorted(cli_options - server_options)

cli_env = set(re.findall(r'env_int\("(BMOE_[A-Z0-9_]+)"', CLI))
server_env = set(re.findall(r'env_int\("(BMOE_[A-Z0-9_]+)"', SERVER))
missing_env = sorted(cli_env - server_env)

if missing_options or missing_env:
    if missing_options:
        print("meitte-server is missing CLI options: " + ", ".join(missing_options), file=sys.stderr)
    if missing_env:
        print("meitte-server is missing CLI env overrides: " + ", ".join(missing_env), file=sys.stderr)
    raise SystemExit(1)

print(
    f"server CLI parity passed: {len(cli_options)} options, {len(cli_env)} env overrides"
)
