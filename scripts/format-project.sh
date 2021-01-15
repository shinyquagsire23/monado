#!/bin/sh
# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0
# Author: Ryan Pavlik <ryan.pavlik@collabora.com>

# Formats all the source files in this project

set -e

if [ ! "${CLANGFORMAT}" ]; then
        for fn in clang-format-11 clang-format-10 clang-format-9 clang-format-8 clang-format-7 clang-format-6.0 clang-format; do
                if command -v $fn > /dev/null; then
                        CLANGFORMAT=$fn
                        break
                fi
        done
fi

if [ ! "${CLANGFORMAT}" ]; then
        echo "We need some version of clang-format, please install one!" 1>&2
        exit 1
fi

(
        cd $(dirname $0)/..
        find \
                src/xrt/auxiliary \
                src/xrt/compositor \
                src/xrt/drivers \
                src/xrt/include \
                src/xrt/ipc \
                src/xrt/state_trackers \
                src/xrt/targets \
                tests \
                \( -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) \
                -exec ${CLANGFORMAT} -i -style=file \{\} +
)
