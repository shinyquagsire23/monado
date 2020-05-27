#!/usr/bin/env python3
# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

import json
import argparse


class Arg:
    @classmethod
    def parseArray(cls, a):
        ret = []
        for elm in a:
            ret.append(cls(elm))
        return ret

    def getFuncArgumentIn(self):
        if self.isAggregate:
            return self.typename + " *" + self.name
        else:
            return self.typename + " " + self.name

    def getFuncArgumentOut(self):
        return self.typename + " *out_" + self.name

    def getStructField(self):
        return self.typename + " " + self.name

    def dump(self):
        print("\t\t" + self.typename + ": " + self.name)

    def __init__(self, data):
        self.name = data['name']
        self.typename = data['type']
        self.isAggregate = False
        if self.typename.find("struct ") == 0:
            self.isAggregate = True
        if self.typename.find("union ") == 0:
            self.isAggregate = True


class Call:

    def dump(self):
        print("Call " + self.name)
        if self.inArgs:
            print("\tIn:")
            for arg in self.inArgs:
                arg.dump()
        if self.outArgs:
            print("\tOut:")
            for arg in self.outArgs:
                arg.dump()

    def writeCallDecl(self, f):
        f.write("\nipc_result_t\n")
        start = "ipc_call_" + self.name + "("
        pad = ""
        for c in start:
            pad = pad + " "

        f.write(start + "struct ipc_connection *ipc_c")
        for arg in self.inArgs:
            f.write(",\n" + pad + arg.getFuncArgumentIn())
        for arg in self.outArgs:
            f.write(",\n" + pad + arg.getFuncArgumentOut())
        if self.outFds:
            f.write(",\n" + pad + "int *fds")
            f.write(",\n" + pad + "size_t num_fds")
        f.write(")")

    def writeHandleDecl(self, f):
        f.write("\nipc_result_t\n")
        start = "ipc_handle_" + self.name + "("
        pad = ""
        for c in start:
            pad = pad + " "

        f.write(start + "volatile struct ipc_client_state *cs")
        for arg in self.inArgs:
            f.write(",\n" + pad + arg.getFuncArgumentIn())
        for arg in self.outArgs:
            f.write(",\n" + pad + arg.getFuncArgumentOut())
        if self.outFds:
            f.write(",\n" + pad + "size_t max_num_fds")
            f.write(",\n" + pad + "int *out_fds")
            f.write(",\n" + pad + "size_t *out_num_fds")
        f.write(")")

    def __init__(self, name, data):

        self.id = None
        self.name = name
        self.inArgs = []
        self.outArgs = []
        self.outFds = False
        for key in data:
            if key == 'id':
                self.id = data[key]
            if key == 'in':
                self.inArgs = Arg.parseArray(data[key])
            if key == 'out':
                self.outArgs = Arg.parseArray(data[key])
            if key == 'out_fds':
                self.outFds = data[key]
        if not self.id:
            self.id = "IPC_" + name.upper()


class Proto:
    @classmethod
    def parse(cls, data):
        return cls(data)

    @classmethod
    def loadAndParse(cls, file):
        with open(file) as infile:
            return cls.parse(json.loads(infile.read()))

    def dump(self):
        for call in self.calls:
            call.dump()

    def addCall(self, name, data):
        self.calls.append(Call(name, data))

    def __init__(self, data):
        self.calls = []
        for name, call in data.items():
            self.addCall(name, call)


header = '''// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  {brief}.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc{suffix}
 */
'''


def doH(file, p):
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
\tipc_result_t result;
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
        if call.inArgs:
            f.write("\nstruct ipc_" + call.name + "_msg\n")
            f.write("{\n")
            f.write("\tenum ipc_command cmd;\n")
            for arg in call.inArgs:
                f.write("\t" + arg.getStructField() + ";\n")
            f.write("};\n")
        # Should we emit a reply struct.
        if call.outArgs:
            f.write("\nstruct ipc_" + call.name + "_reply\n")
            f.write("{\n")
            f.write("\tipc_result_t result;\n")
            for arg in call.outArgs:
                f.write("\t" + arg.getStructField() + ";\n")
            f.write("};\n")

    f.write("\n// clang-format on\n")
    f.close()


