#!/bin/bash
set -euo pipefail

# Print enter on entrypoint bash script
echo "✅ Entering the docker entrypoint script"

# Check user
echo "👤 Current user: $(whoami)"

# Apply the correct permissions to the workspace directory
echo "🔐 Applying permissions to the: $WORKSPACE_DIR" 

# Set all files with the docker user as the owner
sudo chown --no-dereference -R "$(whoami):$(whoami)" "$WORKSPACE_DIR"

# Set executable permissions to all files within a relative path
if [ -d ./tools/tasks ]; then
    sudo chmod -R +x ./tools/tasks
fi

# Cache config
sudo mkdir -p /ccache
sudo chown -R "$(whoami):$(whoami)" /ccache
ccache -M 8G
export CCACHE_DIR=/ccache
export CCACHE_BASEDIR=$WORKSPACE_DIR
export CCACHE_COMPRESS=1
export CCACHE_MAXSIZE=8G
export CCACHE_SLOPPINESS=time_macros,include_file_mtime,include_file_ctime

# Install/update pi packages after /home/devuser/.pi has been mounted by the
# devcontainer. Installing them in the Dockerfile would be hidden by the bind
# mount from devcontainer.json.
echo "🤖 Installing pi packages/plugins"
pi install npm:pi-lmstudio
pi install npm:pi-simplify
pi install npm:pi-web-access
pi install npm:@juicesharp/rpiv-ask-user-question
pi install npm:@juicesharp/rpiv-todo
pi install npm:pi-subagents
pi install npm:pi-intercom
pi install npm:pi-prompt-template-model
pi install npm:@sinamtz/pi-minimax-provider
pi update
pi update --extensions

# Update the git submodules
# git submodule update --init --recursive

export SDL_VIDEODRIVER=x11