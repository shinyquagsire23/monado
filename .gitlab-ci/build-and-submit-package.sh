#!/usr/bin/env bash
# SPDX-License-Identifier: CC0-1.0
# SPDX-FileCopyrightText: 2018-2022 Collabora, Ltd. and the Monado contributors
#
# Requires some environment variables (normally set by CI)
# Any extra args get passed to debuild, so try -B for a local binary-only build

set -euo pipefail

echo "DISTRO ${DISTRO}"
echo "CODENAME ${CODENAME}"
echo "DEB_VERSION_SUFFIX ${DEB_VERSION_SUFFIX}"
echo "CI_COMMIT_SHA ${CI_COMMIT_SHA}"

git remote update --prune

# Prep the source tree: grab the debian directory from the packaging branch.
git checkout "origin/${DISTRO}/${CODENAME}" -- debian/
datestamp=$(date --utc "+%Y%m%d")


if [ ! "${CI_COMMIT_SHA}" ]; then
    echo "Why don't I know what the commit hash is?"
    exit 1
fi

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
