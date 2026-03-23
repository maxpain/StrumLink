FROM ubuntu:26.04

ARG NCS_VERSION=v3.2.4

ENV DEBIAN_FRONTEND=noninteractive

# System dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    git cmake ninja-build gperf ccache dfu-util device-tree-compiler \
    python3-dev python3-pip python3-venv python3-setuptools python3-wheel \
    wget xz-utils file make gcc g++ libffi-dev libssl-dev \
    pkg-config libhidapi-dev libudev-dev libusb-1.0-0-dev \
    && rm -rf /var/lib/apt/lists/*

# West build tool
RUN pip3 install --no-cache-dir --break-system-packages west

# Initialize NCS workspace
RUN west init -m https://github.com/nrfconnect/sdk-nrf --mr ${NCS_VERSION} /ncs
WORKDIR /ncs
RUN west update --narrow -o=--depth=1 && \
    west zephyr-export

# Python requirements from NCS
RUN pip3 install --no-cache-dir --break-system-packages \
    -r /ncs/zephyr/scripts/requirements.txt \
    -r /ncs/nrf/scripts/requirements.txt \
    -r /ncs/bootloader/mcuboot/scripts/requirements.txt

# Zephyr SDK (ARM toolchain)
ARG TARGETARCH
ARG ZEPHYR_SDK_VERSION=0.17.0
RUN if [ "$TARGETARCH" = "arm64" ] || [ "$(uname -m)" = "aarch64" ]; then \
      SDK_ARCH="aarch64"; \
    else \
      SDK_ARCH="x86_64"; \
    fi && \
    wget -q "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-${SDK_ARCH}_minimal.tar.xz" && \
    tar xf zephyr-sdk-*.tar.xz -C /opt/ && \
    rm zephyr-sdk-*.tar.xz && \
    /opt/zephyr-sdk-${ZEPHYR_SDK_VERSION}/setup.sh -t arm-zephyr-eabi -c

ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk-0.17.0
ENV ZEPHYR_BASE=/ncs/zephyr

WORKDIR /workspace
