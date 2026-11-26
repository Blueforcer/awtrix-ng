# The AWTRIX script host: awtrix-host and awtrix-linux in one image. Run it with the host's network
# (network_mode: host) so clocks find it and reach its UDP ports. Build from the repository root:
#   docker build -f packaging/host/Dockerfile -t awtrix-host .
FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git ca-certificates python3 \
    libssl-dev libjpeg-turbo8-dev liblzma-dev \
    && rm -rf /var/lib/apt/lists/*
# densaugeo/base64 1.4.0, the header PlatformIO installs for the native build.
RUN git clone --quiet https://github.com/Densaugeo/base64_arduino /deps/base64 \
    && git -C /deps/base64 checkout --quiet 574949c67f25d500c204ffc12ab80d576d5bd6f2
WORKDIR /src
COPY . .
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DAWTRIX_DEPS=/deps \
    && cmake --build /build --target awtrix-linux awtrix-host

FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3t64 libjpeg-turbo8 liblzma5 ca-certificates tzdata \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --user-group --home-dir /data --no-create-home --shell /usr/sbin/nologin awtrix \
    && install -d -m 0700 -o awtrix -g awtrix /data
COPY --from=build /build/awtrix-linux /build/awtrix-host /usr/local/bin/
COPY --from=build /src/webui/index.html /usr/local/share/awtrix-ng/index.html
VOLUME /data
USER awtrix
EXPOSE 8300
STOPSIGNAL SIGTERM
ENTRYPOINT ["awtrix-host", "--state", "/data", "--binary", "/usr/local/bin/awtrix-linux", \
            "--webui", "/usr/local/share/awtrix-ng/index.html"]
