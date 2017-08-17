VERSION=$(cat src/EspIdiot.ino | grep 'define VERSION' | awk '{print $3}')
VERSION="${VERSION%\"}"
VERSION="${VERSION#\"}"

if [[ -z "$VERSION" ]]; then
    echo "src/EspIdiot.ino not found. Exiting."
    exit 1
fi

if [[ -z "$1" ]]; then
    echo "Using default environment zelenik2"
    ENVIRONMENT="zelenik2"
else 
    echo "Using environment $1"
    ENVIRONMENT="$1"
fi

BIN_FILE=/www/zelenik/firmware/$VERSION.bin

if scp -P 8902 shiptechnic@otselo.eu:$BIN_FILE /tmp/ >&/dev/null; then 
    echo "$BIN_FILE already exists on remote host. Exiting."
    exit 1
fi

echo "Pushing prod version $VERSION..."

pio run -e $ENVIRONMENT && \
    cp .pioenvs/$ENVIRONMENT/firmware.bin $BIN_FILE 
    scp -P 8902 .pioenvs/$ENVIRONMENT/firmware.bin shiptechnic@otselo.eu:$BIN_FILE && \
    echo "Done."
