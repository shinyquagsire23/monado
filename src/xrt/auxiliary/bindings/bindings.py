#!/usr/bin/env python3
# Copyright 2020-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0
"""Generate code from a JSON file describing interaction profiles and
bindings."""

import argparse
import json


def find_component_in_list_by_name(name, component_list, subaction_path=None, identifier_json_path=None):
    """Find a component with the given name in a list of components."""
    for component in component_list:
        if component.component_name == name:
            if subaction_path is not None and component.subaction_path != subaction_path:
                continue
            if identifier_json_path is not None and component.identifier_json_path != identifier_json_path:
                continue
            return component
    return None


class PathsByLengthCollector:
    """Helper class to sort paths by length, useful for creating fast path
    validation functions.
    """

    def __init__(self):
        self.by_length = dict()

    def add_path(self, path):
        length = len(path)
        if length in self.by_length:
            self.by_length[length].add(path)
        else:
            self.by_length[length] = {path}

    def add_paths(self, paths):
        for path in paths:
            self.add_path(path)

    def to_dict_of_lists(self):
        ret = dict()
        for length, set_per_length in self.by_length.items():
            ret[length] = list(set_per_length)
        return ret

def dpad_paths(identifier_path, center):
    paths = [
        identifier_path + "/dpad_up",
        identifier_path + "/dpad_down",
        identifier_path + "/dpad_left",
        identifier_path + "/dpad_right",
    ]

    if center:
        paths.append(identifier_path + "/dpad_center")

    return paths

class DPad:
    """Class holding per identifier information for dpad emulation."""

    @classmethod
    def parse_dpad(dpad_cls,
                   identifier_path,
                   component_list,
                   dpad_json):
        center = dpad_json["center"]
        position_str = dpad_json["position"]
        activate_str = dpad_json.get("activate")

        position_component = find_component_in_list_by_name(position_str,
                                                            component_list)
        activate_component = find_component_in_list_by_name(activate_str,
                                                            component_list)

        paths = dpad_paths(identifier_path, center)

        return DPad(center,
                    paths,
                    position_component,
                    activate_component)

    def __init__(self,
                 center,
                 paths,
                 position_component,
                 activate_component):
        self.center = center
        self.paths = paths
        self.position_component = position_component
        self.activate_component = activate_component


class Component:
    """Components correspond with the standard OpenXR components click, touch,
    force, value, x, y, twist, pose
    """

    @classmethod
    def parse_components(component_cls,
                         subaction_path,
                         identifier_json_path,
                         json_subpath):
        """
        Turn a Identifier's component paths into a list of Component objects.
        """

        monado_bindings = json_subpath["monado_bindings"]
        component_list = []
        for component_name in json_subpath["components"]:  # click, touch, ...
            matched_dpad_emulation = None
            if ("dpad_emulation" in json_subpath and
                    json_subpath["dpad_emulation"]["position"] == component_name):
                matched_dpad_emulation = json_subpath["dpad_emulation"]

            monado_binding = None
            if component_name in monado_bindings:
                monado_binding = monado_bindings[component_name]

            c = Component(subaction_path,
                          identifier_json_path,
                          json_subpath["localized_name"],
                          json_subpath["type"],
                          component_name,
                          matched_dpad_emulation,
                          monado_binding,
                          json_subpath["components"])
            component_list.append(c)

        return component_list

    def __init__(self,
                 subaction_path,
                 identifier_json_path,
                 subpath_localized_name,
                 subpath_type,
                 component_name,
                 dpad_emulation,
                 monado_binding,
                 components_for_subpath):
        self.subaction_path = subaction_path
        self.identifier_json_path = identifier_json_path  # note: starts with a slash
        self.subpath_localized_name = subpath_localized_name
        self.subpath_type = subpath_type
        self.component_name = component_name
        self.dpad_emulation = dpad_emulation
        self.monado_binding = monado_binding

        # click, touch etc. components under the subpath of this component.
        # Only needed for steamvr profile gen.
        self.components_for_subpath = components_for_subpath

    def get_full_openxr_paths(self):
        """A group of paths that derive from the same component.
        For example .../thumbstick, .../thumbstick/x, .../thumbstick/y
        """
        paths = []

        basepath = self.subaction_path + self.identifier_json_path

        if self.component_name == "position":
            paths.append(basepath + "/" + "x")
            paths.append(basepath + "/" + "y")
            if self.has_dpad_emulation():
                paths += dpad_paths(basepath, self.dpad_emulation["center"])
            paths.append(basepath)
        else:
            paths.append(basepath + "/" + self.component_name)
            paths.append(basepath)

        return paths

    def is_input(self):
        # only haptics is output so far, everything else is input
        return self.component_name != "haptic"

    def has_dpad_emulation(self):
        return self.dpad_emulation is not None

    def is_output(self):
        return not self.is_input()


