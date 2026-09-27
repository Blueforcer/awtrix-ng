#!/bin/sh
# Run from the playground checkout after building native_sim. No host ports exposed.
set -eu

# Reuse this entry point inside the ephemeral build container, without inline shell code.
if [ "${1:-}" = "--inside-container" ]; then
    python -m pip install --disable-pip-version-check paho-mqtt==2.1.0
    exec python test/integration/mqtt_smoke.py --broker mqtt-smoke-broker
fi

cd "$(dirname "$0")/../.."
test -x .pio/build/native_sim/program
network_created=false
broker_started=false
cleanup() {
    status=$?
    trap - EXIT
    if [ "$broker_started" = true ]; then
        podman stop mqtt-smoke-broker || :
    fi
    if [ "$network_created" = true ]; then
        podman network rm mqtt-smoke || :
    fi
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

podman network create mqtt-smoke
network_created=true
podman run --detach --rm --name mqtt-smoke-broker --network mqtt-smoke \
    -v "$PWD/test/integration/mosquitto.conf:/mosquitto/config/mosquitto.conf:ro,Z" \
    docker.io/library/eclipse-mosquitto:2
broker_started=true

# Probe readiness instead of racing the listener startup.
attempt=0
until podman exec mqtt-smoke-broker mosquitto_pub -h 127.0.0.1 -t smoke/ready -m ready; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 30 ]; then
        podman logs mqtt-smoke-broker
        exit 1
    fi
    sleep 1
done

podman run --rm --network mqtt-smoke \
    -v pio-home:/root/.platformio -v "$PWD:/w:Z" -w /w \
    localhost/awtrix-build sh test/integration/run_mqtt_smoke.sh --inside-container
