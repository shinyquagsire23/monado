// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Camera based hand tracking sorting implementation.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_ht
 */

#pragma once

#include <math.h>
#include <vector>
#include <algorithm>
#include <iostream>
// Other thing: sort by speed? like, if our thing must have suddenly changed directions, add to error?
// Easy enough to do using more complicated structs.
// Like a past thing with position, velocity and timestamp - present thing with position and timestamp.

// typedef bool booool;

struct psort_atom_t
{
	size_t idx_1;
	size_t idx_2;
	float err;
};


bool
comp_err(psort_atom_t one, psort_atom_t two)
{
	return (one.err < two.err);
}


template <typename Tp_1, typename Tp_2>
void
naive_sort_permutation_by_error(
    // Inputs - shall be initialized with real data before calling. This function shall not modify them in any way.
    std::vector<Tp_1> &in_1,
    std::vector<Tp_2> &in_2,

    // Outputs - shall be uninitialized. This function shall initialize them to the right size and fill them with the
    // proper values.
    std::vector<bool> &used_1,
    std::vector<bool> &used_2,
    std::vector<size_t> &out_indices_1,
    std::vector<size_t> &out_indices_2,
    std::vector<float> &out_errs,

    float (*calc_error)(const Tp_1 &one, const Tp_2 &two),
    float max_err = std::numeric_limits<float>::max())
{
	used_1 = std::vector<bool>(in_1.size()); // silly? Unsure.
	used_2 = std::vector<bool>(in_2.size());

	size_t out_size = std::min(in_1.size(), in_2.size());

	out_indices_1.reserve(out_size);
	out_indices_2.reserve(out_size);

	std::vector<psort_atom_t> associations;

	for (size_t idx_1 = 0; idx_1 < in_1.size(); idx_1++) {
		for (size_t idx_2 = 0; idx_2 < in_2.size(); idx_2++) {
			float err = calc_error(in_1[idx_1], in_2[idx_2]);
			if (err > 0.0f) {
				// Negative error means the error calculator thought there was something so bad with
				// these that they shouldn't be considered at all.
				associations.push_back({idx_1, idx_2, err});
			}
		}
	}

	std::sort(associations.begin(), associations.end(), comp_err);

	for (size_t i = 0; i < associations.size(); i++) {
		psort_atom_t chonk = associations[i];
		if (used_1[chonk.idx_1] || used_2[chonk.idx_2] || (chonk.err > max_err)) {
			continue;
		}
		used_1[chonk.idx_1] = true;
		used_2[chonk.idx_2] = true;

		out_indices_1.push_back(chonk.idx_1);
		out_indices_2.push_back(chonk.idx_2);

		out_errs.push_back(chonk.err);
	}
}
