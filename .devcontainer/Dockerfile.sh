#!/bin/bash

# Update and upgrade packages
sudo apt-get update -y
sudo apt-get upgrade -y

# Install basic utilities
sudo apt-get install -y curl git

# Install C++ environment and SDL2 dependencies
sudo apt-get install -y \
    gcc \
    g++ \
    gcc-multilib \
    g++-multilib \
    build-essential \
    xutils-dev \
    libsdl2-dev \
    libsdl2-gfx-dev \
    libsdl2-image-dev \
    libsdl2-mixer-dev \
    libsdl2-net-dev \
    libsdl2-ttf-dev \
    libreadline6-dev \
    libncurses5-dev \
    mingw-w64 \
    cmake

# Install Ninja build system
sudo apt-get install -y ninja-build

# Install Lua and related libraries
sudo apt-get install -y lua5.4 liblua5.4-dev

# Install Clang compilers and debugging tools
sudo apt-get install -y clang gdb libstdc++6-11-dbg

# Install additional utilities for clang
sudo apt-get install -y clang-format clangd lldb

# Install GLEW for OpenGL (VAOs, Shaders, etc.)
sudo apt-get install -y libglew-dev

# Install PlantUML dependencies
sudo apt-get install -y openjdk-17-jdk graphviz

# Install Boost libraries
sudo apt-get install -y libboost-all-dev

# Install lld (Linker)
sudo apt-get install -y lld

# Install ccache and configure it
# sudo apt-get install -y ccache
# sudo update-ccache-symlinks
# ccache --set-config=cache_dir=/ccache --max-size=8G

# # Add directories to PATH
# export PATH="$PATH:$WORKSPACE_DIR/tools/tasks"
# export PATH="$PATH:$WORKSPACE_DIR/tools/programs"

# End of script
echo "Setup complete!"
