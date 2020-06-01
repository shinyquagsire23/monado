#!/usr/bin/env python3
# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0
"""Generate code from a JSON file describing interaction profiles and bindings."""

import json
import argparse


def strip_subpath_end(path):
    if (path.endswith("/value")):
        return True, path[:-6]
    if (path.endswith("/click")):
        return True, path[:-6]
    if (path.endswith("/touch")):
        return True, path[:-6]
    if (path.endswith("/pose")):
        return True, path[:-5]
    if (path.endswith("/x")):
        return True, path[:-2]
    if (path.endswith("/y")):
        return True, path[:-2]
    if (path.endswith("/output/haptic")):
        return False, path
    return False, path


class Component:

    @classmethod
    def parse_array(cls, user_paths, arr):
        """Turn an array of data into an array of Component objects.
        Creates a Component for each user_path and stripped subpaths.
        """
        check = {}
        ret = []
        for elm in arr:
            ups = user_paths
            if ("user_paths" in elm):
                ups = [elm["user_paths"]]
            for up in ups:
                subpath = elm["subpath"]
                did_strip, stripped_path = strip_subpath_end(subpath)
                fullpath = up + stripped_path
                if (did_strip and not fullpath in check):
                    check[fullpath] = True
                    ret.append(cls(fullpath, elm))
                fullpath = up + subpath
                ret.append(cls(fullpath, elm))
        return ret

    def __init__(self, path, elm):
        """Construct an component."""
        self.path = path


class Profile:
    """An interctive bindings profile."""
    def __init__(self, name, data):
        """Construct an profile."""
        self.name = name
        self.func = name[22:].replace("/", "_")
        self.components = Component.parse_array(data["user_paths"], data["components"])
        self.by_length = {}
        for component in self.components:
            l = len(component.path)
            if (l in self.by_length):
                self.by_length[l].append(component)
            else:
                self.by_length[l] = [component]


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
        self.profiles = [Profile(name, call) for name, call in data.items()]


header = '''// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  {brief}.
 * @author Jakob Bornecrantz <jakob@collabora.com>
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
#include "xrt/xrt_compiler.h"

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
    f.write("\n// clang-format on\n")
    f.close()


def generate_bindings_h(file, p):
    """Generate header for the verify subpaths functions."""
    f = open(file, "w")
    f.write(header.format(brief='Generated bindings data header', group='oxr_api'))
    f.write('''
#include "xrt/xrt_compiler.h"


// clang-format off
''')

    for profile in p.profiles:
        f.write("\nbool\noxr_verify_" + profile.func + "_subpath(const char *str, size_t length);\n")
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
        if output.endswith("oxr_generated_bindings.c"):
            generate_bindings_c(output, p)
        if output.endswith("oxr_generated_bindings.h"):
            generate_bindings_h(output, p)


if __name__ == "__main__":
    main()
