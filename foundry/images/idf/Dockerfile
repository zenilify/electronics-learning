# kaizen-foundry/idf — ESP-IDF 6.0.3 (same as sticks3-bridge on estudio) + microlink + QEMU
FROM espressif/idf:v6.0.3@sha256:01ab23e20744721378d1dd9d0fff4fe15e2edd8bd5cdaf02d24597f4ade05a1e
ARG MICROLINK_REPO=https://github.com/RomuloGatto/microlink.git
ARG MICROLINK_REV=da157ac17f1b7ea61c0ac298b93f4b5937cc1c59
RUN apt-get update && apt-get install -y --no-install-recommends \
      libgcrypt20 libglib2.0-0 libpixman-1-0 libsdl2-2.0-0 libslirp0 \
    && rm -rf /var/lib/apt/lists/*
RUN git init -q /opt/microlink && git -C /opt/microlink fetch -q --depth 1 "$MICROLINK_REPO" "$MICROLINK_REV" \
    && git -C /opt/microlink checkout -q FETCH_HEAD && echo "$MICROLINK_REV" > /opt/microlink/.foundry-rev
RUN python3 "$IDF_PATH/tools/idf_tools.py" install qemu-xtensa \
    && bash -c '. $IDF_PATH/export.sh >/dev/null && qemu-system-xtensa --version | head -1'
LABEL org.opencontainers.image.title="kaizen-foundry/idf" \
      io.zenilify.foundry.pins="esp-idf=v6.0.3 microlink=da157ac qemu-xtensa=idf_tools-default"