def doClientC(file, p):
    f = open(file, "w")
    f.write(header.format(brief='Generated IPC client code', suffix='_client'))
    f.write('''
#include "ipc_client.h"


// clang-format off
\n''')

    # Loop over all of the calls.
    for call in p.calls:
        call.writeCallDecl(f)
        f.write("\n{\n")

        # Message struct
        if call.inArgs:
            f.write("\tstruct ipc_" + call.name + "_msg _msg = {\n")
        else:
            f.write("\tstruct ipc_command_msg _msg = {\n")
        f.write("\t    .cmd = " + str(call.id) + ",\n")
        for arg in call.inArgs:
            if arg.isAggregate:
                f.write("\t    ." + arg.name + " = *" + arg.name + ",\n")
            else:
                f.write("\t    ." + arg.name + " = " + arg.name + ",\n")
        f.write("\t};\n")

        # Reply struct
        if call.outArgs:
            f.write("\tstruct ipc_" + call.name + "_reply _reply;\n")
        else:
            f.write("\tstruct ipc_result_reply _reply = {0};\n")

        f.write('''
\tipc_result_t ret = ipc_client_send_and_get_reply''')
        if call.outFds:
            f.write('''_fds(
\t    ipc_c, &_msg, sizeof(_msg), &_reply, sizeof(_reply), fds, num_fds);''')
        else:
            f.write('''(
\t    ipc_c, &_msg, sizeof(_msg), &_reply, sizeof(_reply));''')
        f.write('''
\tif (ret != IPC_SUCCESS) {
\t\treturn ret;
\t}
\n''')
        for arg in call.outArgs:
            f.write("\t*out_" + arg.name + " = _reply." + arg.name + ";\n")
        f.write("\n\treturn _reply.result;\n}\n")
    f.write("\n// clang-format off\n")
    f.close()


def doClientH(file, p):
    f = open(file, "w")
    f.write(header.format(brief='Generated IPC client code', suffix='_client'))
    f.write('''
#pragma once

#include "ipc_protocol.h"
#include "ipc_client.h"


// clang-format off

''')

    for call in p.calls:
        call.writeCallDecl(f)
        f.write(";\n")
    f.write("\n// clang-format on\n")
    f.close()


def doServerC(file, p):
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
int
ipc_dispatch(volatile struct ipc_client_state *cs, ipc_command_t *ipc_command)
{
\tswitch (*ipc_command) {
''')

    for call in p.calls:
        f.write("\tcase " + call.id + ": {\n")
        if call.inArgs:
            f.write("\t\tstruct ipc_" + call.name + "_msg *msg =\n")
            f.write("\t\t    (struct ipc_" + call.name + "_msg *)ipc_command;\n")
        if call.outArgs:
            f.write("\t\tstruct ipc_" + call.name + "_reply reply = {0};\n")
        else:
            f.write("\t\tstruct ipc_result_reply reply = {0};\n")
        if call.outFds:
            f.write("\t\tint fds[MAX_FDS] = {0};\n")
            f.write("\t\tsize_t num_fds = {0};\n")
        f.write("\n")
        start = "reply.result = ipc_handle_" + call.name + "("
        pad = ""
        for c in start:
            pad = pad + " "
        f.write("\t\t" + start + "cs")
        for arg in call.inArgs:
            if arg.isAggregate:
                f.write(",\n\t\t" + pad + "&msg->" + arg.name)
            else:
                f.write(",\n\t\t" + pad + "msg->" + arg.name)
        for arg in call.outArgs:
            f.write(",\n\t\t" + pad + "&reply." + arg.name)
        if call.outFds:
            f.write(",\n\t\t" + pad + "MAX_FDS")
            f.write(",\n\t\t" + pad + "fds")
            f.write(",\n\t\t" + pad + "&num_fds")
        f.write(");\n")

        if call.outFds:
            f.write("\t\treturn ipc_reply_fds(cs->ipc_socket_fd, &reply, sizeof(reply), fds, num_fds);\n")
        else:
            f.write("\t\treturn ipc_reply(cs->ipc_socket_fd, &reply, sizeof(reply));\n")
        f.write("\t}\n")
    f.write('''\tdefault:
\t\tprintf("UNHANDLED IPC MESSAGE! %d\\n", *ipc_command);
\t\treturn -1;
\t}
}

// clang-format on
''')
    f.close()


def doServerH(file, p):
    f = open(file, "w")
    f.write(header.format(brief='Generated IPC server code', suffix='_server'))
    f.write('''
#pragma once

#include "ipc_protocol.h"
#include "ipc_server.h"


// clang-format off

int
ipc_dispatch(volatile struct ipc_client_state *cs, ipc_command_t *ipc_command);
''')

    for call in p.calls:
        call.writeHandleDecl(f)
        f.write(";\n")
    f.write("\n// clang-format on\n")
    f.close()


def main():
    parser = argparse.ArgumentParser(description='Protocol generator.')
    parser.add_argument('proto', help='Protocol file to use')
    parser.add_argument('output', type=str, nargs='+',
                        help='Output file, uses the ending to figure out what file it should generate')
    args = parser.parse_args()

    p = Proto.loadAndParse(args.proto)

    for output in args.output:
        if output.endswith("ipc_protocol_generated.h"):
            doH(output, p)
        if output.endswith("ipc_client_generated.c"):
            doClientC(output, p)
        if output.endswith("ipc_client_generated.h"):
            doClientH(output, p)
        if output.endswith("ipc_server_generated.c"):
            doServerC(output, p)
        if output.endswith("ipc_server_generated.h"):
            doServerH(output, p)


if __name__ == "__main__":
    main()
