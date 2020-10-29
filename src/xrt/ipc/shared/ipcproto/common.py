#!/usr/bin/env python3
# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0
"""Generate code from a JSON file describing the IPC protocol."""

import json
import re


def write_with_wrapped_args(f, start, args, indent):
    """Write something like a declaration or call."""
    f.write("\n" + indent)
    f.write(start)
    # For parameter indenting
    delim_pad = ",\n" + indent + (" " * len(start))
    f.write(delim_pad.join(args))
    f.write(")")


def write_decl(f, return_type, function_name, args, indent=""):
    """Write a function declaration/definition with wrapped arguments."""
    f.write("\n" + indent)
    f.write(return_type)
    write_with_wrapped_args(f,
                            "{}(".format(function_name),
                            args,
                            indent)


def write_invocation(f, return_val, function_name, args, indent=""):
    """Write a function call with saved return value and wrapped arguments."""
    write_with_wrapped_args(f,
                            "{} = {}(".format(return_val, function_name),
                            args,
                            indent)


def write_result_handler(f, result, cleanup="", indent=""):
    """Write a check of an xrt_result_t value and early out."""
    f.write("\n" + indent)
    f.write("if (%s != XRT_SUCCESS) {" % result)
    f.write("\n" + indent + "\t")

    if cleanup:
        f.write(cleanup)
        f.write("\n" + indent + "\t")

    f.write("return {};".format(result))
    f.write("\n" + indent + "}\n")


class Arg:
    """An IPC call argument."""

    # Keep all these synchronized with the definitions in the JSON Schema.
    SCALAR_TYPES = set(("uint32_t",
                        "int64_t",
                        "uint64_t"))
    AGGREGATE_RE = re.compile(r"((const )?struct|union) (xrt|ipc)_[a-z_]+")
    ENUM_RE = re.compile(r"enum xrt_[a-z_]+")

    @classmethod
    def parse_array(cls, a):
        """Turn an array of data into an array of Arg objects."""
        return [cls(elm) for elm in a]

    def get_func_argument_in(self):
        """Get the type and name of this argument as an input parameter."""
        if self.is_aggregate:
            return self.typename + " *" + self.name
        else:
            return self.typename + " " + self.name

    def get_func_argument_out(self):
        """Get the type and name of this argument as an output parameter."""
        return self.typename + " *out_" + self.name

    def get_struct_field(self):
        """Get the type and name of this argument as a struct field."""
        return self.typename + " " + self.name

    def dump(self):
        """Dump human-readable output to standard out."""
        print("\t\t" + self.typename + ": " + self.name)

    def __init__(self, data):
        """Construct an argument."""
        self.name = data['name']
        self.typename = data['type']
        self.is_standard_scalar = False
        self.is_aggregate = False
        self.is_enum = False
        if self.typename in self.SCALAR_TYPES:
            self.is_standard_scalar = True
        elif self.AGGREGATE_RE.match(self.typename):
            self.is_aggregate = True
        elif self.ENUM_RE.match(self.typename):
            self.is_enum = True
        else:
            raise RuntimeError("Could not process type name: " + self.typename)


