#!/usr/bin/env python3
# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0
"""Generate code from a JSON file describing the IPC protocol."""

from ipcproto.common import Proto, write_invocation, write_result_handler
import argparse

header = '''// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  {brief}.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc{suffix}
 */
'''


def generate_h(file, p):
    """Generate protocol header.

    Defines command enum, utility functions, and command and reply structures.
    """
    f = open(file, "w")
    f.write(header.format(brief='Generated IPC protocol header', suffix=''))
    f.write('''
#pragma once


// clang-format off

struct ipc_connection;
''')

    f.write('''
typedef enum ipc_command
{
\tIPC_ERR = 0,''')
    for call in p.calls:
        f.write("\n\t" + call.id + ",")
    f.write("\n} ipc_command_t;\n")

    f.write('''
struct ipc_command_msg
{
\tenum ipc_command cmd;
};

struct ipc_result_reply
{
\txrt_result_t result;
};

struct ipc_formats_info
{
\tuint64_t formats[IPC_MAX_FORMATS];
\tuint32_t num_formats;
};
''')

    f.write('''
static inline const char *
ipc_cmd_to_str(ipc_command_t id)
{
\tswitch (id) {
\tcase IPC_ERR: return "IPC_ERR";''')
    for call in p.calls:
        f.write("\n\tcase " + call.id + ": return \"" + call.id + "\";")
    f.write("\n\tdefault: return \"IPC_UNKNOWN\";")
    f.write("\n\t}\n}\n")

    for call in p.calls:
        # Should we emit a msg struct.
        if call.in_args:
            f.write("\nstruct ipc_" + call.name + "_msg\n")
            f.write("{\n")
            f.write("\tenum ipc_command cmd;\n")
            for arg in call.in_args:
                f.write("\t" + arg.get_struct_field() + ";\n")
            f.write("};\n")
        # Should we emit a reply struct.
        if call.out_args:
            f.write("\nstruct ipc_" + call.name + "_reply\n")
            f.write("{\n")
            f.write("\txrt_result_t result;\n")
            for arg in call.out_args:
                f.write("\t" + arg.get_struct_field() + ";\n")
            f.write("};\n")

    f.write("\n// clang-format on\n")
    f.close()


def generate_client_c(file, p):
    """Generate IPC client proxy source."""
    f = open(file, "w")
    f.write(header.format(brief='Generated IPC client code', suffix='_client'))
    f.write('''
#include "ipc_client.h"
#include "ipc_protocol_generated.h"


// clang-format off
\n''')

    # Loop over all of the calls.
    for call in p.calls:
        call.write_call_decl(f)
        f.write("\n{\n")

        # Message struct
        if call.in_args:
            f.write("\tstruct ipc_" + call.name + "_msg _msg = {\n")
        else:
            f.write("\tstruct ipc_command_msg _msg = {\n")
        f.write("\t    .cmd = " + str(call.id) + ",\n")
        for arg in call.in_args:
            if arg.is_aggregate:
                f.write("\t    ." + arg.name + " = *" + arg.name + ",\n")
            else:
                f.write("\t    ." + arg.name + " = " + arg.name + ",\n")
        f.write("\t};\n")

        # Reply struct
        if call.out_args:
            f.write("\tstruct ipc_" + call.name + "_reply _reply;\n")
        else:
            f.write("\tstruct ipc_result_reply _reply = {0};\n")

        f.write("""
\t// Other threads must not read/write the fd while we wait for reply
\tos_mutex_lock(&ipc_c->mutex);
""")
        cleanup = "os_mutex_unlock(&ipc_c->mutex);"
        func = 'ipc_send'
        args = ['&ipc_c->imc', '&_msg', 'sizeof(_msg)']
        write_invocation(f, 'xrt_result_t ret', func, args, indent="\t")
        f.write(';')
        write_result_handler(f, 'ret', cleanup, indent="\t")

        func = 'ipc_receive'
        args = ['&ipc_c->imc', '&_reply', 'sizeof(_reply)']
        if call.out_handles:
            func += '_handles_' + call.out_handles.stem
            args.extend(call.out_handles.arg_names)
        write_invocation(f, 'ret', func, args, indent="\t")
        f.write(';')
        write_result_handler(f, 'ret', cleanup, indent="\t")

        for arg in call.out_args:
            f.write("\t*out_" + arg.name + " = _reply." + arg.name + ";\n")
        f.write("\n\t" + cleanup)
        f.write("\n\treturn _reply.result;\n}\n")
    f.write("\n// clang-format off\n")
    f.close()


