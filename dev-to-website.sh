VERSION=$(cat src/EspIdiot.ino | grep 'define VERSION' | awk '{print $3}')
VERSION="${VERSION%\"}"
VERSION="${VERSION#\"}"

if [[ -z "$VERSION" ]]; then
    echo "src/EspIdiot.ino not found. Exiting."
    exit 1
fi

BIN_FILE=/www/zelenik/firmware/$VERSION.bin
if [ -f $BIN_FILE ]; then
    echo "$BIN_FILE already exists. Exiting."
    exit 1
fi

echo "Pushing dev version $VERSION..."

PLATFORMIO_BUILD_FLAGS=-DDEV pio run && \
    cp .pioenvs/esp12e/firmware.bin $BIN_FILE && echo "Done."
