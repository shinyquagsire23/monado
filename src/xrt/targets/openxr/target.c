// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  The thing that binds all of the OpenXR driver together.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "xrt/xrt_prober.h"

#include <stdlib.h>

#ifdef XRT_HAVE_OHMD
#include "ohmd/oh_interface.h"
#endif

#ifdef XRT_HAVE_HDK
#include "hdk/hdk_interface.h"
#endif


typedef struct xrt_prober *(*prober_creator)();


static const prober_creator DRIVERS[] = {
#ifdef XRT_HAVE_HDK
    // Returns NULL if none found, so OK to go first.
    hdk_create_prober,
#endif

#ifdef XRT_HAVE_OHMD
    oh_create_prober,
#endif

};

#define NUM_PROBERS (ARRAY_SIZE(DRIVERS))

/*!
 * An xrt_prober that contains other xrt_probers.
 */
struct xrt_meta_prober
{
	struct xrt_prober base;
	struct xrt_prober *probers[NUM_PROBERS];
};


static inline struct xrt_meta_prober *
xrt_meta_prober(struct xrt_prober *p)
{
	return (struct xrt_meta_prober *)p;
}

static void
xrt_meta_prober_destroy(struct xrt_prober *p)
{
	struct xrt_meta_prober *mp = xrt_meta_prober(p);
	for (size_t i = 0; i < NUM_PROBERS; i++) {
		if (mp->probers[i]) {
			mp->probers[i]->destroy(mp->probers[i]);
			mp->probers[i] = NULL;
		}
	}

	free(p);
}

static struct xrt_device *
xrt_meta_prober_autoprobe(struct xrt_prober *p)
{
	struct xrt_meta_prober *mp = xrt_meta_prober(p);
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

struct xrt_prober *
xrt_create_prober()
{
	struct xrt_meta_prober *p =
	    (struct xrt_meta_prober *)calloc(1, sizeof(struct xrt_meta_prober));

	for (size_t i = 0; i < NUM_PROBERS; i++) {
		p->probers[i] = DRIVERS[i]();
	}

	p->base.lelo_dallas_autoprobe = xrt_meta_prober_autoprobe;
	p->base.destroy = xrt_meta_prober_destroy;

	return &p->base;
}
