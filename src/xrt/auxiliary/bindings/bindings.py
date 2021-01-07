#!/usr/bin/env python3
# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0
"""Generate code from a JSON file describing interaction profiles and
bindings."""

import json
import argparse


class BindingPath:
    def __init__(self, subaction_path, path):
        """Construct an component."""
        self.subaction_path = subaction_path
        self.path = path


def handle_subpath(pathgroup_cls, pathgroup_list, subaction_path, sub_path_itm):
    sub_path_name = sub_path_itm[0]
    sub_path_obj = sub_path_itm[1]

    # Oculus Touch a,b/x,y only exist on one controller
    if "side" in sub_path_obj and sub_path_obj["side"] not in subaction_path:
        return

    # path without the component, e.g. /user/hand/left/input/a, without /click
    base_path = subaction_path + sub_path_name

    # Add each component with a base path as a separate PathGroup, e.g.
    # /user/hand/left/input/a/click and /user/hand/input/a map to the same a click.
    # /user/hand/left/input/a/touch and /user/hand/input/a map to the same a touch.
    # Code later will have to decide how /user/hand/input/a is actually bound
    if sub_path_obj["type"] in ["button", "trigger", "trackpad", "joystick"]:
        # Add component paths like /user/hand/left/input/a/touch
        for component in ["click", "touch", "value", "force"]:
            if component in sub_path_obj and sub_path_obj[component]:
                binding_key = component
                pathgroup = []

                comp_path = base_path + "/" + component
                pathgroup.append(BindingPath(subaction_path, comp_path))

                # Add a base path like /user/hand/left/input/a with the same
                # binding_key as the component
                pathgroup.append(BindingPath(subaction_path, base_path))

                pathgroup_list.append(pathgroup_cls(subaction_path, pathgroup, sub_path_itm, binding_key))

    # same for joystick and trackpad, but with special x/y components
    if sub_path_obj["type"] in ["joystick", "trackpad"]:
        binding_key = "position"
        pathgroup = []

        pathgroup.append(BindingPath(subaction_path, base_path + "/x"))
        pathgroup.append(BindingPath(subaction_path, base_path + "/y"))

        # Add a base path like /user/hand/left/input/trackpad with the same binding_key as the /x and /y component
        pathgroup.append(BindingPath(subaction_path, base_path))

        pathgroup_list.append(pathgroup_cls(subaction_path, pathgroup, sub_path_itm, binding_key))

    # pose inputs can be bound with or without pose component
    if sub_path_obj["type"] == "pose":
        binding_key = "pose"
        pathgroup = []

        pathgroup.append(BindingPath(subaction_path, base_path))
        pathgroup.append(BindingPath(subaction_path, base_path + "/pose"))

        pathgroup_list.append(pathgroup_cls(subaction_path, pathgroup, sub_path_itm, binding_key))

    # haptic feedback only has a base path
    if sub_path_obj["type"] == "vibration":
        binding_key = "haptic"
        pathgroup = []

        pathgroup.append(BindingPath(subaction_path, base_path))

        pathgroup_list.append(pathgroup_cls(subaction_path, pathgroup, sub_path_itm, binding_key))


class PathGroup:
    """Group of paths associated with a single input, for example
    * /user/hand/left/input/trackpad
    * /user/hand/left/input/trackpad/x
    * /user/hand/left/input/trackpad/y
    """

    @classmethod
    def parse_paths(pathgroup_cls, subaction_paths, paths):
        """Turn a profile's input paths into an array of PathGroup objects.
        Creates a PathGroup for each subaction_path and stripped subpaths.
        """
        pathgroup_list = []
        for subaction_path in subaction_paths:
            for sub_path_itm in paths.items():
                handle_subpath(pathgroup_cls, pathgroup_list, subaction_path, sub_path_itm)
        return pathgroup_list

    def __init__(self, subaction_path, pathgroup, sub_path_itm, binding_key=None):
        self.sub_path_name = sub_path_itm[0]
        self.sub_path_obj = sub_path_itm[1]
        self.subaction_path = subaction_path
        self.pathgroup = pathgroup
        self.binding_key = binding_key
        self.is_output = self.sub_path_obj["type"] == "vibration"
        self.is_input = not self.is_output


class Profile:
    """An interctive bindings profile."""
    def __init__(self, name, data):
        """Construct an profile."""
        self.name = name
        self.monado_device = data["monado_device"]
        self.title = data['title']
        self.func = name[22:].replace("/", "_")
        self.pathgroups = PathGroup.parse_paths(data["subaction_paths"],
                                                data["subpaths"])
        self.hw_type = data["type"]

        self.by_length = {}
        for pathgroup in self.pathgroups:
            for input_path in pathgroup.pathgroup:
                length = len(input_path.path)
                if (length in self.by_length):
                    self.by_length[length].append(input_path)
                else:
                    self.by_length[length] = [input_path]


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
            for component in profile.by_length[length]:
                f.write(if_strcmp.format(check=component.path))
            f.write("{\n\t\t\treturn false;\n\t\t}\n")
        f.write("\tdefault:\n\t\treturn false;\n\t}\n}\n")

    f.write(f'\n\nstruct profile_template profile_templates[{len(p.profiles)}] = {{ // array of profile_template\n')
    for profile in p.profiles:
        hw_name = str(profile.name.split("/")[-1])
        vendor_name = str(profile.name.split("/")[-2])
        fname = vendor_name + "_" + hw_name + "_profile.json"
        controller_type = "monado_" + vendor_name + "_" + hw_name

        num_bindings = len(profile.pathgroups)
        f.write(f'\t{{ // profile_template\n')
        f.write(f'\t\t.name = {profile.monado_device},\n')
        f.write(f'\t\t.path = "{profile.name}",\n')
        f.write(f'\t\t.localized_name = "{profile.title}",\n')
        f.write(f'\t\t.steamvr_input_profile_path = "{fname}",\n')
        f.write(f'\t\t.steamvr_controller_type = "{controller_type}",\n')
        f.write(f'\t\t.num_bindings = {num_bindings},\n')
        f.write(f'\t\t.bindings = (struct binding_template[]){{ // array of binding_template\n')

        pathgroup: PathGroup
        for idx, pathgroup in enumerate(profile.pathgroups):
            sp_obj = pathgroup.sub_path_obj

            steamvr_path = pathgroup.sub_path_name
            if pathgroup.binding_key in ["click", "touch", "force", "value"]:
                steamvr_path += "/" + pathgroup.binding_key

            f.write(f'\t\t\t{{ // binding_template {idx}\n')
            f.write(f'\t\t\t\t.subaction_path = "{pathgroup.subaction_path}",\n')
            f.write(f'\t\t\t\t.steamvr_path = "{steamvr_path}",\n')
            f.write(f'\t\t\t\t.localized_name = "{sp_obj["localized_name"]}",\n')

            f.write('\t\t\t\t.paths = { // array of paths\n')
            for input_path in pathgroup.pathgroup:
                f.write(f'\t\t\t\t\t"{input_path.path}",\n')
            f.write('\t\t\t\t\tNULL\n')
            f.write('\t\t\t\t}, // /array of paths\n')

            binding_key = pathgroup.binding_key
            monado_binding = sp_obj["monado_bindings"][binding_key]

            if pathgroup.is_input and monado_binding is not None:
                f.write(f'\t\t\t\t.input = {monado_binding},\n')
            else:
                f.write(f'\t\t\t\t.input = 0,\n')

            if pathgroup.is_output and monado_binding is not None:
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
