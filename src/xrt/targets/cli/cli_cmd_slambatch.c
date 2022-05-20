// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  EuRoC datasets batch evaluation tool
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 */

#include "euroc/euroc_interface.h"
#include "os/os_threading.h"
#include "util/u_logging.h"
#include "xrt/xrt_config_build.h"
#include "xrt/xrt_config_have.h"
#include "xrt/xrt_config_drivers.h"

#include <stdio.h>

#define P(...) fprintf(stderr, __VA_ARGS__)
#define I(...) U_LOG(U_LOGGING_INFO, __VA_ARGS__)

#if defined(XRT_FEATURE_SLAM) && defined(XRT_BUILD_DRIVER_EUROC)

static bool should_exit = false;

static void *
wait_for_exit_key(void *ptr)
{
	getchar();
	should_exit = true;
	return NULL;
}
#endif

int
cli_cmd_slambatch(int argc, const char **argv)
{

#if !defined(XRT_FEATURE_SLAM)
	P("No SLAM system built.\n");
	return EXIT_FAILURE;
#elif !defined(XRT_BUILD_DRIVER_EUROC)
	P("Euroc driver not built, can't reproduce datasets.\n");
	return EXIT_FAILURE;
#else
	// Do not count "monado-cli" and "slambatch" as args
	int nof_args = argc - 2;
	const char **args = &argv[2];

	if (nof_args == 0 || nof_args % 3 != 0) {
		P("Batch evaluator of SLAM datasets.\n");
		P("Usage: %s %s [<euroc_path> <slam_config> <output_path>]...\n", argv[0], argv[1]);
		return EXIT_FAILURE;
	}

	// Allow pressing enter to quit the program by launching a new thread
	struct os_thread_helper wfk_thread;
	os_thread_helper_init(&wfk_thread);
	os_thread_helper_start(&wfk_thread, wait_for_exit_key, NULL);

	timepoint_ns start_time = os_monotonic_get_ns();
	int nof_datasets = nof_args / 3;
	for (int i = 0; i < nof_datasets && !should_exit; i++) {
		const char *dataset_path = args[i * 3];
		const char *slam_config = args[i * 3 + 1];
		const char *output_path = args[i * 3 + 2];

		I("Running dataset %d out of %d", i + 1, nof_datasets);
		I("Dataset path: %s", dataset_path);
		I("SLAM config path: %s", slam_config);
		I("Output path: %s", output_path);

		euroc_run_dataset(dataset_path, slam_config, output_path, &should_exit);
	}
	timepoint_ns end_time = os_monotonic_get_ns();

	pthread_cancel(wfk_thread.thread);

	// Destroy also stops the thread.
	os_thread_helper_destroy(&wfk_thread);

	printf("Done in %.2fs.\n", (double)(end_time - start_time) / U_TIME_1S_IN_NS);
#endif
	return EXIT_SUCCESS;
}
