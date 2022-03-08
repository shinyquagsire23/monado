#!/bin/sh
# Copyright 2021, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0
# Author: Moses Turner <moses@collabora.com>

if ! type "git-lfs" > /dev/null; then
  echo "Install git-lfs!"
  exit
fi

mkdir -p ~/.local/share/monado
cd ~/.local/share/monado
git clone https://gitlab.freedesktop.org/monado/utilities/hand-tracking-models
# Some weird distros aren't configured to automagically do the LFS things; do them just in case.
cd hand-tracking-models
git lfs install
git lfs fetch
git lfs pull