FROM ubuntu:22.04
RUN apt-get update -y
RUN apt-get upgrade -y
RUN apt-get install -y curl git

# C++ environment and SDL2
RUN apt-get install -y \
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

# Add Ninja
RUN apt-get install -y ninja-build

# Add Lua
RUN apt-get install -y lua5.4 liblua5.4-dev

# Add clang compilers 
RUN apt-get install -y clang gdb

# Utilities
RUN apt-get install -y clang-format clangd lldb

# Install glew to work with OpenGL with VAOs, Shaders, ...
RUN apt-get install -y libglew-dev

# Plant to UML requirements
RUN apt-get install -y openjdk-17-jdk graphviz

# Install boost libraries
RUN apt-get install -y libboost-all-dev

# Install lld (Something to build link faster)
RUN apt-get install -y lld

# syntax=docker/dockerfile:1.6
RUN apt-get update && apt-get install -y ccache
RUN update-ccache-symlinks && ccache --set-config=cache_dir=/ccache --max-size=8G

# Install sudo and create a user
RUN apt-get install -y sudo
#👥 Create a non-root user 'sauroncode' with password '1234', grant sudo access without password
RUN useradd -m sauroncode && echo "sauroncode:1234" | chpasswd && adduser sauroncode sudo
RUN echo "%sudo ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

# Add user 'sauroncode' to plugdev and dialout groups for USB access
RUN sudo usermod -aG plugdev sauroncode && sudo usermod -aG dialout sauroncode

# Switch to 'sauroncode' user
USER sauroncode

# Set the working directory in the container
ARG WORKSPACE_DIR
ENV WORKSPACE_DIR=${WORKSPACE_DIR}

ARG PROJECT_NAME
ENV PROJECT_NAME=${PROJECT_NAME}
WORKDIR ${WORKSPACE_DIR}

# Add directories on PATH variable
ENV PATH="$PATH:$WORKSPACE_DIR/tools/tasks"
ENV PATH="$PATH:$WORKSPACE_DIR/tools/programs"

ENV XDG_RUNTIME_DIR=/tmp