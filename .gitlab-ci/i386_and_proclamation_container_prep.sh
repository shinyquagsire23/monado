#!/bin/bash
# Copyright 2018-2022, Collabora, Ltd. and the Monado contributors
# SPDX-License-Identifier: BSL-1.0

(
    cd $(dirname $0)
    bash ./install-cross.sh
    # Using this script "follows the instructions" for some testing of our instructions.
    bash ./install-ci-fairy.sh
)

python3 -m pip install proclamation cmakelang
