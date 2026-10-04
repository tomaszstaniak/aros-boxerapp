#!/bin/bash
# Runs every patch-tooling acceptance scenario on disposable fixtures.
exec python3 -W ignore::ResourceWarning "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/test_patchtool.py" "$@"
