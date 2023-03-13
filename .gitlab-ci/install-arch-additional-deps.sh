#!/bin/bash
# Copyright 2023, Collabora, Ltd. and the Monado contributors
# SPDX-License-Identifier: BSL-1.0
cd $(dirname $0)

mkdir -p deps
pushd deps

# xr-hardware-git required by libsurvive-git
# libuvc required by basalt

for PKG in \
	xr-hardware-git \
	libsurvive-git \
	percetto-git \
	openhmd-git \
	librealsense \
	onnxruntime-git \
	leap-motion \
	libuvc-git \
	basalt-monado-git \

do
	wget https://aur.archlinux.org/cgit/aur.git/snapshot/"$PKG".tar.gz
	tar xfz "$PKG".tar.gz

	pushd "$PKG"

	# makepkg can not be run as root
	chown nobody:users .
	su nobody -s /bin/bash -c "MAKEFLAGS=-j$(nproc) makepkg -fs"

	pacman -U --noconfirm *.pkg.*
	popd
done

popd

# don't keep gigabytes of source code in the container image
rm -rf deps