class Identifer:
    """A Identifier is a OpenXR identifier with a user path, such as button
    X, a trackpad, a pose such as aim. It can have one or more features, even
    tho outputs doesn't include a component/feature path a output indentifier
    will have a haptic output feature.
    """

    @classmethod
    def parse_identifiers(indentifer_cls, json_profile):
        """Turn a profile's input paths into a list of Component objects."""

        json_subaction_paths = json_profile["subaction_paths"]
        json_subpaths = json_profile["subpaths"]

        identifier_list = []
        for subaction_path in json_subaction_paths:  # /user/hand/*
            for json_sub_path_itm in json_subpaths.items():  # /input/*, /output/*
                json_path = json_sub_path_itm[0]  # /input/trackpad
                json_subpath = json_sub_path_itm[1]  # json object associated with a subpath (type, localized_name, ...)

                # Oculus Touch a,b/x,y components only exist on one controller
                if "side" in json_subpath and "/user/hand/" + json_subpath["side"] != subaction_path:
                    continue

                # Full path to the identifier
                identifier_path = subaction_path + json_path

                component_list = Component.parse_components(subaction_path,
                                                            json_path,
                                                            json_subpath)

                dpad = None
                if "dpad_emulation" in json_subpath:
                    dpad = DPad.parse_dpad(identifier_path,
                                           component_list,
                                           json_subpath["dpad_emulation"])

                i = Identifer(subaction_path,
                              identifier_path,
                              json_path,
                              component_list,
                              dpad)
                identifier_list.append(i)

        return identifier_list

    def __init__(self,
                 subaction_path,
                 identifier_path,
                 json_path,
                 component_list,
                 dpad):
        self.subaction_path = subaction_path
        self.identifier_path = identifier_path
        self.json_path = json_path
        self.components = component_list
        self.dpad = dpad
        return


class Profile:
    """An interactive bindings profile."""

    def __init__(self, profile_name, json_profile):
        """Construct an profile."""
        self.name = profile_name
        self.localized_name = json_profile['title']
        self.profile_type = json_profile["type"]
        self.monado_device_enum = json_profile["monado_device"]
        self.validation_func_name = profile_name.replace("/interaction_profiles/", "").replace("/", "_")
        self.identifiers = Identifer.parse_identifiers(json_profile)

        self.components = []
        for identifier in self.identifiers:
            for component in identifier.components:
                self.components.append(component)

        collector = PathsByLengthCollector()
        for component in self.components:
            collector.add_paths(component.get_full_openxr_paths())
        self.subpaths_by_length = collector.to_dict_of_lists()

        collector = PathsByLengthCollector()
        for identifier in self.identifiers:
            if not identifier.dpad:
                continue
            collector.add_path(identifier.identifier_path)
        self.dpad_emulators_by_length = collector.to_dict_of_lists()

        collector = PathsByLengthCollector()
        for identifier in self.identifiers:
            if not identifier.dpad:
                continue
            path = identifier.identifier_path
            collector.add_paths(identifier.dpad.paths)

        self.dpad_paths_by_length = collector.to_dict_of_lists()


class Bindings:
    """A collection of interaction profiles used in bindings."""

    @classmethod
    def parse(cls, json_root):
        """Parse an entire bindings.json into a collection of Profile objects.
        """
        return cls(json_root)

    @classmethod
    def load_and_parse(cls, file):
        """Load a JSON file and parse it into Profile objects."""
        with open(file) as infile:
            json_root = json.loads(infile.read())
            return cls.parse(json_root)

    def __init__(self, json_root):
        """Construct a bindings from a dictionary of profiles."""
        self.profiles = [Profile(profile_name, json_profile) for
                         profile_name, json_profile in json_root["profiles"].items()]


header = '''// Copyright 2020-2022, Collabora, Ltd.
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
{name}(const char *str, size_t length)
{{
\tswitch (length) {{
'''

if_strcmp = '''if (strcmp(str, "{check}") == 0) {{
\t\t\treturn true;
\t\t}} else '''


