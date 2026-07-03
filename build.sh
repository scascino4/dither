#!/bin/sh

set -eu
mkdir -p build

gcc src/dither.c -std=gnu89 -Wall -Wextra -O2 -o build/dither -lm
