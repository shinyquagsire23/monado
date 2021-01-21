#!/usr/bin/env python3
# Copyright 2020-2021, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0
"""Generate code from a JSON file describing interaction profiles and
bindings."""

import json
import argparse


def handle_subpath(pathgroup_cls, feature_list, subaction_path, sub_path_itm):
    sub_path_name = sub_path_itm[0]
    sub_path_obj = sub_path_itm[1]

    # Oculus Touch a,b/x,y only exist on one controller
    if "side" in sub_path_obj and sub_path_obj["side"] not in subaction_path:
        return

    for feature in sub_path_obj["features"]:
        feature_list.append(Feature(subaction_path, sub_path_itm, feature))


class Feature:
    """Features roughly correlate with data sources. For example a trackpad may
    have several features: position, click, touch, force, ...
    """

    @classmethod
    def parse_features(feature_cls, subaction_paths, paths):
        """Turn a profile's input paths into a list of Feature objects."""
        feature_list = []
        for subaction_path in subaction_paths:
            for sub_path_itm in paths.items():
                handle_subpath(feature_cls, feature_list, subaction_path, sub_path_itm)
        return feature_list

    def __init__(self, subaction_path, sub_path_itm, feature_str):
        # note: self.sub_path_name starts with a slash
        self.sub_path_name = sub_path_itm[0]
        self.sub_path_obj = sub_path_itm[1]
        self.subaction_path = subaction_path
        self.feature_str = feature_str

    """A group of paths that derive from the same input.
    For example .../thumbstick, .../thumbstick/x, .../thumbstick/y
    """
    def to_monado_paths(self):
        paths = []

        basepath = self.subaction_path + self.sub_path_name

        if self.feature_str == "position":
            paths.append(basepath + "/" + "x")
            paths.append(basepath + "/" + "y")
            paths.append(basepath)
        else:
            paths.append(basepath + "/" + self.feature_str)
            paths.append(basepath)

        return paths

    def is_input(self):
        # only haptics is output so far, everythine else is input
        return self.feature_str != "haptic"

    def is_output(self):
        return not self.is_input()


class Profile:
    """An interctive bindings profile."""
    def __init__(self, name, data):
        """Construct an profile."""
        self.name = name
        self.monado_device = data["monado_device"]
        self.title = data['title']
        self.func = name[22:].replace("/", "_")
        self.features = Feature.parse_features(data["subaction_paths"],
                                               data["subpaths"])
        self.hw_type = data["type"]

        self.by_length = {}
        for feature in self.features:
            for path in feature.to_monado_paths():
                length = len(path)
                if (length in self.by_length):
                    self.by_length[length].append(path)
                else:
                    self.by_length[length] = [path]


class Bindings:
    """A group of interactive profiles used in bindings."""

    @classmethod
    def parse(cls, data):
        """Parse a dictionary defining a protocol into Profile objects."""
        return cls(data)

    @classmethod
    def load_and_parse(cls, file):
        """Load a JSON file and parse it into Profile objects."""
        with open(file) as infile:
            return cls.parse(json.loads(infile.read()))

    def __init__(self, data):
        """Construct a bindings from a dictionary of profiles."""
        self.profiles = [Profile(name, call) for
                         name, call in data["profiles"].items()]


header = '''// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  {brief}.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @ingroup {group}
 */
'''

func_start = '''
bool
oxr_verify_{func}_subpath(const char *str, size_t length)
{{
\tswitch (length) {{
'''

if_strcmp = '''if (strcmp(str, "{check}") == 0) {{
\t\t\treturn true;
\t\t}} else '''


