#!/bin/sh

set -eu
mkdir -p build

gcc src/dither.c -std=c89 -Wall -Wextra -O2 -o build/dither
