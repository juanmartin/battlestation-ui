#!/usr/bin/env bash
#
# Point this clone at the repository's versioned hooks.
# Run once per clone: tools/install_git_hooks.sh
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

chmod +x .githooks/pre-commit .githooks/pre-push tools/ci.sh
git config core.hooksPath .githooks

echo "installed git hooks from .githooks"
echo "  pre-commit: tools/ci.sh --quick"
echo "  pre-push:   tools/ci.sh"
echo "bypass with --no-verify when needed"
