FROM ubuntu:noble

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

WORKDIR /work

# Install base dependencies and development tools
RUN apt-get update && apt-get install -y \
    cmake \
    llvm-14 \
    clang-14 \
    lldb-14 \
    libc++-14-dev \
    libc++abi-14-dev \
    libunwind-14-dev \
    python3-minimal \
    python-is-python3 \
    zlib1g-dev \
    git \
    joe \
    libprotobuf-dev \
    libz3-dev \
    libgoogle-perftools-dev \
    libboost-container-dev \
    python3-dev \
    gdb \
    valgrind \
    vim \
    nano \
    htop \
    strace \
    ltrace \
    curl \
    wget \
    sudo \
    build-essential \
    && apt clean && rm -rf /var/lib/apt/lists/*


RUN git clone --depth=1 --branch=v4.31c https://github.com/AFLplusplus/AFLplusplus /work/aflpp && \
    cd /work/aflpp && \
    make PERFORMANCE=1 LLVM_CONFIG=llvm-config-14 NO_NYX=1 source-only -j `nproc` && \
    make install && \
    cd /work && rm -rf /work/aflpp/.git


ENV KO_CC=clang-14
ENV KO_CXX=clang++-14
ENV KO_USE_FASTGEN=1

# Create non-root user vscode
ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=$USER_UID

RUN (groupadd --gid $USER_GID $USERNAME 2>/dev/null || true) \
    && (useradd --uid $USER_UID --gid $USER_GID -m $USERNAME 2>/dev/null || usermod -l $USERNAME ubuntu) \
    && echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
    && chmod 0440 /etc/sudoers.d/$USERNAME

# Switch to vscode user
USER $USERNAME

WORKDIR /workspaces/symsan
