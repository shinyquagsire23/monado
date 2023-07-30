#!/usr/bin/env bash
# SPDX-License-Identifier: CC0-1.0
# SPDX-FileCopyrightText: 2018-2022 Collabora, Ltd. and the Monado contributors
set -e
set -x

rm -rf build
cmake -GNinja -B build -S . "$@"
echo "Build Options:"; grep "^XRT_" build/CMakeCache.txt
ninja -C build
