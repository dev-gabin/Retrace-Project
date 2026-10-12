#!/bin/sh
# Run from any directory. Configuration is a trusted, local shell env file.
set -eu
PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ENV_FILE=${RETRACE_ENV_FILE:-"$PROJECT_DIR/.local/db.env"}
if [ -f "$ENV_FILE" ]; then
    set -a
    . "$ENV_FILE"
    set +a
fi
export RETRACE_DATA_DIR=${RETRACE_DATA_DIR:-"$PROJECT_DIR/data"}
exec "$PROJECT_DIR/build/retrace-storage" "$@"
