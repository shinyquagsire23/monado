// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Pretty printing functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include "xrt/xrt_compiler.h"

struct oxr_sink_logger;


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Prints a fov to a @ref oxr_sink_logger, on the form of `\n\t${name}->fov ...`.
 *
 * Note no trailing line break and but a starting line break, see @ref aux_pretty.
 *
 * @ingroup oxr_main
 */
void
oxr_pp_fov_indented_as_object(struct oxr_sink_logger *slog, const struct xrt_fov *fov, const char *name);

/*!
 * Prints a pose to a @ref oxr_sink_logger, on the form of `\n\t${name}->pose ...`.
 *
 * Note no trailing line break and but a starting line break, see @ref aux_pretty.
 *
 * @ingroup oxr_main
 */
void
oxr_pp_pose_indented_as_object(struct oxr_sink_logger *slog, const struct xrt_pose *pose, const char *name);

/*!
 * Prints a space to a @ref oxr_sink_logger, on the form of `\n\t${name}-><field> ...`.
 *
 * Note no trailing line break and but a starting line break, see @ref aux_pretty.
 *
 * @ingroup oxr_main
 */
void
oxr_pp_space_indented(struct oxr_sink_logger *slog, const struct oxr_space *spc, const char *name);

/*!
 * Prints a space to a @ref oxr_sink_logger, on the form of `\n\t${name}-><field> ...`.
 *
 * Note no trailing line break and but a starting line break, see @ref aux_pretty.
 *
 * @ingroup oxr_main
 */
void
oxr_pp_relation_indented(struct oxr_sink_logger *slog, const struct xrt_space_relation *relation, const char *name);


#ifdef __cplusplus
}
#endif
