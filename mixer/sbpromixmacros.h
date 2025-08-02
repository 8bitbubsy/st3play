#pragma once

#define SBPRO_GET_VOLUMES \
	float fPanL = 1.0f; \
	float fPanR = 1.0f; \
	if (song.stereomode) \
	{ \
		/* While ST3 has either 100% left or 100% right in SB Pro stereo */ \
		/* mode, I want to be kind to headphone users and use 0x3/0xC */ \
		/* GUS panning (using GUS equal power panning). */ \
		if (ch->channelnum < 8) \
		{ \
			/* left channel */ \
			fPanL = 0.894427191f; /* sqrt((15-0x3)/15) */ \
			fPanR = 0.447213595f; /* sqrt(    0x3 /15) */ \
		} \
		else \
		{ \
			/* right channel */ \
			fPanL = 0.447213595f; /* sqrt((15-0xC)/15) */ \
			fPanR = 0.894427191f; /* sqrt(    0xC /15) */ \
		} \
	} \
	\
	if (ch->amixtype == 1) /* swap output channels for this voice (efx SA1 = swap L/R, SA0 = normal) */ \
	{ \
		float fTmpPan = fPanL; \
		fPanL = fPanR; \
		fPanR = fTmpPan; \
	} \
	\
	const float fVol = ch->m_vol * (1.0f / 64.0f); \
	fVolL = fVol * fPanL; \
	fVolR = fVol * fPanR;

#define SBPRO_GET_MIXER_VARS \
	fAudioMixL = fMixBufL; \
	fAudioMixR = fMixBufR; \
	smpPtr = ch->m_base + ch->m_pos;

#define SBPRO_FIX_LERP_SAMPLE \
	const uint8_t oldSample = ch->m_base[ch->m_end]; \
	if (ch->m_loop != 65535) /* loop enabled? */ \
		ch->m_base[ch->m_end] = ch->m_base[ch->m_loop]; \
	else \
		ch->m_base[ch->m_end] = 0;

#define SBPRO_RESTORE_LERP_SAMPLE \
	ch->m_base[ch->m_end] = oldSample;

#define SBPRO_INC_POS \
	ch->frac += ch->delta; \
	smpPtr += ch->frac >> MIX_FRAC_BITS; \
	ch->frac &= MIX_FRAC_MASK;

#define SBPRO_GET_SMP \
	fSample  = smpPtr[0] * (1.0f / 128.0f); \
	fSample2 = smpPtr[1] * (1.0f / 128.0f); \
	fSample += (fSample2-fSample) * ((int32_t)((uint32_t)ch->frac >> 1) * (1.0f / (MIX_FRAC_SCALE/2.0f)));

#define SBPRO_RENDER_SMP \
	SBPRO_GET_SMP \
	*fAudioMixL++ += fSample * fVolL; \
	*fAudioMixR++ += fSample * fVolR;

#define SBPRO_LIMIT_MIX_NUM \
	samplesToMix = INT32_MAX; \
	if (ch->delta > 0) \
	{ \
		i = ch->m_end - ch->m_pos; /* samples until end point */ \
		uint64_t tmp64 = ((uint64_t)i << MIX_FRAC_BITS) - ch->frac; \
		samplesToMix = (int32_t)(tmp64 / ch->delta) + 1; \
	} \
	\
	if (samplesToMix > samplesLeft) \
		samplesToMix = samplesLeft; \
	\
	samplesLeft -= samplesToMix;

#define SBPRO_HANDLE_SAMPLE_END \
	ch->m_pos = (uint32_t)(smpPtr - ch->m_base); \
	if (ch->m_pos >= ch->m_end) \
	{ \
		if (ch->m_loop != 65535) /* loop enabled? */ \
		{ \
			const uint32_t loopLength = (uint32_t)(ch->m_end - ch->m_loop); \
			do \
			{ \
				ch->m_pos -= loopLength; \
			} \
			while (ch->m_pos >= ch->m_end); \
		} \
		else /* no loop */ \
		{ \
			ch->m_speed = 0; /* stop sample */ \
			SBPRO_RESTORE_LERP_SAMPLE \
			break; \
		} \
	} \
	smpPtr = ch->m_base + ch->m_pos;
