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
#include <acfutils/list.h>
#include <acfutils/log.h>
#include <acfutils/riff.h>
#include <acfutils/types.h>
#include <acfutils/wav.h>

#define	WAVE_ID	FOURCC("WAVE")
#define	FMT_ID	FOURCC("fmt ")
#define	DATA_ID	FOURCC("data")

#define	READ_BUFSZ	((1024 * 1024) / sizeof (opus_int16))	/* bytes */

static ALCdevice *old_dev = NULL, *my_dev = NULL;
static ALCcontext *old_ctx = NULL, *my_ctx = NULL;
static bool_t use_shared = B_FALSE;
static bool_t ctx_saved = B_FALSE;
static bool_t openal_inited = B_FALSE;

/*
 * ctx_save/ctx_restore must be used to bracket all OpenAL calls. This makes
 * sure private contexts are handled properly (when in use). If shared
 * contexts are used, these functions are no-ops.
 */
static bool_t
ctx_save(void)
{
	ALuint err;

	if (use_shared)
		return (B_TRUE);

	ASSERT(!ctx_saved);

	old_ctx = alcGetCurrentContext();
	if (old_ctx != NULL) {
		old_dev = alcGetContextsDevice(old_ctx);
		VERIFY(old_dev != NULL);
	} else {
		old_dev = NULL;
	}

	alcMakeContextCurrent(my_ctx);
	if ((err = alcGetError(my_dev)) != ALC_NO_ERROR) {
		logMsg("Error switching to my audio context (0x%x)", err);
		return (B_FALSE);
	}

	ctx_saved = B_TRUE;

	return (B_TRUE);
}

static bool_t
ctx_restore(void)
{
	ALuint err;

	if (use_shared)
		return (B_TRUE);

	ASSERT(ctx_saved);
	/* To prevent tripping ASSERT in ctx_save if ctx_restore fails */
	ctx_saved = B_FALSE;

	if (old_ctx != NULL) {
		alcMakeContextCurrent(old_ctx);
		VERIFY(old_dev != NULL);
		if ((err = alcGetError(old_dev)) != ALC_NO_ERROR) {
			logMsg("Error restoring shared audio context (0x%x)",
			    err);
			return (B_FALSE);
		}
	}

	return (B_TRUE);
}

void
openal_set_shared_ctx(bool_t flag)
{
	ASSERT(!openal_inited);
	use_shared = flag;
}

bool_t
openal_init(void)
{
	ASSERT(!openal_inited);

	if (!ctx_save())
		return (B_FALSE);

	if (!use_shared || alcGetCurrentContext() == NULL) {
		ALuint err;

		my_dev = alcOpenDevice(NULL);
		if (my_dev == NULL) {
			logMsg("Cannot init audio system: device open failed.");
			(void) ctx_restore();
			return (B_FALSE);
		}
		my_ctx = alcCreateContext(my_dev, NULL);
		if ((err = alcGetError(my_dev)) != ALC_NO_ERROR) {
			logMsg("Cannot init audio system: create context "
			    "failed (0x%x)", err);
			alcCloseDevice(my_dev);
			(void) ctx_restore();
			return (B_FALSE);
		}
		VERIFY(my_ctx != NULL);
		/* No current context, install our own */
		if (use_shared && alcGetCurrentContext() == NULL) {
			alcMakeContextCurrent(my_ctx);
			if ((err = alcGetError(my_dev)) != ALC_NO_ERROR) {
				logMsg("Error installing my audio context "
				    "(0x%x)", err);
				alcDestroyContext(my_ctx);
				alcCloseDevice(my_dev);
				return (B_FALSE);
			}
		}
	}

	if (!ctx_restore())
		return (B_FALSE);

	openal_inited = B_TRUE;

	return (B_TRUE);
}

void
openal_fini()
{
	if (!openal_inited)
		return;

	if (!use_shared) {
		alcDestroyContext(my_ctx);
		alcCloseDevice(my_dev);
		my_ctx = NULL;
		my_dev = NULL;
	}
	openal_inited = B_FALSE;
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

	if (!ctx_save())
		return (B_FALSE);

	alGenBuffers(1, &wav->albuf);
	if ((err = alGetError()) != AL_NO_ERROR) {
		logMsg("Error loading WAV file %s: alGenBuffers failed (0x%x).",
		    filename, err);
		(void) ctx_restore();
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
		(void) ctx_restore();
		return (B_FALSE);
	}

	alGenSources(1, &wav->alsrc);
	if ((err = alGetError()) != AL_NO_ERROR) {
		logMsg("Error loading WAV file %s: alGenSources failed (0x%x).",
		    filename, err);
		(void) ctx_restore();
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
			(void) ctx_restore(); \
			return (B_FALSE); \
		} \
	} while (0)
	CHECK_ERROR(alSourcei(wav->alsrc, AL_BUFFER, wav->albuf));
	CHECK_ERROR(alSourcef(wav->alsrc, AL_PITCH, 1.0));
	CHECK_ERROR(alSourcef(wav->alsrc, AL_GAIN, 1.0));
	CHECK_ERROR(alSourcei(wav->alsrc, AL_LOOPING, 0));
	CHECK_ERROR(alSourcefv(wav->alsrc, AL_POSITION, zeroes));
	CHECK_ERROR(alSourcefv(wav->alsrc, AL_VELOCITY, zeroes));

	(void) ctx_restore();

	return (B_TRUE);
}

