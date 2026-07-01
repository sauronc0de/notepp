# syntax=docker/dockerfile:1.6

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        git \
        gh \
        gnupg \
        sudo \
        nano \
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
        libreadline-dev \
        libncurses-dev \
        libgtk-3-dev \
        mingw-w64 \
        cmake \
        ninja-build \
        clang \
        gdb \
        clang-format \
        clangd \
        lldb \
        libglew-dev \
        openjdk-17-jdk \
        graphviz \
        libboost-all-dev \
        lld \
        ccache && \
    rm -rf /var/lib/apt/lists/*

###############################################################################
# Node.js 22
###############################################################################
RUN mkdir -p /etc/apt/keyrings && \
    curl -fsSL https://deb.nodesource.com/gpgkey/nodesource-repo.gpg.key \
        | gpg --dearmor -o /etc/apt/keyrings/nodesource.gpg && \
    echo "deb [signed-by=/etc/apt/keyrings/nodesource.gpg] https://deb.nodesource.com/node_22.x nodistro main" \
        > /etc/apt/sources.list.d/nodesource.list && \
    apt-get update && \
    apt-get install -y --no-install-recommends nodejs && \
    node --version && \
    npm --version && \
    rm -rf /var/lib/apt/lists/*

RUN update-ccache-symlinks && \
    ccache --set-config=cache_dir=/ccache --max-size=8G

RUN useradd -m sauroncode && \
    echo "sauroncode:1234" | chpasswd && \
    adduser sauroncode sudo && \
    usermod -aG plugdev,dialout sauroncode && \
    echo "%sudo ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

USER sauroncode

ENV HOME=/home/sauroncode
ENV XDG_RUNTIME_DIR=/tmp

ARG WORKSPACE_DIR
ENV WORKSPACE_DIR=${WORKSPACE_DIR}

ARG PROJECT_NAME
ENV PROJECT_NAME=${PROJECT_NAME}

WORKDIR ${WORKSPACE_DIR}

ENV PATH="${PATH}:${WORKSPACE_DIR}/tools/tasks:${WORKSPACE_DIR}/tools/programs"
ENV PATH="${HOME}/.npm-global/bin:${PATH}"

RUN npm config set prefix "${HOME}/.npm-global"

###############################################################################
# Pi AI tooling
###############################################################################
RUN set -eux; \
    npm install -g @earendil-works/pi-coding-agent; \
    pi install npm:pi-lmstudio || true; \
    pi install npm:pi-simplify || true; \
    pi install npm:pi-web-access || true; \
    pi install npm:pi-rpiv-ask-user-question || true; \
    pi install npm:pi-juicesharp/rpiv-todo || true; \
    pi install npm:pi-subagents || true; \
    pi install npm:@sinamtz/pi-minimax-provider || true