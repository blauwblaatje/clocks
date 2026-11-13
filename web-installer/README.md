# Install and update site

A container with the timer's firmware and its install page, for dc-k3s.

- `index.html`: ESP Web Tools install button (first install and USB reflash,
  Chrome or Edge), link to `firmware.bin` for the setup page upload.
- `manifest.json`: ESP Web Tools, one merged image (`firmware.factory.bin`) at 0.
- `latest.json`: `version`, `app` (firmware.bin), `size`, `sha256`, for the
  timer's "Update" menu item (firmware/lib/update; the device checks
  version, size, image header and SHA-256).

Built by the repository's `Dockerfile`: PlatformIO compiles the firmware
with `FW_VERSION` = the image version, `make_release.py` writes the two JSON
files, nginx-unprivileged serves them on port 8080.

    docker build --build-arg VERSION=0.1.0 -t crg-timer-site .
    docker run --rm -p 8080:8080 crg-timer-site

`.github/workflows/image.yml` (as RTFM's) publishes to GHCR on a `vX.Y.Z` tag.
Will run at clock.quagga.rocks on dc-k3s. Not yet: a GitHub repository
for this project, and the dc-k3s manifests.