def write_verify_func(f, name, dict_of_lists):
    """Generate function to check if a string is in a set of strings.
    Input is a file to write the code into, a dict where keys are length and
    the values are lists of strings of that length. And a suffix if any."""

    f.write(func_start.format(name=name))
    for length in dict_of_lists:
        f.write("\tcase " + str(length) + ":\n\t\t")
        for path in dict_of_lists[length]:
            f.write(if_strcmp.format(check=path))
        f.write("{\n\t\t\treturn false;\n\t\t}\n")
    f.write("\tdefault:\n\t\treturn false;\n\t}\n}\n")


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
        name = "oxr_verify_" + profile.validation_func_name + "_subpath"
        write_verify_func(f, name, profile.subpaths_by_length)
        name = "oxr_verify_" + profile.validation_func_name + "_dpad_path"
        write_verify_func(f, name, profile.dpad_paths_by_length)
        name = "oxr_verify_" + profile.validation_func_name + "_dpad_emulator"
        write_verify_func(f, name, profile.dpad_emulators_by_length)

    f.write(
        f'\n\nstruct profile_template profile_templates[{len(p.profiles)}] = {{ // array of profile_template\n')
    for profile in p.profiles:
        hw_name = str(profile.name.split("/")[-1])
        vendor_name = str(profile.name.split("/")[-2])
        fname = vendor_name + "_" + hw_name + "_profile.json"
        controller_type = "monado_" + vendor_name + "_" + hw_name

        binding_count = len(profile.components)
        f.write(f'\t{{ // profile_template\n')
        f.write(f'\t\t.name = {profile.monado_device_enum},\n')
        f.write(f'\t\t.path = "{profile.name}",\n')
        f.write(f'\t\t.localized_name = "{profile.localized_name}",\n')
        f.write(f'\t\t.steamvr_input_profile_path = "{fname}",\n')
        f.write(f'\t\t.steamvr_controller_type = "{controller_type}",\n')
        f.write(f'\t\t.binding_count = {binding_count},\n')
        f.write(
            f'\t\t.bindings = (struct binding_template[]){{ // array of binding_template\n')

        component: Component
        for idx, component in enumerate(profile.components):

            json_path = component.identifier_json_path
            steamvr_path = json_path # @todo Doesn't handle pose yet.
            if component.component_name in ["click", "touch", "force", "value"]:
                steamvr_path += "/" + component.component_name

            f.write(f'\t\t\t{{ // binding_template {idx}\n')
            f.write(f'\t\t\t\t.subaction_path = "{component.subaction_path}",\n')
            f.write(f'\t\t\t\t.steamvr_path = "{steamvr_path}",\n')
            f.write(
                f'\t\t\t\t.localized_name = "{component.subpath_localized_name}",\n')

            f.write('\t\t\t\t.paths = { // array of paths\n')
            for path in component.get_full_openxr_paths():
                f.write(f'\t\t\t\t\t"{path}",\n')
            f.write('\t\t\t\t\tNULL\n')
            f.write('\t\t\t\t}, // /array of paths\n')

            # print("component", component.__dict__)

            component_str = component.component_name

            # controllers can have input that we don't have bindings for
            if component.monado_binding:
                monado_binding = component.monado_binding

                if component.is_input() and monado_binding is not None:
                    f.write(f'\t\t\t\t.input = {monado_binding},\n')
                else:
                    f.write(f'\t\t\t\t.input = 0,\n')

                if component.has_dpad_emulation() and "activate" in component.dpad_emulation:
                    activate_component = find_component_in_list_by_name(
                        component.dpad_emulation["activate"], profile.components,
                        subaction_path=component.subaction_path,
                        identifier_json_path=component.identifier_json_path)
                    f.write(
                        f'\t\t\t\t.dpad_activate = {activate_component.monado_binding},\n')
                else:
                    f.write(f'\t\t\t\t.dpad_activate = 0,\n')

                if component.is_output() and monado_binding is not None:
                    f.write(f'\t\t\t\t.output = {monado_binding},\n')
                else:
                    f.write(f'\t\t\t\t.output = 0,\n')

            f.write(f'\t\t\t}}, // /binding_template {idx}\n')

        f.write('\t\t}, // /array of binding_template\n')

        dpads = []
        for idx, identifier in enumerate(profile.identifiers):
            if identifier.dpad:
                dpads.append(identifier)

