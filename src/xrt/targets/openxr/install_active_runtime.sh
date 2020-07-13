#!/bin/sh -eux
# Copyright 2019, Drew DeVault <sir@cmpwn.com>
# SPDX-License-Identifier: BSL-1.0
#
# Used by the Meson build only.

sysconfdir="${DESTDIR-}"/"$1"
manifest="$2"
xrversion="$3"

runtime_path="$sysconfdir"/xdg/openxr/"$xrversion"/active_runtime.json

mkdir -p "$sysconfdir"/xdg/openxr/"$xrversion"
ln -sf "$manifest" "$runtime_path"