class HandleType:
    """A native handle type requiring special treatment."""

    # Keep this synchronized with the definition in the JSON Schema.
    HANDLE_RE = re.compile(r"xrt_([a-z_]+)_handle_t")

    def __init__(self, config_dict):
        """Construct from dict, originating in JSON."""
        match = self.HANDLE_RE.match(config_dict.get("type", ""))
        if not match:
            raise RuntimeError(
                "Could not match handle regex to type of in/out handle! "
                + str(config_dict))
        self.typename = match.group(0)
        self.stem = match.group(1)
        self.argstem = 'handles'

    def __str__(self):
        """Convert to string by returning the type name."""
        return self.typename

    @property
    def arg_name(self):
        """Get the argument name."""
        return self.argstem

    @property
    def count_arg_name(self):
        """Get the name of the count argument."""
        return 'num_'+self.argstem

    @property
    def count_arg_type(self):
        """Get the type of the count argument."""
        return "uint32_t"

    @property
    def arg_names(self):
        """Get the argument names for the client proxy."""
        return (self.arg_name,
                self.count_arg_name)

    @property
    def arg_decls(self):
        """Get the argument declarations for the client proxy."""
        types = (self.typename + ' *',
                 self.count_arg_type + ' ')
        return (x + y for x, y in zip(types, self.arg_names))

    @property
    def const_arg_decls(self):
        """Get the const argument declarations for the client proxy."""
        return ("const {}".format(x) for x in self.arg_decls)

    @property
    def handler_arg_names(self):
        """Get the argument names for the server handler."""
        return ('max_' + self.count_arg_name,
                'out_' + self.arg_name,
                'out_' + self.count_arg_name)

    @property
    def handler_arg_decls(self):
        """Get the argument declarations for the server handler."""
        types = (self.count_arg_type + ' ',
                 self.typename + ' *',
                 self.count_arg_type + ' *')
        return (x + y for x, y in zip(types, self.handler_arg_names))


class Call:
    """A single IPC call."""

    def dump(self):
        """Dump human-readable output to standard out."""
        print("Call " + self.name)
        if self.in_args:
            print("\tIn:")
            for arg in self.in_args:
                arg.dump()
        if self.out_args:
            print("\tOut:")
            for arg in self.out_args:
                arg.dump()

    def write_call_decl(self, f):
        """Write declaration of ipc_call_CALLNAME."""
        args = ["struct ipc_connection *ipc_c"]
        args.extend(arg.get_func_argument_in() for arg in self.in_args)
        if self.in_handles:
            args.extend(self.in_handles.const_arg_decls)
        args.extend(arg.get_func_argument_out() for arg in self.out_args)
        if self.out_handles:
            args.extend(self.out_handles.arg_decls)
        write_decl(f, 'xrt_result_t', 'ipc_call_' + self.name, args)

    def write_handler_decl(self, f):
        """Write declaration of ipc_handle_CALLNAME."""
        args = ["volatile struct ipc_client_state *ics"]
        args.extend(arg.get_func_argument_in() for arg in self.in_args)
        args.extend(arg.get_func_argument_out() for arg in self.out_args)
        if self.out_handles:
            args.extend(self.out_handles.handler_arg_decls)
        if self.in_handles:
            args.extend(self.in_handles.const_arg_decls)
        write_decl(f, 'xrt_result_t', 'ipc_handle_' + self.name, args)

    @property
    def needs_msg_struct(self):
        """Decide whether this call needs a msg struct."""
        return self.in_args or self.in_handles

    def __init__(self, name, data):
        """Construct a call from call name and call data dictionary."""
        self.id = None
        self.name = name
        self.in_args = []
        self.out_args = []
        self.in_handles = None
        self.out_handles = None
        for key, val in data.items():
            if key == 'id':
                self.id = val
            elif key == 'in':
                self.in_args = Arg.parse_array(val)
            elif key == 'out':
                self.out_args = Arg.parse_array(val)
            elif key == 'out_handles':
                self.out_handles = HandleType(val)
            elif key == 'in_handles':
                self.in_handles = HandleType(val)
            else:
                raise RuntimeError("Unrecognized key")
        if not self.id:
            self.id = "IPC_" + name.upper()


class Proto:
    """An IPC protocol containing one or more calls."""

    @classmethod
    def parse(cls, data):
        """Parse a dictionary defining a protocol into Call objects."""
        return cls(data)

    @classmethod
    def load_and_parse(cls, file):
        """Load a JSON file and parse it into Call objects."""
        with open(file) as infile:
            return cls.parse(json.loads(infile.read()))

    def dump(self):
        """Dump human-readable output to standard out."""
        for call in self.calls:
            call.dump()

    def __init__(self, data):
        """Construct a protocol from a dictionary of calls."""
        self.calls = [Call(name, call) for name, call
                      in data.items()
                      if not name.startswith("$")]
