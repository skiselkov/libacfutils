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

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <alc.h>
#include <opusfile.h>

#include <acfutils/assert.h>
#include <acfutils/helpers.h>
#include <acfutils/list.h>
#include <acfutils/log.h>
#include <acfutils/riff.h>
#include <acfutils/types.h>
#include <acfutils/wav.h>
#include <acfutils/time.h>

#include "minimp3.h"

#define	WAVE_ID	FOURCC("WAVE")
#define	FMT_ID	FOURCC("fmt ")
#define	DATA_ID	FOURCC("data")

#define	READ_BUFSZ	((1024 * 1024) / sizeof (opus_int16))	/* bytes */

#define	WAV_OP_PARAM(al_op, al_param_name, err_ret, ...) \
	do { \
		ALuint err; \
		alc_t sav; \
		if (wav == NULL || wav->alsrc == 0) \
			return err_ret; \
		VERIFY(ctx_save(wav->alc, &sav)); \
		al_op(wav->alsrc, al_param_name, __VA_ARGS__); \
		if ((err = alGetError()) != AL_NO_ERROR) { \
			logMsg("Error performing " #al_op "(" #al_param_name \
			    ") on WAV %s, error 0x%x.", wav->name, err); \
			VERIFY(ctx_restore(wav->alc, &sav)); \
			return err_ret; \
		} \
		VERIFY(ctx_restore(wav->alc, &sav)); \
	} while (0)

#define	WAV_SET_PARAM(al_op, al_param_name, ...) \
	WAV_OP_PARAM(al_op, al_param_name, , __VA_ARGS__)

