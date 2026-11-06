# kaizen-foundry/arduino — arduino-cli + Seeed SAMD (Wio Terminal) + PlatformIO, versions = macbook 2026-11-06
FROM python:3.12-slim-bookworm
ARG ACLI=1.5.1
ARG SEEED_SAMD=1.8.6
ARG PIO=6.1.19
RUN apt-get update && apt-get install -y --no-install-recommends curl ca-certificates git \
    && rm -rf /var/lib/apt/lists/*
RUN curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=/usr/local/bin sh -s "$ACLI" \
    && arduino-cli version
RUN arduino-cli config init \
    && arduino-cli config set board_manager.additional_urls \
         https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json \
         https://espressif.github.io/arduino-esp32/package_esp32_index.json \
    && arduino-cli core update-index \
    && arduino-cli core install "Seeeduino:samd@${SEEED_SAMD}" \
    && arduino-cli lib install "ArduinoJson@7.4.3" "FlashStorage_SAMD@1.3.2" "Seeed Arduino FS@2.1.5" \
         "Seeed Arduino rpcUnified@2.1.4" "Seeed Arduino rpcWiFi@1.1.0" "Seeed Arduino SFUD@2.0.2" \
         "Seeed_Arduino_mbedtls@3.0.2" \
    && arduino-cli core list && arduino-cli lib list
RUN pip install --no-cache-dir "platformio==${PIO}" && pio --version
LABEL org.opencontainers.image.title="kaizen-foundry/arduino" \
      io.zenilify.foundry.pins="arduino-cli=1.5.1 Seeeduino:samd=1.8.6 platformio=6.1.19"