def generate_client_h(file, p):
    """Generate IPC client header.

    Contains prototypes for generated IPC proxy call functions.
    """
    f = open(file, "w")
    f.write(header.format(brief='Generated IPC client code', suffix='_client'))
    f.write('''
#pragma once

#include "ipc_protocol.h"
#include "ipc_protocol_generated.h"
#include "ipc_client.h"


// clang-format off

''')

    for call in p.calls:
        call.write_call_decl(f)
        f.write(";\n")
    f.write("\n// clang-format on\n")
    f.close()


def generate_server_c(file, p):
    """Generate IPC server stub/dispatch source."""
    f = open(file, "w")
    f.write(header.format(brief='Generated IPC server code', suffix='_server'))
    f.write('''
#include "ipc_server_generated.h"

#include "ipc_protocol.h"
#include "ipc_server.h"
#include "ipc_utils.h"


// clang-format off

#define MAX_HANDLES 16
''')

    f.write('''
xrt_result_t
ipc_dispatch(volatile struct ipc_client_state *ics, ipc_command_t *ipc_command)
{
\tswitch (*ipc_command) {
''')

    for call in p.calls:
        f.write("\tcase " + call.id + ": {\n")
        if call.in_args:
            f.write(
                "\t\tstruct ipc_{}_msg *msg =\n".format(call.name))
            f.write(
                "\t\t    (struct ipc_{}_msg *)ipc_command;\n".format(
                    call.name))
        if call.out_args:
            f.write("\t\tstruct ipc_%s_reply reply = {0};\n" % call.name)
        else:
            f.write("\t\tstruct ipc_result_reply reply = {0};\n")
        if call.out_handles:
            f.write("\t\t%s %s[MAX_HANDLES] = {0};\n" % (
                call.out_handles.typename, call.out_handles.arg_name))
            f.write("\t\tsize_t %s = {0};\n" % call.out_handles.count_arg_name)
        f.write("\n")

        # Write call to ipc_handle_CALLNAME
        args = ["ics"]
        for arg in call.in_args:
            args.append(("&msg->" + arg.name)
                        if arg.is_aggregate
                        else ("msg->" + arg.name))
        args.extend("&reply." + arg.name for arg in call.out_args)
        if call.out_handles:
            args.extend(("MAX_HANDLES",
                         call.out_handles.arg_name,
                         "&" + call.out_handles.count_arg_name))
        write_invocation(f, 'reply.result', 'ipc_handle_' +
                         call.name, args, indent="\t\t")
        f.write(";\n")

        # TODO do we check reply.result and
        # error out before replying if it's not success?

        func = 'ipc_send'
        args = ["(struct ipc_message_channel *)&ics->imc",
                "&reply",
                "sizeof(reply)"]
        if call.out_handles:
            func += '_handles_' + call.out_handles.stem
            args.extend(call.out_handles.arg_names)
        write_invocation(f, 'xrt_result_t ret', func, args, indent="\t\t")
        f.write(";")
        f.write("\n\t\treturn ret;\n")
        f.write("\t}\n")
    f.write('''\tdefault:
\t\tprintf("UNHANDLED IPC MESSAGE! %d\\n", *ipc_command);
\t\treturn XRT_ERROR_IPC_FAILURE;
\t}
}

// clang-format on
''')
    f.close()


def generate_server_header(file, p):
    """Generate IPC server header.

    Declares handler prototypes to implement,
    as well as the prototype for the generated dispatch function.
    """
    f = open(file, "w")
    f.write(header.format(brief='Generated IPC server code', suffix='_server'))
    f.write('''
#pragma once

#include "ipc_protocol.h"
#include "ipc_protocol_generated.h"
#include "ipc_server.h"


// clang-format off

xrt_result_t
ipc_dispatch(volatile struct ipc_client_state *ics, ipc_command_t *ipc_command);
''')

    for call in p.calls:
        call.write_handler_decl(f)
        f.write(";\n")
    f.write("\n// clang-format on\n")
    f.close()


def main():
    """Handle command line and generate a file."""
    parser = argparse.ArgumentParser(description='Protocol generator.')
    parser.add_argument(
        'proto', help='Protocol file to use')
    parser.add_argument(
        'output', type=str, nargs='+',
        help='Output file, uses the name to choose output type')
    args = parser.parse_args()

    p = Proto.load_and_parse(args.proto)

    for output in args.output:
        if output.endswith("ipc_protocol_generated.h"):
            generate_h(output, p)
        if output.endswith("ipc_client_generated.c"):
            generate_client_c(output, p)
        if output.endswith("ipc_client_generated.h"):
            generate_client_h(output, p)
        if output.endswith("ipc_server_generated.c"):
            generate_server_c(output, p)
        if output.endswith("ipc_server_generated.h"):
            generate_server_header(output, p)


if __name__ == "__main__":
    main()
