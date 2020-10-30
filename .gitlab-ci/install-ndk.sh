#!/bin/sh
# Copyright 2018-2020, Collabora, Ltd. and the Monado contributors
# SPDX-License-Identifier: BSL-1.0
VERSION=r21
FN=android-ndk-${VERSION}-linux-x86_64.zip
wget https://dl.google.com/android/repository/$FN
unzip $FN -d /opt
mv /opt/android-ndk-${VERSION} /opt/android-ndk
