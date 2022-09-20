#!/bin/sh
# SPDX-License-Identifier: CC0-1.0
# SPDX-FileCopyrightText: 2018-2022 Collabora, Ltd. and the Monado contributors

# This runs the command in the README as an extra bit of continuous integration.
set -e

(
    cd "$(dirname "$0")"
    sh -c "$(grep '^python3 -m pip' README.md)"
)
