#!/bin/sh -eux
sysconfdir="$DESTDIR"/"$1"
manifest="$2"
xrversion="$3"

runtime_path="$sysconfdir"/xdg/openxr/"$xrversion"/active_runtime.json

mkdir -p "$sysconfdir"/xdg/openxr/"$xrversion"
ln -s "$manifest" "$runtime_path"
