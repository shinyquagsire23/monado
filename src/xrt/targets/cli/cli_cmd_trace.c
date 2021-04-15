// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Trace marker parsing and coversion code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "xrt/xrt_compiler.h"

#include "util/u_misc.h"
#include "util/u_trace_marker.h"

#include "cli_common.h"

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <inttypes.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

#define P(...) fprintf(stderr, __VA_ARGS__)

#define TRACE_PIPE_FILENAME "/sys/kernel/tracing/trace_pipe"
#define TRACE_MARKER_FILENAME "/sys/kernel/tracing/trace_marker"
#define BUF_SIZE (1024 * 8)


struct trace
{
	int fd;
	FILE *file;

	bool running;

	char buffer[BUF_SIZE];
};

static struct trace t = {0};


/*
 *
 * JSON writing.
 *
 */

static void
json_w_header()
{
	fprintf(t.file,
	        "{\n"
	        "\t\"displayTimeUnit\": \"ms\",\n"
	        "\t\"traceEvents\": [\n"
	        "\t\t{\n"
	        "\t\t\t\"#\": \"This is to avoid having to deal with ',' all over the code.\"\n"
	        "\t\t}");
}

static void
json_w_end()
{
	fprintf(t.file,
	        "\n"
	        "\t]\n"
	        "}\n");
}


/*
 *
 * Functions.
 *
 */

static int
open_file_fd(const char *filename, int flags)
{
	int fd = open(filename, flags);
	if (fd < 0) {
		fprintf(stderr, " :: Failed to open the file: '%s'\n", filename);
		fprintf(stderr, "    See command help!\n");
	}

	return fd;
}

static int
open_fd()
{
	int check_fd = open_file_fd(TRACE_MARKER_FILENAME, O_WRONLY);
	if (check_fd < 0) {
		return -1;
	}
	close(check_fd);

	fprintf(stderr, " :: Checked '%s'\n", TRACE_MARKER_FILENAME);

	t.fd = open_file_fd(TRACE_PIPE_FILENAME, O_RDONLY);

	fprintf(stderr, " :: Opened '%s'\n", TRACE_PIPE_FILENAME);

	return 0;
}

static void
handle_data(const char *rest_of_line, size_t len)
{
	int type = 0;
	int data_length = 0;
	int consumed = 0;
	int scan = sscanf(rest_of_line, "%i %i %n", &type, &data_length, &consumed);
	char data[1024];

	if ((int)data_length * 2 != (int)len - (int)consumed) {
		return;
	}

	if ((size_t)data_length > sizeof(data)) {
		return;
	}

	rest_of_line = rest_of_line + consumed;
	len = len - (size_t)consumed;


	for (int i = 0; i < data_length; i++) {
		unsigned int tmp = 0;
		scan = sscanf(rest_of_line + (i * 2), "%2x", &tmp);
		if (scan < 1) {
			return;
		}

		data[i] = tmp;
	}

	switch (type) {
	case U_TRACE_DATA_TYPE_TIMING_FRAME: u_ft_write_json(t.file, (void *)data); break;
	case U_TRACE_DATA_TYPE_TIMING_RENDER: u_rt_write_json(t.file, (void *)data); break;
	default: fprintf(stderr, "%.*s\n", (int)len, rest_of_line); break;
	}
}

static void
handle_line(const char *line, size_t len)
{
	int pid = 0, tid = 0;
	int secs = 0, usecs = 0;
	char cmd = '\0';
	int consumed = 0;
	char function[256] = {0};

	int scan = sscanf(                                                      //
	    line,                                                               //
	    "           <...>-%i %*s %*s %d.%d: tracing_mark_write: %1s %i %n", //
	    &tid,                                                               //
	    &secs,                                                              //
	    &usecs,                                                             //
	    &cmd,                                                               //
	    &pid,                                                               //
	    &consumed);                                                         //

	if (scan < 5) {
		fprintf(stderr, "%.*s\n", (int)len, line);
		return;
	}

	switch (cmd) {
	case 'E':
	case 'B':
		scan = sscanf(line + consumed, "%256s", function);
		if (scan < 1) {
			fprintf(stderr, "%.*s\n", (int)len, line);
			return;
		}

		fprintf(t.file,
		        ",\n"
		        "\t\t{\n"
		        "\t\t\t\"ph\": \"%.1s\",\n"
		        "\t\t\t\"name\": \"%s\",\n"
		        "\t\t\t\"cat\": \"%s\",\n"
		        "\t\t\t\"ts\": %u%06u,\n"
		        "\t\t\t\"pid\": %u,\n"
		        "\t\t\t\"tid\": %u\n"
		        "\t\t}",
		        &cmd, function, "func", secs, usecs, pid, tid);
		break;
	case 'r': handle_data(line + consumed, len - (size_t)consumed); break;
	default: fprintf(stderr, "%.*s\n", (int)len, line); break;
	}
}

