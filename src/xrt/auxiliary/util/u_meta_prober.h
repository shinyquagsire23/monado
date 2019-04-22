// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Simplistic meta prober that wraps multiple probers.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#pragma once

#include <stdlib.h>
#include "xrt/xrt_prober.h"

#ifdef XRT_BUILD_OHMD
#include "ohmd/oh_interface.h"
#endif

#ifdef XRT_BUILD_HDK
#include "hdk/hdk_interface.h"
#endif


#ifdef __cplusplus
extern "C" {
#endif


typedef struct xrt_auto_prober *(*prober_creator)();

static const prober_creator DRIVERS[] = {
#ifdef XRT_BUILD_HDK
    // Returns NULL if none found, so OK to go first.
    hdk_create_auto_prober,
#endif

#ifdef XRT_BUILD_OHMD
    oh_create_auto_prober,
#endif

};

#define NUM_PROBERS (ARRAY_SIZE(DRIVERS))

/*!
 * An xrt_auto_prober that contains other xrt_auto_probers.
 */
struct u_meta_prober
{
	struct xrt_auto_prober base;
	struct xrt_auto_prober *probers[NUM_PROBERS];
};


static inline struct u_meta_prober *
u_meta_prober(struct xrt_auto_prober *p)
{
	return (struct u_meta_prober *)p;
}

static void
u_meta_prober_destroy(struct xrt_auto_prober *p)
{
	struct u_meta_prober *mp = u_meta_prober(p);

	for (size_t i = 0; i < NUM_PROBERS; i++) {
		if (mp->probers[i]) {
			mp->probers[i]->destroy(mp->probers[i]);
			mp->probers[i] = NULL;
		}
	}

	free(p);
}

static struct xrt_device *
u_meta_prober_autoprobe(struct xrt_auto_prober *p)
{
	struct u_meta_prober *mp = u_meta_prober(p);

	for (size_t i = 0; i < NUM_PROBERS; i++) {
		if (mp->probers[i]) {
			struct xrt_device *ret =
			    mp->probers[i]->lelo_dallas_autoprobe(
			        mp->probers[i]);
			if (ret) {
				return ret;
			}
		}
	}

	/* Couldn't find any prober that works. */
	return NULL;
}

static struct xrt_auto_prober *
u_meta_prober_create()
{
	struct u_meta_prober *p =
	    (struct u_meta_prober *)calloc(1, sizeof(struct u_meta_prober));

	for (size_t i = 0; i < NUM_PROBERS; i++) {
		p->probers[i] = DRIVERS[i]();
	}

	p->base.lelo_dallas_autoprobe = u_meta_prober_autoprobe;
	p->base.destroy = u_meta_prober_destroy;

	return &p->base;
}


#ifdef __cplusplus
}
#endif
