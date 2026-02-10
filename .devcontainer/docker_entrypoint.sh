#!/bin/bash

# Print enter on entrypoint bash script
echo "✅ Entering the docker entrypoint script"

# Check user
echo "👤 Current user: $(whoami)"

# Apply the correct permissions to the workspace directory
echo "🔐 Applying permissions to the: $WORKSPACE_DIR" 

# Set all files with the docker user as the owner
sudo chown --no-dereference -R $(whoami):$(whoami) $WORKSPACE_DIR

# Set executable permissions to all files within a relative path
sudo chmod -R +x ./tools/tasks

# Cache config
sudo chown -R $(whoami) /ccache
ccache -M 8G
export CCACHE_DIR=/ccache
export CCACHE_BASEDIR=$WORKSPACE_DIR
export CCACHE_COMPRESS=1
export CCACHE_MAXSIZE=8G
export CCACHE_SLOPPINESS=time_macros,include_file_mtime,include_file_ctime

# Update the git submodules
# git submodule update --init --recursive

export SDL_VIDEODRIVER=x11