static int
loop()
{
	while (t.running) {
		ssize_t ret = read(t.fd, t.buffer, BUF_SIZE);
		if (ret < 0) {
			return 1;
		}
		if (ret == 0) {
			continue;
		}

		size_t pos = 0;
		size_t count = 0;
		while (count < (size_t)ret) {
			if (t.buffer[count] != '\n') {
				count++;
				continue;
			}

			assert(t.buffer[pos] != '\n');

			size_t length = count - pos;
			handle_line(t.buffer + pos, length);

			// Point *after* the newline.
			count++;
			pos = count;
		}

		fflush(t.file);
	}

	return 0;
}

void
signal_handler(int signum, siginfo_t *info, void *ptr)
{
	t.running = false;

	// Since we are doing a clean shutdown ^C be on it's own line.
	ssize_t ret = write(STDERR_FILENO, "\n", 1);
	(void)ret; // We are just trying to make the CLI look better.
}

void
catch_sigterm()
{
	static struct sigaction _sigact;

	memset(&_sigact, 0, sizeof(_sigact));
	_sigact.sa_sigaction = signal_handler;
	_sigact.sa_flags = SA_SIGINFO;

	sigaction(SIGTERM, &_sigact, NULL);
	sigaction(SIGINT, &_sigact, NULL);
}

static int
trace_pipe(int argc, const char **argv)
{
	catch_sigterm();

	t.file = stdout;
	t.running = true;

	int ret = open_fd();
	if (ret < 0) {
		return 1;
	}

	json_w_header();

	u_ft_write_json_metadata(t.file);
	u_rt_write_json_metadata(t.file);

	P(" :: Looping\n");

	ret = loop();
	if (ret < 0) {
		return 1;
	}

	json_w_end();

	P(" :: Clean shutdown\n");

	return 0;
}

static void
print_help(int argc, const char **argv)
{
	if (argc >= 3) {
		P("Unknown trace command '%s'\n\n", argv[2]);
	}

	P("Usage %s trace <cmd>\n", argv[0]);
	P("\n");
	P("Commands:\n");
	P("  pipe - Read the trace_pipe stream and convert into json outputted to stdout.\n");
	P("\n");
	P("Example:\n");
	P("  $ %s trace pipe 1> /tmp/chrome_tracing.json\n", argv[0]);
	P("\n");
	P("Make sure your user has access to the files:\n");
	P("  '%s'.\n", TRACE_PIPE_FILENAME);
	P("  '%s'.\n", TRACE_MARKER_FILENAME);
	P("\n");
	P("The reference clocks needs to be the same in Monado and the tracing framework.\n");
	P("  $ echo mono | sudo dd of=/sys/kernel/tracing/trace_clock\n");
	P("This command is very unsecure but will make things work.\n");
	P("  $ sudo chown -R <user>:<user> /sys/kernel/tracing\n");
	P("\n");
	P("See https://lwn.net/Articles/366796/\n");
}

int
cli_cmd_trace(int argc, const char **argv)
{
	if (argc <= 2) {
		print_help(argc, argv);
		return 1;
	}

	if (strcmp(argv[2], "help") == 0) {
		print_help(2, argv);
		return 0;
	}

	if (strcmp(argv[2], "pipe") == 0) {
		return trace_pipe(argc, argv);
	}

	print_help(argc, argv);

	return 1;
}
