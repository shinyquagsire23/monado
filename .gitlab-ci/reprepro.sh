#!/usr/bin/env bash
# SPDX-License-Identifier: CC0-1.0
# SPDX-FileCopyrightText: 2018-2022 Collabora, Ltd. and the Monado contributors

###############################################
#           GENERATED - DO NOT EDIT           #
# see .gitlab-ci/reprepro.sh.template instead #
###############################################


set -euo pipefail

# Convince gnupg to work properly in CI
mkdir -p ~/.gnupg && chmod 700 ~/.gnupg
cp .gitlab-ci/gpg.conf .gitlab-ci/gpg-agent.conf ~/.gnupg
echo RELOADAGENT | gpg-connect-agent
gpg --batch --no-tty --yes --pinentry-mode loopback --passphrase "${MONADO_GPG_PASSPHRASE}" --import "${MONADO_GPG_SECRET_KEY}"
mkdir -p repo/conf
# Substitute in the GPG fingerprint into the repository config.
# This file is itself generated with ci-fairy.
envsubst < .gitlab-ci/distributions  >> repo/conf/distributions

# For each distro, sign the changes file and add it to the repo.

# bullseye
if [ -f "incoming/bullseye.distro" ]; then
    VERSION=$(cat incoming/bullseye.distro)
    echo "Signing and processing bullseye: ${VERSION}"
    debsign -k "${MONADO_GPG_FINGERPRINT}" -p "gpg --batch --no-tty --yes --pinentry-mode loopback --passphrase ${MONADO_GPG_PASSPHRASE}" "incoming/monado_${VERSION}_amd64.changes"
    reprepro -V --ignore=wrongdistribution -b repo include bullseye "incoming/monado_${VERSION}_amd64.changes"
else
    echo "Skipping bullseye - no artifact found"
fi

# focal
if [ -f "incoming/focal.distro" ]; then
    VERSION=$(cat incoming/focal.distro)
    echo "Signing and processing focal: ${VERSION}"
    debsign -k "${MONADO_GPG_FINGERPRINT}" -p "gpg --batch --no-tty --yes --pinentry-mode loopback --passphrase ${MONADO_GPG_PASSPHRASE}" "incoming/monado_${VERSION}_amd64.changes"
    reprepro -V --ignore=wrongdistribution -b repo include focal "incoming/monado_${VERSION}_amd64.changes"
else
    echo "Skipping focal - no artifact found"
fi

# hirsute
if [ -f "incoming/hirsute.distro" ]; then
    VERSION=$(cat incoming/hirsute.distro)
    echo "Signing and processing hirsute: ${VERSION}"
    debsign -k "${MONADO_GPG_FINGERPRINT}" -p "gpg --batch --no-tty --yes --pinentry-mode loopback --passphrase ${MONADO_GPG_PASSPHRASE}" "incoming/monado_${VERSION}_amd64.changes"
    reprepro -V --ignore=wrongdistribution -b repo include hirsute "incoming/monado_${VERSION}_amd64.changes"
else
    echo "Skipping hirsute - no artifact found"
fi

# impish
if [ -f "incoming/impish.distro" ]; then
    VERSION=$(cat incoming/impish.distro)
    echo "Signing and processing impish: ${VERSION}"
    debsign -k "${MONADO_GPG_FINGERPRINT}" -p "gpg --batch --no-tty --yes --pinentry-mode loopback --passphrase ${MONADO_GPG_PASSPHRASE}" "incoming/monado_${VERSION}_amd64.changes"
    reprepro -V --ignore=wrongdistribution -b repo include impish "incoming/monado_${VERSION}_amd64.changes"
else
    echo "Skipping impish - no artifact found"
fi
