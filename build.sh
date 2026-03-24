#!/bin/bash
set -e

IMAGE_NAME="guitar-ncs"
CONTAINER_WORK="/workspace"

usage() {
    echo "Usage: $0 <command> [mode]"
    echo ""
    echo "Commands:"
    echo "  setup     Build Docker image with NCS SDK (~20 min first time)"
    echo "  tx        Build TX firmware (guitar)"
    echo "  rx        Build RX firmware (dongle)"
    echo "  all       Build both TX and RX"
    echo "  clean     Remove build directories"
    echo "  shell     Open shell inside build container"
    echo ""
    echo "Mode (optional):"
    echo "  debug     Include serial console + logging (default)"
    echo "  release   Strip logging, min latency, max battery life"
    echo ""
    echo "Examples:"
    echo "  $0 tx              # debug build"
    echo "  $0 tx release      # production build"
    echo "  $0 all release     # both in production mode"
    echo ""
}

docker_run() {
    docker run --rm \
        -v "$(pwd):${CONTAINER_WORK}" \
        -w "${CONTAINER_WORK}" \
        "${IMAGE_NAME}" \
        "$@"
}

cmd_setup() {
    echo "==> Building Docker image (this will take a while on first run)..."
    docker build -t "${IMAGE_NAME}" .
    echo "==> Done. Image size:"
    docker images "${IMAGE_NAME}" --format "{{.Size}}"
}

cmd_build() {
    local target="$1"
    local mode="${2:-debug}"
    local board="${BOARD:-promicro_nrf52840/nrf52840/uf2}"
    local overlay_arg=""

    if [ -f "${target}/${mode}.conf" ]; then
        overlay_arg="-DEXTRA_CONF_FILE=${CONTAINER_WORK}/${target}/${mode}.conf"
    fi

    mkdir -p "build/${target}"
    echo "==> Building ${target} [${mode}] for board ${board}"
    docker_run bash -c "
        cd ${CONTAINER_WORK}/${target} && \
        west build -b ${board} --no-sysbuild --build-dir ${CONTAINER_WORK}/build/${target} . -- ${overlay_arg} && \
        echo '==> Build artifacts:' && \
        ls -lh ${CONTAINER_WORK}/build/${target}/zephyr/zephyr.{hex,bin,uf2} 2>/dev/null || true
    "
}

cmd_clean() {
    echo "==> Removing build directories"
    rm -rf build/
}

cmd_shell() {
    docker run --rm -it \
        -v "$(pwd):${CONTAINER_WORK}" \
        -w "${CONTAINER_WORK}" \
        "${IMAGE_NAME}" \
        /bin/bash
}

case "${1:-}" in
    setup)  cmd_setup ;;
    tx)     cmd_build tx "${2:-debug}" ;;
    rx)     cmd_build rx "${2:-debug}" ;;
    all)    cmd_build tx "${2:-debug}" && cmd_build rx "${2:-debug}" ;;
    clean)  cmd_clean ;;
    shell)  cmd_shell ;;
    *)      usage; exit 1 ;;
esac
