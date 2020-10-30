#!/bin/bash
# Copyright 2019-2020, Mesa contributors
# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: MIT

# Based on https://gitlab.freedesktop.org/mesa/mesa/-/blob/master/.gitlab-ci/container/x86_build.sh

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

CROSS_ARCHITECTURES="i386"
for arch in $CROSS_ARCHITECTURES; do
    dpkg --add-architecture $arch
done

apt-get update

for arch in $CROSS_ARCHITECTURES; do
    # OpenCV and libuvc aren't on this list because
    # they apparently can't be installed in both architectures at once
    apt-get install -y --no-install-recommends --no-remove \
            crossbuild-essential-${arch} \
            libelf-dev:${arch} \
            libavcodec-dev:${arch} \
            libegl1-mesa-dev:${arch} \
            libgl1-mesa-dev:${arch} \
            libglvnd-dev:${arch} \
            libhidapi-dev:${arch} \
            libudev-dev:${arch} \
            libusb-1.0-0-dev:${arch} \
            libv4l-dev:${arch} \
            libvulkan-dev:${arch} \
            libwayland-dev:${arch} \
            libx11-dev:${arch} \
            libxcb-randr0-dev:${arch} \
            libxrandr-dev:${arch} \
            libxxf86vm-dev:${arch}

    if [ "$arch" != "i386" ]; then
        mkdir /var/cache/apt/archives/${arch}
        apt-get install -y --no-remove \
                libstdc++6:${arch}
    fi
done


# for 64bit windows cross-builds
# apt-get install -y --no-remove \
#     libz-mingw-w64-dev \
#     mingw-w64 \
#     wine \
#     wine32 \
#     wine64


apt-get autoremove -y --purge
apt-get clean

# Generate cross build files for Meson
for arch in $CROSS_ARCHITECTURES; do
  cross_file="/cross_file-$arch.txt"
  /usr/share/meson/debcrossgen --arch "$arch" -o "$cross_file"
  if [ "$arch" = "i386" ]; then
    # Work around a bug in debcrossgen that should be fixed in the next release
    sed -i "s|cpu_family = 'i686'|cpu_family = 'x86'|g" "$cross_file"
  fi

  # Rely on qemu-user being configured in binfmt_misc on the host
  sed -i -e '/\[properties\]/a\' -e "needs_exe_wrapper = False" "$cross_file"
done
