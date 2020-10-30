#!/bin/sh
# Copyright 2018-2020, Collabora, Ltd. and the Monado contributors
# SPDX-License-Identifier: BSL-1.0

# Install the OpenXR SDK, whatever version, installed system-wide.
git clone https://github.com/KhronosGroup/OpenXR-SDK
pushd OpenXR-SDK
mkdir build
pushd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=Off -DPRESENTATION_BACKEND=xlib -DDYNAMIC_LOADER=ON -DOpenGL_GL_PREFERENCE=GLVND -GNinja ..
ninja install
popd
popd

# Install OpenHMD from git master, as released versions are not sufficient
# for us to build.
git clone https://github.com/OpenHMD/OpenHMD
pushd OpenHMD
mkdir build
meson --prefix=/usr/local --libdir=lib build
ninja -C build install
popd
