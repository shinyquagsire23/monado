#!/bin/bash
# Copyright 2018-2020, 2022, Collabora, Ltd. and the Monado contributors
# SPDX-License-Identifier: BSL-1.0

(
    cd $(dirname $0)
    bash ./install-ndk.sh
    bash ./install-android-sdk.sh
)