static wav_t *
wav_load_opus(const char *filename)
{
	wav_t *wav;
	int error;
	OggOpusFile *file = op_open_file(filename, &error);
	const OpusHead *head;
	int sz = 0, cap = 0;
	opus_int16 *pcm = NULL;

	if (file == NULL) {
		logMsg("Error reading OPUS file \"%s\": op_open_file error %d",
		    filename, error);
		return (NULL);
	}
	head = op_head(file, 0);
	VERIFY(head != NULL);

	wav = calloc(1, sizeof (*wav));

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

		if (sz == cap) {
			cap += READ_BUFSZ;
			pcm = realloc(pcm, cap * sizeof (*pcm));
		}
		op_read_sz = op_read(file, &pcm[sz], cap - sz, 0);
		if (op_read_sz > 0)
			sz += op_read_sz * wav->fmt.n_channels;
		else
			break;
	}
	wav->duration = ((double)(sz / wav->fmt.n_channels)) / wav->fmt.srate;

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
wav_load_wav(const char *filename)
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
wav_load(const char *filename, const char *descr_name)
{
	const char *dot = strrchr(filename, '.');
	wav_t *wav;

	ASSERT(openal_inited);

	if (dot != NULL && (strcmp(&dot[1], "opus") == 0 ||
	    strcmp(&dot[1], "OPUS") == 0)) {
		wav = wav_load_opus(filename);
	} else {
		wav = wav_load_wav(filename);
	}
	if (wav != NULL) {
		wav->name = strdup(descr_name);
	}

	return (wav);
}

/*
 * Destroys a WAV file as returned by wav_load().
 */
void
wav_free(wav_t *wav)
{
	if (wav == NULL)
		return;

	ASSERT(openal_inited);

	VERIFY(ctx_save());
	free(wav->name);
	if (wav->alsrc != 0) {
		alSourceStop(wav->alsrc);
		alDeleteSources(1, &wav->alsrc);
	}
	if (wav->albuf != 0)
		alDeleteBuffers(1, &wav->albuf);
	VERIFY(ctx_restore());

	free(wav);
}

#define	WAV_SET_PARAM(al_op, al_param_name, ...) \
	do { \
		ALuint err; \
		if (wav == NULL || wav->alsrc == 0) \
			return; \
		ASSERT(openal_inited); \
		VERIFY(ctx_save()); \
		al_op(wav->alsrc, al_param_name, __VA_ARGS__); \
		if ((err = alGetError()) != AL_NO_ERROR) { \
			logMsg("Error changing " #al_param_name " of WAV %s, " \
			    "error 0x%x.", wav->name, err); \
		} \
		VERIFY(ctx_restore()); \
	} while (0)

/*
 * Sets the audio gain (volume) of a WAV file from 0.0 (silent) to 1.0
 * (full volume).
 */
void
wav_set_gain(wav_t *wav, float gain)
{
	WAV_SET_PARAM(alSourcef, AL_GAIN, gain);
}

/*
 * Sets the whether the WAV will loop continuously while playing.
 */
void
wav_set_loop(wav_t *wav, bool_t loop)
{
	WAV_SET_PARAM(alSourcei, AL_LOOPING, loop);
}

void
wav_set_pitch(wav_t *wav, float pitch)
{
	WAV_SET_PARAM(alSourcef, AL_PITCH, pitch);
}

void
wav_set_position(wav_t *wav, vect3_t pos)
{
	WAV_SET_PARAM(alSource3f, AL_POSITION, pos.x, pos.y, pos.z);
}

/*
 * Starts playback of a WAV file loaded through wav_load.
 * Playback volume is full (1.0) or the last value set by wav_set_gain.
 */
bool_t
wav_play(wav_t *wav)
{
	ALuint err;

	if (wav == NULL)
		return (B_FALSE);

	ASSERT(openal_inited);

	VERIFY(ctx_save());

	alSourcePlay(wav->alsrc);
	if ((err = alGetError()) != AL_NO_ERROR) {
		logMsg("Can't play sound: alSourcePlay failed (0x%x).", err);
		VERIFY(ctx_restore());
		return (B_FALSE);
	}

	VERIFY(ctx_restore());

	return (B_TRUE);
}

/*
 * Stops playback of a WAV file started via wav_play and resets the
 * playback position back to the start of the file.
 */
void
wav_stop(wav_t *wav)
{
	ALuint err;

	if (wav == NULL)
		return;

	if (wav->alsrc == 0)
		return;

	ASSERT(openal_inited);
	VERIFY(ctx_save());
	alSourceStop(wav->alsrc);
	if ((err = alGetError()) != AL_NO_ERROR)
		logMsg("Can't stop sound, alSourceStop failed (0x%x).", err);
	VERIFY(ctx_restore());
}