#define	LISTENER_OP_PARAM(al_op, al_param_name, err_ret, ...) \
	do { \
		ALuint err; \
		alc_t sav; \
		VERIFY(ctx_save(alc, &sav)); \
		al_op(al_param_name, __VA_ARGS__); \
		if ((err = alGetError()) != AL_NO_ERROR) { \
			logMsg("Error changing listener param " \
			    #al_param_name ", error 0x%x.", err); \
			VERIFY(ctx_restore(alc, &sav)); \
			return err_ret; \
		} \
		VERIFY(ctx_restore(alc, &sav)); \
	} while (0)

#define	LISTENER_SET_PARAM(al_op, al_param_name, ...) \
	LISTENER_OP_PARAM(al_op, al_param_name, , __VA_ARGS__)

struct alc {
	ALCdevice	*dev;
	ALCcontext	*ctx;
};

/*
 * ctx_save/ctx_restore must be used to bracket all OpenAL calls. This makes
 * sure private contexts are handled properly (when in use). If shared
 * contexts are used, these functions are no-ops.
 */
static bool_t
ctx_save(alc_t *alc, alc_t *sav)
{
	ALuint err;

	(void) alGetError(); // cleanup after other OpenAL users

	if (alc != NULL && alc->ctx == NULL)
		return (B_TRUE);

	sav->ctx = alcGetCurrentContext();
	if (sav->ctx != NULL) {
		sav->dev = alcGetContextsDevice(sav->ctx);
		VERIFY(sav->dev != NULL);
	} else {
		sav->dev = NULL;
	}

	if (alc != NULL) {
		ASSERT(alc->ctx != NULL);
		alcMakeContextCurrent(alc->ctx);
		if ((err = alcGetError(alc->dev)) != ALC_NO_ERROR) {
			logMsg("Error switching to my audio context (0x%x)",
			    err);
			return (B_FALSE);
		}
	}

	return (B_TRUE);
}

static bool_t
ctx_restore(alc_t *alc, alc_t *sav)
{
	ALuint err;

	if (alc != NULL && alc->ctx == NULL)
		return (B_TRUE);

	if (sav->ctx != NULL) {
		alcMakeContextCurrent(sav->ctx);
		VERIFY(sav->dev != NULL);
		if ((err = alcGetError(sav->dev)) != ALC_NO_ERROR) {
			logMsg("Error restoring shared audio context (0x%x)",
			    err);
			return (B_FALSE);
		}
	}

	return (B_TRUE);
}

char **
openal_list_output_devs(size_t *num_p)
{
	char **devs = NULL;
	size_t num = 0;

	for (const char *device = alcGetString(NULL, ALC_ALL_DEVICES_SPECIFIER);
	    device != NULL && *device != 0; device += strlen(device) + 1) {
		devs = realloc(devs, (num + 1) * sizeof (*devs));
		devs[num] = strdup(device);
		num++;
	}

	*num_p = num;
	return (devs);
}

alc_t *
openal_init(const char *devname, bool_t shared)
{
	alc_t	*alc;
	alc_t	sav;

	if (!ctx_save(NULL, &sav))
		return (NULL);

	alc = calloc(1, sizeof (*alc));
	if (!shared || sav.ctx == NULL) {
		ALCdevice *dev = NULL;
		ALCcontext *ctx = NULL;
		ALuint err;

		dev = alcOpenDevice(devname);
		if (dev == NULL) {
			logMsg("Cannot init audio system: device open failed.");
			free(alc);
			(void) ctx_restore(NULL, &sav);
			return (B_FALSE);
		}
		ctx = alcCreateContext(dev, NULL);
		if ((err = alcGetError(dev)) != ALC_NO_ERROR) {
			logMsg("Cannot init audio system: create context "
			    "failed (0x%x)", err);
			alcCloseDevice(dev);
			free(alc);
			(void) ctx_restore(NULL, &sav);
			return (B_FALSE);
		}
		VERIFY(ctx != NULL);
		/* No current context, install our own */
		if (shared && sav.ctx == NULL) {
			sav.ctx = ctx;
			sav.dev = dev;
			alcMakeContextCurrent(sav.ctx);
			VERIFY(sav.dev != NULL);
			if ((err = alcGetError(sav.dev)) != ALC_NO_ERROR) {
				logMsg("Error installing shared audio context "
				    "(0x%x)", err);
				return (B_FALSE);
			}
		}
		if (!shared) {
			alc->dev = dev;
			alc->ctx = ctx;
		}
	}

	if (!ctx_restore(alc, &sav)) {
		if (!shared) {
			alcDestroyContext(alc->ctx);
			alcCloseDevice(alc->dev);
		}
		free(alc);
		return (NULL);
	}

	return (alc);
}

void
openal_fini(alc_t *alc)
{
	ASSERT(alc != NULL);

	if (alc->dev != NULL) {
		alcDestroyContext(alc->ctx);
		alcCloseDevice(alc->dev);
	}
	free(alc);
}

static bool_t
check_audio_fmt(const wav_fmt_hdr_t *fmt, const char *filename)
{
	/* format support check */
	if (fmt->datafmt != 1 ||
	    (fmt->n_channels != 1 && fmt->n_channels != 2) ||
	    (fmt->bps != 8 && fmt->bps != 16)) {
		logMsg("Error loading WAV file \"%s\": unsupported audio "
		    "format.", filename);
		return (B_FALSE);
	}
	return (B_TRUE);
}

static bool_t
wav_gen_al_bufs(wav_t *wav, const void *buf, size_t bufsz, const char *filename)
{
	ALuint err;
	ALfloat zeroes[3] = { 0.0, 0.0, 0.0 };
	alc_t sav;

	if (!ctx_save(wav->alc, &sav))
		return (B_FALSE);

	alGenBuffers(1, &wav->albuf);
	if ((err = alGetError()) != AL_NO_ERROR) {
		logMsg("Error loading WAV file %s: alGenBuffers failed (0x%x).",
		    filename, err);
		(void) ctx_restore(wav->alc, &sav);
		return (B_FALSE);
	}
	if (wav->fmt.bps == 16) {
		alBufferData(wav->albuf, wav->fmt.n_channels == 2 ?
		    AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, buf, bufsz,
		    wav->fmt.srate);
	} else {
		alBufferData(wav->albuf, wav->fmt.n_channels == 2 ?
		    AL_FORMAT_STEREO8 : AL_FORMAT_MONO8, buf, bufsz,
		    wav->fmt.srate);
	}

	if ((err = alGetError()) != AL_NO_ERROR) {
		logMsg("Error loading WAV file %s: alBufferData failed (0x%x).",
		    filename, err);
		(void) ctx_restore(wav->alc, &sav);
		return (B_FALSE);
	}

	alGenSources(1, &wav->alsrc);
	if ((err = alGetError()) != AL_NO_ERROR) {
		logMsg("Error loading WAV file %s: alGenSources failed (0x%x).",
		    filename, err);
		(void) ctx_restore(wav->alc, &sav);
		return (B_FALSE);
	}
#define	CHECK_ERROR(stmt) \
	do { \
		stmt; \
		if ((err = alGetError()) != AL_NO_ERROR) { \
			logMsg("Error loading WAV file %s, \"%s\" failed " \
			    "with error 0x%x", filename, #stmt, err); \
			alDeleteSources(1, &wav->alsrc); \
			VERIFY3S(alGetError(), ==, AL_NO_ERROR); \
			wav->alsrc = 0; \
			(void) ctx_restore(wav->alc, &sav); \
			return (B_FALSE); \
		} \
	} while (0)
	CHECK_ERROR(alSourcei(wav->alsrc, AL_BUFFER, wav->albuf));
	CHECK_ERROR(alSourcef(wav->alsrc, AL_PITCH, 1.0));
	CHECK_ERROR(alSourcef(wav->alsrc, AL_GAIN, 1.0));
	CHECK_ERROR(alSourcei(wav->alsrc, AL_LOOPING, 0));
	CHECK_ERROR(alSourcefv(wav->alsrc, AL_POSITION, zeroes));
	CHECK_ERROR(alSourcefv(wav->alsrc, AL_VELOCITY, zeroes));

	(void) ctx_restore(wav->alc, &sav);

	return (B_TRUE);
}

static wav_t *
wav_load_opus(const char *filename, alc_t *alc)
{
	wav_t *wav;
	int error;
	OggOpusFile *file = op_open_file(filename, &error);
	const OpusHead *head;
	unsigned sz = 0, cap = 0;
	opus_int16 *pcm = NULL;

	if (file == NULL) {
		logMsg("Error reading OPUS file \"%s\": op_open_file error %d",
		    filename, error);
		return (NULL);
	}
	head = op_head(file, 0);
	VERIFY(head != NULL);

	wav = calloc(1, sizeof (*wav));
	wav->alc = alc;

	/* fake a wav_fmt_hdr_t from the OpusHead object */
	wav->fmt.datafmt = 1;
	wav->fmt.n_channels = head->channel_count;
	wav->fmt.srate = 48000;		/* Opus always outputs 48 kHz! */
	wav->fmt.bps = 16;
	wav->fmt.byte_rate = (wav->fmt.srate * wav->fmt.bps *
	    wav->fmt.n_channels) / 8;

	if (!check_audio_fmt(&wav->fmt, filename))
		goto errout;

	for (;;) {
		int op_read_sz;

		/*
		 * opusfile asks us to keep at least 120ms of buffer space
		 * available, so /8 gives us 125ms
		 */
		if (sz + ((wav->fmt.srate * wav->fmt.n_channels) / 8) >= cap) {
			cap += READ_BUFSZ;
			pcm = realloc(pcm, cap * sizeof (*pcm));
		}
		op_read_sz = op_read(file, &pcm[sz], cap - sz, 0);
		if (op_read_sz > 0)
			sz += op_read_sz * wav->fmt.n_channels;
		else
			break;
	}
	wav->duration = ((double)((sz - head->pre_skip) /
	    wav->fmt.n_channels)) / wav->fmt.srate;

	VERIFY3S(head->pre_skip, <, sz);
	wav_gen_al_bufs(wav, &pcm[head->pre_skip],
	    (sz - head->pre_skip) * sizeof (*pcm), filename);

	free(pcm);
	op_free(file);

	return (wav);
errout:
	if (wav != NULL)
		wav_free(wav);
	if (file != NULL)
		op_free(file);
	return (NULL);
}

static wav_t *
wav_load_mp3(const char *filename, alc_t *alc)
{
	wav_t *wav;
	mp3_decoder_t mp3;
	mp3_info_t info;
	long len;
	char *contents = file2str_name(&len, filename);
	int16_t *pcm = NULL;
	int bytes, n_bytes, audio_bytes;

	if (contents == NULL) {
		logMsg("Error reading MP3 file \"%s\": %s", filename,
		    strerror(errno));
		return (NULL);
	}

	wav = calloc(1, sizeof (*wav));
	wav->alc = alc;

	mp3 = mp3_create();
	pcm = malloc(1024 * 1024);

	bytes = mp3_decode(mp3, contents, len, pcm, &info);
	if (bytes == 0) {
		logMsg("Error decoding MP3 file %s", filename);
		mp3_done(&mp3);
		goto errout;
	}

	/* fake a wav_fmt_hdr_t from the OpusHead object */
	wav->fmt.datafmt = 1;
	wav->fmt.n_channels = info.channels;
	wav->fmt.srate = info.sample_rate;
	wav->fmt.bps = 16;
	wav->fmt.byte_rate = (wav->fmt.srate * wav->fmt.bps *
	    wav->fmt.n_channels) / 8;

	audio_bytes = 0;
	while ((n_bytes = mp3_decode(mp3, &contents[bytes], len - bytes,
	    &pcm[audio_bytes / sizeof (*pcm)], &info)) > 0) {
		bytes += n_bytes;
		audio_bytes += info.audio_bytes;
	}

	if (!check_audio_fmt(&wav->fmt, filename)) {
		mp3_done(&mp3);
		goto errout;
	}

	wav->duration = ((double)((bytes / sizeof (*pcm)) /
	    wav->fmt.n_channels)) / wav->fmt.srate;

	wav_gen_al_bufs(wav, pcm, bytes, filename);

	mp3_done(&mp3);
	free(contents);
	free(pcm);

	return (wav);
errout:
	if (wav != NULL)
		wav_free(wav);
	free(contents);
	free(pcm);

	return (NULL);
}

static wav_t *
wav_load_wav(const char *filename, alc_t *alc)
{
	wav_t *wav = NULL;
	FILE *fp;
	size_t filesz;
	riff_chunk_t *riff = NULL;
	uint8_t *filebuf = NULL;
	riff_chunk_t *chunk;
	int sample_sz;

	if ((fp = fopen(filename, "rb")) == NULL) {
		logMsg("Error loading WAV file \"%s\": can't open file: %s",
		    filename, strerror(errno));
		return (NULL);
	}

	fseek(fp, 0, SEEK_END);
	filesz = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if ((wav = calloc(1, sizeof (*wav))) == NULL)
		goto errout;
	wav->alc = alc;
	if ((filebuf = malloc(filesz)) == NULL)
		goto errout;
	if (fread(filebuf, 1, filesz, fp) != filesz)
		goto errout;
	if ((riff = riff_parse(WAVE_ID, filebuf, filesz)) == NULL) {
		logMsg("Error loading WAV file \"%s\": file doesn't appear "
		    "to be valid RIFF.", filename);
		goto errout;
	}

	chunk = riff_find_chunk(riff, FMT_ID, 0);
	if (chunk == NULL || chunk->datasz < sizeof (wav->fmt)) {
		logMsg("Error loading WAV file \"%s\": file missing or "
		    "malformed `fmt ' chunk.", filename);
		goto errout;
	}
	memcpy(&wav->fmt, chunk->data, sizeof (wav->fmt));
	if (riff->bswap) {
		wav->fmt.datafmt = BSWAP16(wav->fmt.datafmt);
		wav->fmt.n_channels = BSWAP16(wav->fmt.n_channels);
		wav->fmt.srate = BSWAP32(wav->fmt.srate);
		wav->fmt.byte_rate = BSWAP32(wav->fmt.byte_rate);
		wav->fmt.bps = BSWAP16(wav->fmt.bps);
	}

	if (!check_audio_fmt(&wav->fmt, filename))
		goto errout;

	/*
	 * Check the DATA chunk is present and contains the correct number
	 * of samples.
	 */
	sample_sz = (wav->fmt.n_channels * wav->fmt.bps) / 8;
	chunk = riff_find_chunk(riff, DATA_ID, 0);
	if (chunk == NULL || (chunk->datasz & (sample_sz - 1)) != 0) {
		logMsg("Error loading WAV file %s: `data' chunk missing or "
		    "contains bad number of samples.", filename);
		goto errout;
	}

	wav->duration = ((double)(chunk->datasz / sample_sz)) / wav->fmt.srate;

	/* BSWAP the samples if necessary */
	if (riff->bswap && wav->fmt.bps == 16) {
		for (uint16_t *s = (uint16_t *)chunk->data;
		    (uint8_t *)s < chunk->data + chunk->datasz;
		    s++)
			*s = BSWAP16(*s);
	}

	if (!wav_gen_al_bufs(wav, chunk->data, chunk->datasz, filename))
		goto errout;

	riff_free_chunk(riff);
	free(filebuf);
	fclose(fp);

	return (wav);

errout:
	if (filebuf != NULL)
		free(filebuf);
	wav_free(wav);
	fclose(fp);

	return (NULL);
}

/*
 * Loads a WAV file from a file and returns a buffered representation
 * ready to be passed to OpenAL. Currently we only support mono or
 * stereo raw PCM (uncompressed) WAV files.
 */
wav_t *
wav_load(const char *filename, const char *descr_name, alc_t *alc)
{
	const char *dot = strrchr(filename, '.');
	wav_t *wav;

	ASSERT(alc != NULL);

	if (dot != NULL && (strcmp(&dot[1], "opus") == 0 ||
	    strcmp(&dot[1], "OPUS") == 0)) {
		wav = wav_load_opus(filename, alc);
	} else if (dot != NULL && (strcmp(&dot[1], "mp3") == 0 ||
	    strcmp(&dot[1], "MP3") == 0)) {
		wav = wav_load_mp3(filename, alc);
	} else {
		wav = wav_load_wav(filename, alc);
	}
	if (wav != NULL)
		wav->name = strdup(descr_name);

	return (wav);
}

/*
 * Destroys a WAV file as returned by wav_load().
 */
void
wav_free(wav_t *wav)
{
	alc_t sav;

	if (wav == NULL)
		return;

	VERIFY(ctx_save(wav->alc, &sav));
	free(wav->name);
	if (wav->alsrc != 0) {
		alSourceStop(wav->alsrc);
		alDeleteSources(1, &wav->alsrc);
	}
	if (wav->albuf != 0)
		alDeleteBuffers(1, &wav->albuf);
	VERIFY(ctx_restore(wav->alc, &sav));

	free(wav);
}

/*
 * Sets the audio gain (volume) of a WAV file from 0.0 (silent) to 1.0
 * (full volume).
 */
void
wav_set_gain(wav_t *wav, float gain)
{
	WAV_SET_PARAM(alSourcef, AL_GAIN, gain);
}

float
wav_get_gain(wav_t *wav)
{
	ALfloat gain;
	WAV_OP_PARAM(alGetSourcef, AL_GAIN, NAN, &gain);
	return (gain);
}

/*
 * Sets the whether the WAV will loop continuously while playing.
 */
void
wav_set_loop(wav_t *wav, bool_t loop)
{
	WAV_SET_PARAM(alSourcei, AL_LOOPING, loop);
}

bool_t
wav_get_loop(wav_t *wav)
{
	ALint looping;
	WAV_OP_PARAM(alGetSourcei, AL_LOOPING, B_FALSE, &looping);
	return (looping);
}

void
wav_set_pitch(wav_t *wav, float pitch)
{
	WAV_SET_PARAM(alSourcef, AL_PITCH, pitch);
}

float
wav_get_pitch(wav_t *wav)
{
	ALfloat pitch;
	WAV_OP_PARAM(alGetSourcef, AL_PITCH, NAN, &pitch);
	return (pitch);
}

void
wav_set_position(wav_t *wav, vect3_t pos)
{
	WAV_SET_PARAM(alSource3f, AL_POSITION, pos.x, pos.y, pos.z);
}

vect3_t
wav_get_position(wav_t *wav)
{
	ALfloat x, y, z;
	WAV_OP_PARAM(alGetSource3f, AL_POSITION, NULL_VECT3, &x, &y, &z);
	return (VECT3(x, y, z));
}

void
wav_set_velocity(wav_t *wav, vect3_t vel)
{
	WAV_SET_PARAM(alSource3f, AL_VELOCITY, vel.x, vel.y, vel.z);
}

vect3_t
wav_get_velocity(wav_t *wav)
{
	ALfloat x, y, z;
	WAV_OP_PARAM(alGetSource3f, AL_VELOCITY, NULL_VECT3, &x, &y, &z);
	return (VECT3(x, y, z));
}

void
wav_set_ref_dist(wav_t *wav, double d)
{
	WAV_SET_PARAM(alSourcef, AL_REFERENCE_DISTANCE, d);
}

double
wav_get_ref_dist(wav_t *wav)
{
	ALfloat d;
	WAV_OP_PARAM(alGetSourcef, AL_REFERENCE_DISTANCE, NAN, &d);
	return (d);
}

void
wav_set_max_dist(wav_t *wav, double d)
{
	WAV_SET_PARAM(alSourcef, AL_MAX_DISTANCE, d);
}

double
wav_get_max_dist(wav_t *wav)
{
	ALfloat d;
	WAV_OP_PARAM(alGetSourcef, AL_MAX_DISTANCE, NAN, &d);
	return (d);
}

void
wav_set_rolloff_fact(wav_t *wav, double r)
{
	WAV_SET_PARAM(alSourcef, AL_ROLLOFF_FACTOR, r);
}

double
wav_get_rolloff_fact(wav_t *wav)
{
	ALfloat r;
	WAV_OP_PARAM(alGetSourcef, AL_ROLLOFF_FACTOR, NAN, &r);
	return (r);
}

/*
 * Starts playback of a WAV file loaded through wav_load.
 * Playback volume is full (1.0) or the last value set by wav_set_gain.
 */
bool_t
wav_play(wav_t *wav)
{
	ALuint err;
	alc_t sav;

	if (wav == NULL)
		return (B_FALSE);

	VERIFY(ctx_save(wav->alc, &sav));

	alSourcePlay(wav->alsrc);
	if ((err = alGetError()) != AL_NO_ERROR) {
		logMsg("Can't play sound: alSourcePlay failed (0x%x).", err);
		VERIFY(ctx_restore(wav->alc, &sav));
		return (B_FALSE);
	}
	wav->play_start = microclock();

	VERIFY(ctx_restore(wav->alc, &sav));

	return (B_TRUE);
}

bool_t
wav_is_playing(wav_t *wav)
{
	return (wav->play_start != 0 && (wav_get_loop(wav) ||
	    USEC2SEC(microclock() - wav->play_start) < wav->duration));
}

/*
 * Stops playback of a WAV file started via wav_play and resets the
 * playback position back to the start of the file.
 */
void
wav_stop(wav_t *wav)
{
	ALuint err;
	alc_t sav;

	if (wav == NULL)
		return;

	if (wav->alsrc == 0)
		return;

	VERIFY(ctx_save(wav->alc, &sav));
	alSourceStop(wav->alsrc);
	if ((err = alGetError()) != AL_NO_ERROR)
		logMsg("Can't stop sound, alSourceStop failed (0x%x).", err);
	VERIFY(ctx_restore(wav->alc, &sav));
	wav->play_start = 0;
}

void
alc_set_dist_model(alc_t *alc, ALenum model)
{
	alc_t sav;
	VERIFY(ctx_save(alc, &sav));
	alDistanceModel(model);
	VERIFY(ctx_restore(alc, &sav));
}

void
alc_listener_set_pos(alc_t *alc, vect3_t pos)
{
	LISTENER_SET_PARAM(alListener3f, AL_POSITION, pos.x, pos.y, pos.z);
}

vect3_t
alc_listener_get_pos(alc_t *alc)
{
	ALfloat x, y, z;
	LISTENER_OP_PARAM(alGetListener3f, AL_POSITION, NULL_VECT3, &x, &y, &z);
	return (VECT3(x, y, z));
}

void
alc_listener_set_orient(alc_t *alc, vect3_t orient)
{
	ALfloat v[6] = {
		sin(DEG2RAD(orient.z)),
		sin(DEG2RAD(orient.x)),
		-cos(DEG2RAD(orient.z)),
		0, 1, 0		/* Assume viewer is always upright */
	};
	LISTENER_SET_PARAM(alListenerfv, AL_ORIENTATION, v);
}

void
alc_listener_set_velocity(alc_t *alc, vect3_t vel)
{
	LISTENER_SET_PARAM(alListener3f, AL_VELOCITY, vel.x, vel.y, vel.z);
}
