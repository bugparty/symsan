# Build steps:
#   1) Build the symsan builder image from repo root: docker build -t symsan-builder .
#   2) Build this image using the builder artifacts:
#      docker build -t symsan-web-service -f web-service/Dockerfile . --build-arg BUILDER_IMAGE=symsan-builder

ARG BUILDER_IMAGE=symsan-builder
FROM ${BUILDER_IMAGE} AS symsan-builder
WORKDIR /work/symsan

FROM ubuntu:noble

ENV DEBIAN_FRONTEND=noninteractive \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    FGTEST_PATH=/appdata/bin/fgtest \
    FGTEST_BIN_DIR=/appdata/bin \
    PATH="/opt/venv/bin:/appdata/bin:${PATH}"

WORKDIR /app

RUN apt-get update && \
    apt-get install -y python3 python3-pip python3-venv \
    libz3-dev libgoogle-perftools-dev libboost-container-dev libprotobuf-dev \
    libc++-14-dev libc++abi-14-dev libunwind-14-dev && \
    rm -rf /var/lib/apt/lists/*

COPY web-service/requirements.txt .
RUN python3 -m venv /opt/venv && \
    /opt/venv/bin/pip install --no-cache-dir -r requirements.txt

RUN mkdir -p /appdata/bin
COPY --from=symsan-builder /work/symsan/build/bin/fgtest /appdata/bin/fgtest
COPY --from=symsan-builder /work/symsan/examples/dummy /appdata/bin/dummy
COPY --from=symsan-builder /work/symsan/examples/xor /appdata/bin/xor
COPY --from=symsan-builder /work/symsan/examples/control_temp /appdata/bin/control_temp
COPY --from=symsan-builder /work/symsan/examples/*ctwm_index.json /appdata/bin/

COPY web-service/app.py web-service/fgtest_wrapper.py ./

# Ensure runtime directories exist
RUN mkdir -p uploads results

EXPOSE 8000
ENTRYPOINT ["uvicorn", "app:app", "--host", "0.0.0.0", "--port", "8000"]
