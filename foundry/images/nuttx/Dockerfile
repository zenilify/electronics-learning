# kaizen-foundry/nuttx — RISC-V toolchain for NuttX on ESP32-P4 (same GCC as NuttX CI)
FROM ubuntu:24.04
ARG XPACK=14.2.0-3
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential git curl ca-certificates xz-utils python3 python3-venv \
      bison flex gperf libncurses-dev genromfs xxd cmake ninja-build unzip file pkg-config autoconf automake libtool \
    && rm -rf /var/lib/apt/lists/*
RUN mkdir -p /opt/riscv-none-elf-gcc && curl -fsSL \
      "https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v${XPACK}/xpack-riscv-none-elf-gcc-${XPACK}-linux-x64.tar.gz" \
    | tar -C /opt/riscv-none-elf-gcc --strip-components 1 -xz
# kconfig-frontends (kconfig-tweak/-conf/-mconf), built exactly like NuttX CI does
ARG NUTTX_TOOLS=9ad3e1ee75c7d39bfee8019ce44f4fd035d89584
RUN mkdir -p /tmp/nt && curl -fsSL "https://github.com/patacongo/tools/archive/${NUTTX_TOOLS}.tar.gz" \
      | tar -C /tmp/nt --strip-components=1 -xz \
    && cd /tmp/nt/kconfig-frontends \
    && ./configure --enable-mconf --disable-gconf --disable-qconf --enable-static --prefix=/opt/kconfig-frontends \
    && make -j"$(nproc)" install && rm -rf /tmp/nt
RUN python3 -m venv /opt/venv && /opt/venv/bin/pip install --no-cache-dir \
      "kconfiglib==14.1.0" "esptool==4.8.1" pyelftools
ENV PATH=/opt/venv/bin:/opt/riscv-none-elf-gcc/bin:/opt/kconfig-frontends/bin:$PATH
RUN riscv-none-elf-gcc --version | head -1 && esptool.py version | tail -1 && command -v kconfig-tweak kconfig-conf
LABEL org.opencontainers.image.title="kaizen-foundry/nuttx" \
      io.zenilify.foundry.pins="xpack-riscv-none-elf-gcc=14.2.0-3 kconfiglib=14.1.0 esptool=4.8.1 nuttx-tools=9ad3e1e"
