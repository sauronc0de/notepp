#!/bin/bash

# Default to current directory if no argument given
ROOT_DIR="${1:-.}"

# Ensure path is absolute
ROOT_DIR="$(cd "$ROOT_DIR" && pwd)"

echo "Project Tree: $ROOT_DIR"

# Traverse and format
find "$ROOT_DIR" -mindepth 1 | sed "s|^$ROOT_DIR/||" | awk '
BEGIN { FS="/" }
{
    indent = ""
    for (i = 1; i < NF; i++) {
        indent = indent "│   "
    }
    print indent "├── " $NF
}'
