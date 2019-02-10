VERSION=$(cat src/EspIdiot.ino | grep 'define VERSION' | awk '{print $3}')
VERSION="${VERSION%\"}"
VERSION="${VERSION#\"}"

if [[ -z "$VERSION" ]]; then
    echo "src/EspIdiot.ino not found. Exiting."
    exit 1
fi

BIN_FILE=/www/zelenik/firmware/$VERSION.bin

if scp -o "StrictHostKeyChecking no" -P 8902 shiptechnic@otselo.eu:$BIN_FILE /tmp/ >&/dev/null; then 
    echo "$BIN_FILE already exists on remote host. Exiting."
    exit 1
fi

echo "Pushing prod version $VERSION..."

pio run -e esp12e && \
    cp .pioenvs/esp12e/firmware.bin $BIN_FILE 
    scp -o "StrictHostKeyChecking no" -P 8902 .pioenvs/esp12e/firmware.bin shiptechnic@otselo.eu:$BIN_FILE && \
    echo "Done."
