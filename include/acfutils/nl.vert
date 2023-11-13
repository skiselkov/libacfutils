/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license in the file COPYING
 * or http://www.opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file COPYING.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2019 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACF_UTILS_NL_VERT_
#define	_ACF_UTILS_NL_VERT_

#ifndef	LACF_NL_VERT_UNIFORM_OFFSET
#define	LACF_NL_VERT_UNIFORM_OFFSET	10
#endif
#ifndef	LACF_NL_VERT_INPUT_OFFSET
#define	LACF_NL_VERT_INPUT_OFFSET	0
#endif

layout(location = LACF_NL_VERT_UNIFORM_OFFSET + 0)
    uniform vec2	_nl_vp;
layout(location = LACF_NL_VERT_UNIFORM_OFFSET + 1)
    uniform float	_nl_semi_width;

layout(location = LACF_NL_VERT_INPUT_OFFSET + 0) in vec3	_nl_seg_here;
layout(location = LACF_NL_VERT_INPUT_OFFSET + 1) in vec3	_nl_seg_start;
layout(location = LACF_NL_VERT_INPUT_OFFSET + 2) in vec3	_nl_seg_end;

mat3
nl_mat3_rotate_dir(mat3 m, vec2 dir)
{
	mat3 rotate_mat = mat3(
	    dir.y, -dir.x, 0.0,
	    dir.x, dir.y, 0.0,
	    0.0, 0.0, 1.0
	);
	return (rotate_mat * m);
}

vec2
nl_vec3_norm_right(vec2 v)
{
	return (vec2(v.y, -v.x));
}

vec4
nl_vert_main(const mat4 pvm_in)
{
	const float offsets[4] = float[4](1.0, -1.0, -1.0, 1.0);
	float offset_pix = offsets[gl_VertexID & 3] * _nl_semi_width;
	vec4 pos_start = pvm_in * vec4(_nl_seg_start, 1.0);
	vec4 pos_end = pvm_in * vec4(_nl_seg_end, 1.0);
	vec2 pos_start_ndc = pos_start.xy / pos_start.w;
	vec2 pos_end_ndc = pos_end.xy / pos_end.w;
	vec2 s2e_dir = normalize(pos_end_ndc - pos_start_ndc);
	/* NDC space is 2.0 units wide/high, not 1.0 */
	vec2 scr_ratio = vec2(2.0) / _nl_vp;
	vec4 scr_pos = pvm_in * vec4(_nl_seg_here, 1.0);
	vec2 offset = nl_vec3_norm_right(s2e_dir) * offset_pix * scr_ratio *
	    scr_pos.w;
	return (scr_pos + vec4(offset, 0.0, 0.0));
}

vec3
nl_get_vtx_pos(void)
{
	return (_nl_seg_here);
}

#endif	/* _ACF_UTILS_NL_VERT_ */
