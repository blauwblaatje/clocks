# Install and update site

A container with the timer's firmware and its install page, for dc-k3s.

One folder per board, named by its board id (firmware/lib/update,
`upd::Board`): `waveshare-c6/`, `m5-stopwatch/`, each with
`firmware.bin` (updates, setup page upload), `firmware.factory.bin` (USB
install) and `latest.json` (`version`, `app`, `size`, `sha256`). A device's
release address is the site; it adds `<board id>/latest.json` and checks
version, size, image header (chip), board marker and SHA-256.

- `index.html`: ESP Web Tools install button (first install and USB reflash,
  Chrome or Edge), links to each board's `firmware.bin`.
- `manifest.json`: ESP Web Tools, one build per chip (ESP32-C6, ESP32-S3);
  the page installs the one for the chip it finds.
- `latest.json`, `firmware.bin` at the top: copies of the Waveshare's, where
  0.1.0 devices look.

Built by the repository's `Dockerfile`: PlatformIO compiles both firmwares
with `FW_VERSION` = the image version, `make_release.py` writes the JSON
files (and refuses a build without its board marker), nginx-unprivileged
serves them on port 8080.

    docker build --build-arg VERSION=0.1.0 -t crg-timer-site .
    docker run --rm -p 8080:8080 crg-timer-site

`.github/workflows/image.yml` (as RTFM's) publishes to GHCR on a `vX.Y.Z` tag.
Runs at https://clock.quagga.rocks on dc-k3s (since 0.1.0, 2026-11-13).
