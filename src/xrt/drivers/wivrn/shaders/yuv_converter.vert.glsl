/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#version 450

/*
layout (binding = 0) uniform yuv_ubo {
    vec4 uvs;
} ubo;
*/

layout (location = 0) out vec2 outUV;

void main() {
    int num_slices = (gl_VertexIndex >> 4) & 0xF;
    int slice_idx = (gl_VertexIndex >> 8) & 0xF;
    int vtx_idx = (gl_VertexIndex & 0xF);

    float f_num_slices = float(num_slices);
    float f_slice_idx = float(slice_idx);

    vec2 outUV_base = vec2((vtx_idx << 1) & 2, vtx_idx & 2); // [(0, 0), (2, 0), (0, 2)]
    vec2 outUV_sliced = vec2(outUV_base.x, (outUV_base.y / f_num_slices) + ((1.0/f_num_slices) * f_slice_idx));
    
    outUV = outUV_sliced;
    gl_Position = vec4(outUV_base * 2.0f + -1.0f, 0.0f, 1.0f); // [(-1, -1), (3, -1), (-1, 3)]
}
