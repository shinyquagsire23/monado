#!/bin/bash
# Copyright 2018-2021, Collabora, Ltd. and the Monado contributors
# SPDX-License-Identifier: BSL-1.0

(
    cd $(dirname $0)
    bash ./install-cross.sh
)

python3 -m pip install proclamation
