#!/usr/bin/env bash
set -euo pipefail

# Build the web-service image along with its required builder image.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WEB_DIR="${ROOT_DIR}/web-service"

BUILDER_IMAGE="${BUILDER_IMAGE:-symsan-builder}"
WEB_IMAGE="${WEB_IMAGE:-symsan-web-service}"
BUILDX_BUILDER="${BUILDX_BUILDER:-}"
BUILDX_PROGRESS="${BUILDX_PROGRESS:-auto}"

buildx() {
  local args=(
    buildx build
    --progress="${BUILDX_PROGRESS}"
    --load
  )
  if [[ -n "${BUILDX_BUILDER}" ]]; then
    args+=(--builder "${BUILDX_BUILDER}")
  fi
  docker "${args[@]}" "$@"
}

echo "==> Building builder image: ${BUILDER_IMAGE}"
buildx \
  -t "${BUILDER_IMAGE}" \
  "${ROOT_DIR}"

echo "==> Building web-service image: ${WEB_IMAGE}"
buildx \
  -t "${WEB_IMAGE}" \
  -f "${WEB_DIR}/Dockerfile" \
  --build-arg "BUILDER_IMAGE=${BUILDER_IMAGE}" \
  "${ROOT_DIR}"

echo "==> Done. Images built:"
docker images "${WEB_IMAGE}"
docker images "${BUILDER_IMAGE}"
