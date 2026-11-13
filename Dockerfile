# CLOCK (CRG-Linked Officials' Clock Kit) install and update site: ESP Web Tools page, the firmware
# (built here with PlatformIO), manifest.json and latest.json, on nginx.
# Build: docker build --build-arg VERSION=0.1.0 -t crg-timer-site .
# The firmware is the same for every architecture: only the nginx stage is
# multi-arch (amd64, arm64).
FROM --platform=$BUILDPLATFORM python:3.12-slim AS build
ARG VERSION=dev
RUN apt-get update && apt-get install -y --no-install-recommends git && rm -rf /var/lib/apt/lists/* \
 && pip install --no-cache-dir platformio==6.2.0
WORKDIR /src
COPY firmware firmware
COPY web-installer web-installer
RUN cd firmware && PLATFORMIO_BUILD_FLAGS="-DFW_VERSION=\\\"${VERSION}\\\"" pio run -e c6 \
 && mkdir -p /out && cp .pio/build/c6/firmware.bin .pio/build/c6/firmware.factory.bin /out/ \
 && cp ../web-installer/index.html /out/ && python ../web-installer/make_release.py "$VERSION" /out

FROM nginxinc/nginx-unprivileged:1.27.4-alpine
COPY --from=build /out /usr/share/nginx/html
EXPOSE 8080
