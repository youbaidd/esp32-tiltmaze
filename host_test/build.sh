#!/usr/bin/env bash
# Builds and runs a host-native reproduction of TiltMaze's render pipeline,
# for judging drawing changes - or hunting memory bugs with ASan/UBSan -
# without flashing hardware. See README.md in this directory.
set -euo pipefail
cd "$(dirname "$0")"

cp ../main/render.c ../main/maze.c ../main/game.c .

gcc -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra \
    -Istubs -I../main \
    main_host.c render.c maze.c game.c \
    -lm -o host_test_bin

OUT="${1:-frame.ppm}"
FRAMES="${2:-1}"
ASAN_OPTIONS=detect_leaks=0 ./host_test_bin "$OUT" "$FRAMES"

PNG="${OUT%.ppm}.png"
python3 -c "from PIL import Image; Image.open('$OUT').save('$PNG')" 2>/dev/null \
    && echo "also wrote $PNG" || true
