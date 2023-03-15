#!/bin/sh
# Copyright 2020-2023, Mesa contributors
# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: MIT

# From https://gitlab.freedesktop.org/mesa/mesa/-/blob/999b956ebc4c26fa0c407369e630c687ece02209/.gitlab-ci/container/container_pre_build.sh

set -e

# Make a wrapper script for ninja to always include the -j flags
# to avoid oversubscribing/DOS'ing the shared runners
{
    echo '#!/bin/sh -x'
    # shellcheck disable=SC2016
    echo '/usr/bin/ninja -j${FDO_CI_CONCURRENT:-4} "$@"'
} > /usr/local/bin/ninja
chmod +x /usr/local/bin/ninja


# Set MAKEFLAGS so that all make invocations in container builds include the
# flags (doesn't apply to non-container builds, but we don't run make there)
export MAKEFLAGS="-j${FDO_CI_CONCURRENT:-4}"
