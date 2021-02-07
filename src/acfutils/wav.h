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
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACF_UTILS_WAV_H_
#define	_ACF_UTILS_WAV_H_

#include <stdint.h>

#include <al.h>

#include "geom.h"
#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct alc alc_t;

typedef struct wav_fmt_hdr {
	uint16_t	datafmt;	/* PCM = 1 */
	uint16_t	n_channels;
	uint32_t	srate;		/* sample rate in Hz */
	uint32_t	byte_rate;	/* (srate * bps * #channels) / 8 */
	uint16_t	padding;	/* unused */
	uint16_t	bps;		/* bits per sample */
} wav_fmt_hdr_t;

typedef struct wav_s {
	char		*name;
	wav_fmt_hdr_t	fmt;
	double		duration;	/* in seconds */
	alc_t		*alc;
	ALuint		albuf;
	ALuint		alsrc;

	vect3_t		dir;
	double		cone_inner;
	double		cone_outer;
	double		gain_outer;

	vect3_t		pos;
	vect3_t		vel;
	bool_t		loop;
	double		ref_dist;
	double		max_dist;
	double		rolloff_fact;
	float		gain;
	float		pitch;

	uint64_t	play_start;
} wav_t;

API_EXPORT char **openal_list_output_devs(size_t *num_p);
API_EXPORT alc_t *openal_init(const char *devname, bool_t shared);
API_EXPORT alc_t *openal_init2(const char *devname, bool_t shared,
    const int *attrs, bool_t thr_local);
API_EXPORT void openal_fini(alc_t *alc);

API_EXPORT wav_t *wav_load(const char *filename, const char *descr_name,
    alc_t *alc);
API_EXPORT void wav_free(wav_t *wav);

API_EXPORT void wav_set_offset(wav_t *wav, float offset_sec);
API_EXPORT float wav_get_offset(wav_t *wav);
API_EXPORT void wav_set_gain(wav_t *wav, float gain);
API_EXPORT float wav_get_gain(wav_t *wav);
API_EXPORT void wav_set_loop(wav_t *wav, bool_t loop);
API_EXPORT bool_t wav_get_loop(wav_t *wav);
API_EXPORT void wav_set_pitch(wav_t *wav, float pitch);
API_EXPORT float wav_get_pitch(wav_t *wav);
API_EXPORT void wav_set_position(wav_t *wav, vect3_t pos);
API_EXPORT vect3_t wav_get_position(wav_t *wav);
API_EXPORT void wav_set_velocity(wav_t *wav, vect3_t vel);
API_EXPORT vect3_t wav_get_velocity(wav_t *wav);
API_EXPORT void wav_set_ref_dist(wav_t *wav, double d);
API_EXPORT double wav_get_ref_dist(wav_t *wav);
API_EXPORT void wav_set_max_dist(wav_t *wav, double d);
API_EXPORT double wav_get_max_dist(wav_t *wav);
API_EXPORT void wav_set_spatialize(wav_t *wav, bool_t flag);
API_EXPORT void wav_set_rolloff_fact(wav_t *wav, double r);
API_EXPORT double wav_get_rolloff_fact(wav_t *wav);

/* Directional parameters */
API_EXPORT void wav_set_dir(wav_t *wav, vect3_t dir);
API_EXPORT void wav_set_cone_inner(wav_t *wav, double cone_inner);
API_EXPORT void wav_set_cone_outer(wav_t *wav, double cone_outer);
API_EXPORT void wav_set_gain_outer(wav_t *wav, double gain_outer);
API_EXPORT void wav_set_gain_outerhf(wav_t *wav, double gain_outerhf);
API_EXPORT void wav_set_stereo_angles(wav_t *wav, double a1, double a2);

API_EXPORT void wav_set_air_absorption_fact(wav_t *wav, double fact);

API_EXPORT bool_t wav_play(wav_t *wav);
API_EXPORT bool_t wav_is_playing(wav_t *wav);
API_EXPORT void wav_stop(wav_t *wav);

API_EXPORT void alc_set_dist_model(alc_t *alc, ALenum model);
API_EXPORT void alc_listener_set_pos(alc_t *alc, vect3_t pos);
API_EXPORT vect3_t alc_listener_get_pos(alc_t *alc);
API_EXPORT void alc_listener_set_orient(alc_t *alc, vect3_t orient);
API_EXPORT void alc_listener_set_velocity(alc_t *alc, vect3_t vel);

API_EXPORT alc_t *alc_global_save(alc_t *new_alc);
API_EXPORT void alc_global_restore(alc_t *new_alc, alc_t *old_alc);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_WAV_H_ */
