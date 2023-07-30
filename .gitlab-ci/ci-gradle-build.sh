#!/usr/bin/env bash
# SPDX-License-Identifier: CC0-1.0
# SPDX-FileCopyrightText: 2018-2022 Collabora, Ltd. and the Monado contributors
set -e

MAX_WORKERS="${FDO_CI_CONCURRENT:-4}"

export GRADLE_ARGS="-Porg.gradle.daemon=false "

set -x
cp .gitlab-ci/local.properties .
./gradlew clean
./gradlew --max-workers "$MAX_WORKERS" "$@"
