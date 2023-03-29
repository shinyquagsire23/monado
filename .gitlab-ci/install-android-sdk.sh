#!/usr/bin/env bash
# Copyright 2022, Collabora, Ltd. and the Monado contributors
# SPDX-License-Identifier: BSL-1.0

##
#######################################################
#                GENERATED - DO NOT EDIT              #
# see .gitlab-ci/install-android-sdk.sh.jinja instead #
#######################################################
##

# Partially inspired by https://about.gitlab.com/blog/2018/10/24/setting-up-gitlab-ci-for-android-projects/

set -eo pipefail
VERSION=9477386
ROOT=${ROOT:-/opt/android-sdk}
ANDROID_COMPILE_SDK=${ANDROID_COMPILE_SDK:-32}
ANDROID_BUILD_TOOLS=${ANDROID_BUILD_TOOLS:-32.0.0}

mkdir -p "$ROOT"
FN=commandlinetools-linux-${VERSION}_latest.zip
wget https://dl.google.com/android/repository/$FN
unzip $FN -d "$ROOT/extract"
mkdir -p "$ROOT/cmdline-tools"
mv "$ROOT/extract/cmdline-tools/" "$ROOT/cmdline-tools/latest/"
mv "$ROOT/extract/" "$ROOT/cmdline-tools/"

SDKMANAGER=$ROOT/cmdline-tools/latest/bin/sdkmanager

echo "Installing the Android compile SDK platform android-${ANDROID_COMPILE_SDK}"
echo y | $SDKMANAGER "platforms;android-${ANDROID_COMPILE_SDK}" >> /dev/null

echo "Installing the Android platform tools"
echo y | $SDKMANAGER "platform-tools" >> /dev/null

echo "Installing the Android build tools ${ANDROID_BUILD_TOOLS}"
echo y | $SDKMANAGER "build-tools;${ANDROID_BUILD_TOOLS}" >> /dev/null

set +o pipefail
yes | $SDKMANAGER --licenses
set -o pipefail