#        for identifier in dpads:
#            print(identifier.path, identifier.dpad_position_component)

        dpad_count = len(dpads)
        f.write(f'\t\t.dpad_count = {dpad_count},\n')
        if len(dpads) == 0:
            f.write(f'\t\t.dpads = NULL,\n')
        else:
            f.write(
                f'\t\t.dpads = (struct dpad_emulation[]){{ // array of dpad_emulation\n')
            for idx, identifier in enumerate(dpads):
                f.write('\t\t\t{\n')
                f.write(f'\t\t\t\t.subaction_path = "{identifier.subaction_path}",\n')
                f.write('\t\t\t\t.paths = {\n')
                for path in identifier.dpad.paths:
                    f.write(f'\t\t\t\t\t"{path}",\n')
                f.write('\t\t\t\t},\n')
                f.write(f'\t\t\t\t.position = {identifier.dpad.position_component.monado_binding},\n')
                if identifier.dpad.activate_component:
                    f.write(f'\t\t\t\t.activate = {identifier.dpad.activate_component.monado_binding},\n')
                else:
                    f.write(f'\t\t\t\t.activate = 0')

                f.write('\t\t\t},\n')
            f.write('\t\t}, // /array of dpad_emulation\n')

        f.write('\t}, // /profile_template\n')

    f.write('}; // /array of profile_template\n\n')

    inputs = set()
    outputs = set()
    for profile in p.profiles:
        component: Component
        for idx, component in enumerate(profile.components):

            if not component.monado_binding:
                continue

            if component.subpath_type == "vibration":
                outputs.add(component.monado_binding)
            else:
                inputs.add(component.monado_binding)

    # special cased bindings that are never directly used in the input profiles
    inputs.add("XRT_INPUT_GENERIC_HEAD_POSE")
    inputs.add("XRT_INPUT_GENERIC_HEAD_DETECT")
    inputs.add("XRT_INPUT_GENERIC_HAND_TRACKING_LEFT")
    inputs.add("XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT")
    inputs.add("XRT_INPUT_GENERIC_TRACKER_POSE")

    f.write('const char *\n')
    f.write('xrt_input_name_string(enum xrt_input_name input)\n')
    f.write('{\n')
    f.write('\tswitch(input)\n')
    f.write('\t{\n')
    for input in inputs:
        f.write(f'\tcase {input}: return "{input}";\n')
    f.write(f'\tdefault: return "UNKNOWN";\n')
    f.write('\t}\n')
    f.write('}\n')

    f.write('enum xrt_input_name\n')
    f.write('xrt_input_name_enum(const char *input)\n')
    f.write('{\n')
    for input in inputs:
        f.write(f'\tif(strcmp("{input}", input) == 0) return {input};\n')
    f.write(f'\treturn XRT_INPUT_GENERIC_TRACKER_POSE;\n')
    f.write('}\n')

    f.write('const char *\n')
    f.write('xrt_output_name_string(enum xrt_output_name output)\n')
    f.write('{\n')
    f.write('\tswitch(output)\n')
    f.write('\t{\n')
    for output in outputs:
        f.write(f'\tcase {output}: return "{output}";\n')
    f.write(f'\tdefault: return "UNKNOWN";\n')
    f.write('\t}\n')
    f.write('}\n')

    f.write('enum xrt_output_name\n')
    f.write('xrt_output_name_enum(const char *output)\n')
    f.write('{\n')
    for output in outputs:
        f.write(f'\tif(strcmp("{output}", output) == 0) return {output};\n')
    f.write(f'\treturn XRT_OUTPUT_NAME_SIMPLE_VIBRATION;\n')
    f.write('}\n')

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
        f.write("\nbool\noxr_verify_" + profile.validation_func_name +
                "_subpath(const char *str, size_t length);\n")
        f.write("\nbool\noxr_verify_" + profile.validation_func_name +
                "_dpad_path(const char *str, size_t length);\n")
        f.write("\nbool\noxr_verify_" + profile.validation_func_name +
                "_dpad_emulator(const char *str, size_t length);\n")

    f.write(f'''
#define PATHS_PER_BINDING_TEMPLATE 16

enum oxr_dpad_binding_point
{{
\tOXR_DPAD_BINDING_POINT_NONE,
\tOXR_DPAD_BINDING_POINT_UP,
\tOXR_DPAD_BINDING_POINT_DOWN,
\tOXR_DPAD_BINDING_POINT_LEFT,
\tOXR_DPAD_BINDING_POINT_RIGHT,
}};

struct dpad_emulation
{{
\tconst char *subaction_path;
\tconst char *paths[PATHS_PER_BINDING_TEMPLATE];
\tenum xrt_input_name position;
\tenum xrt_input_name activate; // Can be zero
}};

struct binding_template
{{
\tconst char *subaction_path;
\tconst char *steamvr_path;
\tconst char *localized_name;
\tconst char *paths[PATHS_PER_BINDING_TEMPLATE];
\tenum xrt_input_name input;
\tenum xrt_input_name dpad_activate;
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
\tsize_t binding_count;
\tstruct dpad_emulation *dpads;
\tsize_t dpad_count;
}};

#define NUM_PROFILE_TEMPLATES {len(p.profiles)}
extern struct profile_template profile_templates[NUM_PROFILE_TEMPLATES];

''')

    f.write('const char *\n')
    f.write('xrt_input_name_string(enum xrt_input_name input);\n\n')

    f.write('enum xrt_input_name\n')
    f.write('xrt_input_name_enum(const char *input);\n\n')

    f.write('const char *\n')
    f.write('xrt_output_name_string(enum xrt_output_name output);\n\n')

    f.write('enum xrt_output_name\n')
    f.write('xrt_output_name_enum(const char *output);\n\n')

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

    bindings = Bindings.load_and_parse(args.bindings)

    for output in args.output:
        if output.endswith("generated_bindings.c"):
            generate_bindings_c(output, bindings)
        if output.endswith("generated_bindings.h"):
            generate_bindings_h(output, bindings)


if __name__ == "__main__":
    main()