def generate_bindings_c(file, p):
    """Generate the file to verify subpaths on a interaction profile."""
    f = open(file, "w")
    f.write(header.format(brief='Generated bindings data', group='oxr_main'))
    f.write('''
#include "b_generated_bindings.h"
#include <string.h>

// clang-format off
''')

    for profile in p.profiles:
        f.write(func_start.format(func=profile.func))
        for length in profile.by_length:
            f.write("\tcase " + str(length) + ":\n\t\t")
            for path in profile.by_length[length]:
                f.write(if_strcmp.format(check=path))
            f.write("{\n\t\t\treturn false;\n\t\t}\n")
        f.write("\tdefault:\n\t\treturn false;\n\t}\n}\n")

    f.write(f'\n\nstruct profile_template profile_templates[{len(p.profiles)}] = {{ // array of profile_template\n')
    for profile in p.profiles:
        hw_name = str(profile.name.split("/")[-1])
        vendor_name = str(profile.name.split("/")[-2])
        fname = vendor_name + "_" + hw_name + "_profile.json"
        controller_type = "monado_" + vendor_name + "_" + hw_name

        num_bindings = len(profile.features)
        f.write(f'\t{{ // profile_template\n')
        f.write(f'\t\t.name = {profile.monado_device},\n')
        f.write(f'\t\t.path = "{profile.name}",\n')
        f.write(f'\t\t.localized_name = "{profile.title}",\n')
        f.write(f'\t\t.steamvr_input_profile_path = "{fname}",\n')
        f.write(f'\t\t.steamvr_controller_type = "{controller_type}",\n')
        f.write(f'\t\t.num_bindings = {num_bindings},\n')
        f.write(f'\t\t.bindings = (struct binding_template[]){{ // array of binding_template\n')

        feature: Feature
        for idx, feature in enumerate(profile.features):
            sp_obj = feature.sub_path_obj

            steamvr_path = feature.sub_path_name
            if feature.feature_str in ["click", "touch", "force", "value"]:
                steamvr_path += "/" + feature.feature_str

            f.write(f'\t\t\t{{ // binding_template {idx}\n')
            f.write(f'\t\t\t\t.subaction_path = "{feature.subaction_path}",\n')
            f.write(f'\t\t\t\t.steamvr_path = "{steamvr_path}",\n')
            f.write(f'\t\t\t\t.localized_name = "{sp_obj["localized_name"]}",\n')

            f.write('\t\t\t\t.paths = { // array of paths\n')
            for path in feature.to_monado_paths():
                f.write(f'\t\t\t\t\t"{path}",\n')
            f.write('\t\t\t\t\tNULL\n')
            f.write('\t\t\t\t}, // /array of paths\n')

            # print("feature", feature.__dict__)

            feature_str = feature.feature_str

            # controllers can have input that we don't have bindings for'
            if feature_str not in sp_obj["monado_bindings"]:
                continue

            monado_binding = sp_obj["monado_bindings"][feature_str]

            if feature.is_input() and monado_binding is not None:
                f.write(f'\t\t\t\t.input = {monado_binding},\n')
            else:
                f.write(f'\t\t\t\t.input = 0,\n')

            if feature.is_output() and monado_binding is not None:
                f.write(f'\t\t\t\t.output = {monado_binding},\n')
            else:
                f.write(f'\t\t\t\t.output = 0,\n')

            f.write(f'\t\t\t}}, // /binding_template {idx}\n')

        f.write('\t\t}, // /array of binding_template\n')
        f.write('\t}, // /profile_template\n')

    f.write('}; // /array of profile_template\n\n')

    f.write("\n// clang-format on\n")

    f.close()


def generate_bindings_h(file, p):
    """Generate header for the verify subpaths functions."""
    f = open(file, "w")
    f.write(header.format(brief='Generated bindings data header',
                          group='oxr_api'))
    f.write('''
#pragma once

#include <stddef.h>

#include "xrt/xrt_defines.h"

// clang-format off
''')

    for profile in p.profiles:
        f.write("\nbool\noxr_verify_" + profile.func +
                "_subpath(const char *str, size_t length);\n")

    f.write(f'''

struct binding_template
{{
\tconst char *subaction_path;
\tconst char *steamvr_path;
\tconst char *localized_name;
\tconst char *paths[8];
\tenum xrt_input_name input;
\tenum xrt_output_name output;
}};

struct profile_template
{{
\tenum xrt_device_name name;
\tconst char *path;
\tconst char *localized_name;
\tconst char *steamvr_input_profile_path;
\tconst char *steamvr_controller_type;
\tstruct binding_template *bindings;
\tsize_t num_bindings;
}};

#define NUM_PROFILE_TEMPLATES {len(p.profiles)}
extern struct profile_template profile_templates[{len(p.profiles)}];
''')

    f.write("\n// clang-format on\n")
    f.close()


def main():
    """Handle command line and generate a file."""
    parser = argparse.ArgumentParser(description='Bindings generator.')
    parser.add_argument(
        'bindings', help='Bindings file to use')
    parser.add_argument(
        'output', type=str, nargs='+',
        help='Output file, uses the name to choose output type')
    args = parser.parse_args()

    p = Bindings.load_and_parse(args.bindings)

    for output in args.output:
        if output.endswith("generated_bindings.c"):
            generate_bindings_c(output, p)
        if output.endswith("generated_bindings.h"):
            generate_bindings_h(output, p)


if __name__ == "__main__":
    main()
