FROM ubuntu:noble

WORKDIR /work

ENV DEBIAN_FRONTEND=noninteractive \
    TZ=Etc/UTC

RUN apt-get update && \
    apt-get install -y cmake llvm-14 clang-14 lldb-14 libc++-14-dev libc++abi-14-dev libunwind-14-dev \
    python3-minimal python-is-python3 zlib1g-dev git joe libprotobuf-dev \
    libz3-dev libgoogle-perftools-dev libboost-container-dev python3-dev && \
    rm -rf /var/lib/apt/lists/*

RUN git clone --depth=1 --branch=v4.31c https://github.com/AFLplusplus/AFLplusplus /work/aflpp && \
    cd /work/aflpp && make PERFORMANCE=1 LLVM_CONFIG=llvm-config-14 NO_NYX=1 source-only -j `nproc` && make install

COPY . /work/symsan

ENV KO_CC=clang-14 \
    KO_CXX=clang++-14 \
    KO_USE_FASTGEN=1

RUN cd /work/symsan/ && mkdir -p build && \
    cd build && CC=clang-14 CXX=clang++-14 cmake -DCMAKE_INSTALL_PREFIX=. -DAFLPP_PATH=/work/aflpp -DCMAKE_BUILD_TYPE=Debug ../  && \
    make -j `nproc` && make install

# Build example binaries (dummy, xor) so downstream images can copy them directly.
RUN cd /work/symsan/examples && \
    chmod +x dummy_build.sh xor_build.sh control_temp_build.sh && \
    ./dummy_build.sh && \
    ./control_temp_build.sh && \
    ./xor_build.sh
