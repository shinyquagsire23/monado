#!/usr/bin/env python3
# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0
"""Generate SteamVR input profiles from a JSON file describing interaction
profiles and bindings."""

from bindings import *
import argparse
import os
import errno
import json


def names(p):
    hw_name = str(p.name.split("/")[-1])
    vendor_name = str(p.name.split("/")[-2])

    fname = vendor_name + "_" + hw_name + "_profile.json"

    return hw_name, vendor_name, fname


def open_file(args, fname):
    out_dir = str(args.output)
    try:
        os.makedirs(out_dir)
    except OSError as e:
        if e.errno != errno.EEXIST:
            raise

    fname = out_dir + "/" + fname

    f = open(fname, "w")
    return f


def steamvr_subpath_name(sub_path_name, sub_path_obj):
    if sub_path_obj["type"] == "pose":
        return sub_path_name.replace("/input/", "/pose/")

    return sub_path_name


def get_required_features(path_type):
    if path_type == "button":
        return ["click", "touch"]
    if path_type == "trigger":
        return ["click", "touch", "value", "force"]
    if path_type == "joystick":
        return ["click", "touch"]
    if path_type == "pose":
        return []
    if path_type == "vibration":
        return []
    return []


def main():
    """Handle command line and generate a file."""
    parser = argparse.ArgumentParser(description='Bindings generator.')
    parser.add_argument(
        'bindings', help='Bindings file to use')
    parser.add_argument(
        'output', type=str, help='Output directory')
    args = parser.parse_args()

    bindings = Bindings.load_and_parse(args.bindings)

    for p in bindings.profiles:

        device_class = ""
        if p.hw_type == "tracked_controller":
            device_class = "TrackedDeviceClass_Controller"
        else:
            # TODO: profile for non-controller hw
            continue

        hw_name, vendor_name, fname = names(p)

        input_source = {}

        feature: Feature
        for idx, feature in enumerate(p.features):
            sp_name = steamvr_subpath_name(feature.sub_path_name, feature.sub_path_obj)
            sp = feature.sub_path_obj

            input_source[sp_name] = {
                "type": sp["type"],
                "binding_image_point": [0, 0],  # TODO
                "order": idx
            }

            for req in get_required_features(sp["type"]):
                input_source[sp_name][req] = req in sp["features"]

        j = {
            "json_id": "input_profile",
            "controller_type": "monado_" + vendor_name + "_" + hw_name,
            "device_class": device_class,
            "resource_root": "steamvr-monado",
            "driver_name": "monado",
            # "legacy_binding": None, # TODO
            "input_bindingui_mode": "controller_handed",
            "should_show_binidng_errors": True,
            "input_bindingui_left": {
                "image": "{indexcontroller}/icons/indexcontroller_left.svg"  # TODO
            },
            "input_bindingui_right": {
                "image": "{indexcontroller}/icons/indexcontroller_right.svg"  # TODO
            },
            "input_source": input_source

        }

        f = open_file(args, fname)

        # print("Creating SteamVR input profile", f.name)

        json.dump(j, f, indent=2)

        f.close()


if __name__ == "__main__":
    main()
