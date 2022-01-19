#!/usr/bin/env bash
# SPDX-License-Identifier: CC0-1.0
# SPDX-FileCopyrightText: 2018-2022 Collabora, Ltd. and the Monado contributors

set -euo pipefail

# Prep the source tree
git merge "origin/${DISTRO}/${CODENAME}" --no-commit
datestamp=$(date --utc "+%Y%m%d")
debian/extra/prepare-commit-package.sh "${CI_COMMIT_SHA}" "1~${DEB_VERSION_SUFFIX}~ci${datestamp}"
# Build the package
debuild -uc -us "$@"
# Stash the package version in a convenient file for a later job.
INCOMING="$(pwd)/incoming"
export INCOMING
mkdir -p "$INCOMING"
dpkg-parsechangelog --show-field version > "incoming/${CODENAME}.distro"
# Use dput-ng to move the package-related files into some artifacts.
mkdir -p ~/.dput.d/profiles
envsubst < .gitlab-ci/localhost.json > ~/.dput.d/profiles/localhost.json
dput --debug localhost "../monado_$(dpkg-parsechangelog --show-field version)_amd64.changes"
