#!/bin/bash
# Copyright 2018-2019, Collabora, Ltd.
# Copyright 2016, Sensics, Inc.
# SPDX-License-Identifier: Apache-2.0

if [ ! "$CLANG_FORMAT" ]; then
        for exe in clang-format-8 clang-format-7 clang-format-6.0 clang-format; do
                if which $exe >/dev/null 2>&1; then
                        CLANG_FORMAT=$exe
                        break
                fi
        done
fi
if [ ! "$CLANG_FORMAT" ]; then
        echo "Can't find clang-format - please set CLANG_FORMAT to a command or path" >&2
        exit 1
fi

runClangFormatOnDir() {
    find "$1" \( -name "*.c" -o -name "*.cpp" -o -name "*.h" \)| \
        grep -v "\.boilerplate" | \
        xargs ${CLANG_FORMAT} -style=file -i
}

(
cd $(dirname $0)/../src/xrt
runClangFormatOnDir .
)
