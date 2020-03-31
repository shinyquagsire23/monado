#!/bin/sh
# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0
# Author: Ryan Pavlik <ryan.pavlik@collabora.com>

# Runs "codespell" all the source files in this project.
# Pass:
#     -i 3
# as arguments to interactively fix detected issues,
# including ambiguous ones,
# rather than just directly fixing the ones it can.

# Success error code if no mistakes or only auto-fixable mistakes found.
# Failure error code if one or more ambiguous fixes found (requiring interactive fixing).

# See https://github.com/codespell-project/codespell
# or just run pip3 install codespell

set -e

# Comma-delimited list of words for codespell to not try to correct.
IGNORE_WORDS_LIST="ang,sinc,sie,inout"

SCRIPTDIR=$(cd $(dirname $0) && pwd)

(
        cd $SCRIPTDIR/..
        find \
                *.md \
                doc \
                scripts/format-*.sh \
                src/xrt \
                \( -name "*.c" \
                -o -name "*.cpp" \
                -o -name "*.h" \
                -o -name "*.hpp" \
                -o -name "*.sh" \
                -o -name "*.md" \
                -o -name "CMakeLists.txt" \) \
                -exec codespell \
                        --exclude-file=${SCRIPTDIR}/monado-codespell.exclude \
                    --ignore-words-list="${IGNORE_WORDS_LIST}" \
                    -w \
                    "$@" \
                    \{\} +
)
