# Build host for the pinned ARM userspace build and its emulated contracts.
# Package indexes and packages come from snapshot.ubuntu.com at the single
# timestamp recorded in ubuntu.sources; apt also downloads the live index files
# but selects candidates only from the snapshot. host-packages.lock records the
# resulting dpkg inventory, and reproduce.py refuses an image that differs.

# The base image carries no CA bundle and the snapshot service is HTTPS-only,
# so a trust stage installs ca-certificates and the openssl packages it pulls in
# from the live archive over plain HTTP (signatures checked by apt) and
# contributes only the bundle. The final stage replaces it with the snapshot's
# own ca-certificates package.
FROM ubuntu:24.04@sha256:786a8b558f7be160c6c8c4a54f9a57274f3b4fb1491cf65146521ae77ff1dc54 AS trust
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates \
    && rm -rf /var/lib/apt/lists/*

FROM ubuntu:24.04@sha256:786a8b558f7be160c6c8c4a54f9a57274f3b4fb1491cf65146521ae77ff1dc54
ENV DEBIAN_FRONTEND=noninteractive
COPY --from=trust /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/ca-certificates.crt
COPY ubuntu.sources /etc/apt/sources.list.d/ubuntu.sources
RUN chmod 0644 /etc/apt/sources.list.d/ubuntu.sources && apt-get update && apt-get install -y --no-install-recommends \
    build-essential git python3 rsync patch bc bison flex file unzip cpio \
    wget xz-utils ca-certificates cmake libssl-dev openssl qemu-user mosquitto \
    && rm -rf /var/lib/apt/lists/*
# Ubuntu's base image supplies uid 1000. No privileged build or device access.
USER 1000:1000
ENV LC_ALL=C TZ=UTC
WORKDIR /work
ENTRYPOINT ["python3", "/runner/tools/system/build.py"]
