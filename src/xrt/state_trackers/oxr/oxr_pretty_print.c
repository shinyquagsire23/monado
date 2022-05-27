// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Pretty printing functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include "oxr_objects.h" // For now needs to come before oxr_logger.h
#include "oxr_logger.h"
#include "oxr_input_transform.h"
#include "oxr_pretty_print.h"


/*
 *
 * helper functions.
 *
 */

static void
print_vec3_field(struct oxr_sink_logger *slog, const struct xrt_vec3 *v, const char *name, const char *field)
{
	oxr_slog(slog, "\n\t%s.%s: (%f, %f, %f)", name, field, v->x, v->y, v->z);
}

static void
print_pose_field(struct oxr_sink_logger *slog, const struct xrt_pose *pose, const char *name, const char *field)
{
	const struct xrt_vec3 *p = &pose->position;
	const struct xrt_quat *q = &pose->orientation;

	oxr_slog(slog, "\n\t%s.%s: (%f, %f, %f) (%f, %f, %f, %f)", name, field, p->x, p->y, p->z, q->x, q->y, q->z,
	         q->w);
}


/*
 *
 * 'Exported' functions.
 *
 */

void
oxr_pp_fov_indented_as_object(struct oxr_sink_logger *slog, const struct xrt_fov *fov, const char *name)
{
	oxr_slog(slog, "\n\t%s.fov: (%f, %f, %f, %f)", name, fov->angle_left, fov->angle_right, fov->angle_up,
	         fov->angle_down);
}

void
oxr_pp_pose_indented_as_object(struct oxr_sink_logger *slog, const struct xrt_pose *pose, const char *name)
{
	print_pose_field(slog, pose, name, "pose");
}

void
oxr_pp_space_indented(struct oxr_sink_logger *slog, const struct oxr_space *spc, const char *name)
{
	oxr_slog(slog, "\n\t%s.type: ", name);

	switch (spc->space_type) {
	case OXR_SPACE_TYPE_ACTION: {
		struct oxr_action_input *input = NULL;
		oxr_action_get_pose_input(spc->sess, spc->act_key, &spc->subaction_paths, &input);
		if (input == NULL) {
			oxr_slog(slog, "action (inactive)");
			break;
		}
		oxr_slog(slog, "action ('%s', ", input->xdev->str);
		u_pp_xrt_input_name(oxr_slog_dg(slog), input->input->name);
		oxr_slog(slog, ")");
	} break;
	case OXR_SPACE_TYPE_REFERENCE_VIEW: oxr_slog(slog, "view"); break;
	case OXR_SPACE_TYPE_REFERENCE_LOCAL: oxr_slog(slog, "local"); break;
	case OXR_SPACE_TYPE_REFERENCE_STAGE: oxr_slog(slog, "stage"); break;
	case OXR_SPACE_TYPE_REFERENCE_UNBOUNDED_MSFT: oxr_slog(slog, "unbounded"); break;
	case OXR_SPACE_TYPE_REFERENCE_COMBINED_EYE_VARJO: oxr_slog(slog, "combined_eye"); break;
	default: oxr_slog(slog, "unknown_space"); break;
	}

	print_pose_field(slog, &spc->pose, name, "offset");
}

void
oxr_pp_relation_indented(struct oxr_sink_logger *slog, const struct xrt_space_relation *relation, const char *name)
{
	print_pose_field(slog, &relation->pose, name, "pose");
	if ((relation->relation_flags & XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT) != 0) {
		print_vec3_field(slog, &relation->linear_velocity, name, "linear_velocity");
	}
	if ((relation->relation_flags & XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT) != 0) {
		print_vec3_field(slog, &relation->angular_velocity, name, "angluar_velocity");
	}
}
