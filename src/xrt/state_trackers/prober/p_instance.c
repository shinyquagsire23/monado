

#include "xrt/xrt_prober.h"
#include "xrt/xrt_instance.h"
#include "xrt/xrt_gfx_fd.h"

#include "util/u_misc.h"


/*
 *
 * Struct and helpers.
 *
 */

struct p_instance
{
	struct xrt_instance base;
	struct xrt_prober *xp;
};

static inline struct p_instance *
p_instance(struct xrt_instance *xinst)
{
	return (struct p_instance *)xinst;
}


/*
 *
 * Member functions.
 *
 */

static int
p_instance_select(struct xrt_instance *xinst,
                  struct xrt_device **xdevs,
                  size_t num_xdevs)
{
	struct p_instance *pinst = p_instance(xinst);

	int ret = xrt_prober_probe(pinst->xp);
	if (ret < 0) {
		return ret;
	}

	return xrt_prober_select(pinst->xp, xdevs, num_xdevs);
}

static int
p_instance_create_fd_compositor(struct xrt_instance *xinst,
                                struct xrt_device *xdev,
                                bool flip_y,
                                struct xrt_compositor_fd **out_xcfd)
{
	struct xrt_compositor_fd *xcfd =
	    xrt_gfx_provider_create_fd(xdev, flip_y);

	if (xcfd == NULL) {
		return -1;
	}

	*out_xcfd = xcfd;

	return 0;
}

static int
p_instance_get_prober(struct xrt_instance *xinst, struct xrt_prober **out_xp)
{
	struct p_instance *pinst = p_instance(xinst);

	*out_xp = pinst->xp;

	return 0;
}

static void
p_instance_destroy(struct xrt_instance *xinst)
{
	struct p_instance *pinst = p_instance(xinst);

	xrt_prober_destroy(&pinst->xp);
	free(pinst);
}


/*
 *
 * Exported function(s).
 *
 */

int
xrt_instance_create(struct xrt_instance **out_xinst)
{
	struct xrt_prober *xp = NULL;

	int ret = xrt_prober_create(&xp);
	if (ret < 0) {
		return ret;
	}

	struct p_instance *pinst = U_TYPED_CALLOC(struct p_instance);
	pinst->base.select = p_instance_select;
	pinst->base.create_fd_compositor = p_instance_create_fd_compositor;
	pinst->base.get_prober = p_instance_get_prober;
	pinst->base.destroy = p_instance_destroy;
	pinst->xp = xp;

	*out_xinst = &pinst->base;

	return 0;
}
