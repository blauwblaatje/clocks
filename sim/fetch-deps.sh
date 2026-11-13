#!/bin/sh
# Downloads the simulator's dependencies into deps/: LVGL v9.2.2 and the
# Montserrat TTFs (v7.222, SIL OFL). Run once from sim/.
set -e
cd "$(dirname "$0")"
mkdir -p deps
if [ ! -d deps/lvgl ]; then
    curl -sSL https://github.com/lvgl/lvgl/archive/refs/tags/v9.2.2.tar.gz | tar xz -C deps
    mv deps/lvgl-9.2.2 deps/lvgl
fi
mkdir -p deps/montserrat
for w in Medium SemiBold Bold; do
    [ -f deps/montserrat/Montserrat-$w.ttf ] ||
        curl -sSL -o deps/montserrat/Montserrat-$w.ttf https://github.com/JulietaUla/Montserrat/raw/v7.222/fonts/ttf/Montserrat-$w.ttf
done
[ -f deps/montserrat/OFL.txt ] || curl -sSL -o deps/montserrat/OFL.txt https://github.com/JulietaUla/Montserrat/raw/v7.222/OFL.txt
echo "deps ready"
