#!/usr/bin/env python3
# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0
"""Generate code from a JSON file describing the IPC protocol."""

from ipcproto.common import Proto, write_invocation
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

        func = 'ipc_client_send_and_get_reply'
        args = ['ipc_c', '&_msg', 'sizeof(_msg)', '&_reply', 'sizeof(_reply)']
        if call.out_fds:
            func += '_fds'
            args.extend(('fds', 'num_fds'))
        write_invocation(f, 'xrt_result_t ret', func, args, indent="\t")
        f.write(';')
        f.write('''
\tif (ret != XRT_SUCCESS) {
\t\treturn ret;
\t}
\n''')
        for arg in call.out_args:
            f.write("\t*out_" + arg.name + " = _reply." + arg.name + ";\n")
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
#include "ipc_protocol.h"
#include "ipc_server.h"
#include "ipc_server_utils.h"
#include "ipc_server_generated.h"


// clang-format off

#define MAX_FDS 16
''')

    f.write('''
xrt_result_t
ipc_dispatch(volatile struct ipc_client_state *cs, ipc_command_t *ipc_command)
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
        if call.out_fds:
            f.write("\t\tint fds[MAX_FDS] = {0};\n")
            f.write("\t\tsize_t num_fds = {0};\n")
        f.write("\n")

        # Write call to ipc_handle_CALLNAME
        args = ["cs"]
        for arg in call.in_args:
            args.append(("&msg->" + arg.name)
                        if arg.is_aggregate
                        else ("msg->" + arg.name))
        args.extend("&reply." + arg.name for arg in call.out_args)
        if call.out_fds:
            args.extend(("MAX_FDS",
                         "fds",
                         "&num_fds",))
        write_invocation(f, 'reply.result', 'ipc_handle_' +
                         call.name, args, indent="\t\t")
        f.write(";\n")

        if call.out_fds:
            f.write(
                "\t\t"
                "return ipc_reply_fds(cs->ipc_socket_fd, "
                "&reply, sizeof(reply), "
                "fds, num_fds);\n")
        else:
            f.write(
                "\t\t"
                "return ipc_reply(cs->ipc_socket_fd, "
                "&reply, sizeof(reply));\n")
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
ipc_dispatch(volatile struct ipc_client_state *cs, ipc_command_t *ipc_command);
''')

    for call in p.calls:
        call.write_handle_decl(f)
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
