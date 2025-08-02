/*
** Channel mixer for Sound Blaster Pro mode.
** Has way better precision than the ST3 SB Pro mixer,
** uses linear interpolation, and has softer stereo separation.
** I do this to make the listening experience closer to a GUS,
** which is more comfortable.
**
** NOTE: This code expects that there's room for 1 extra sample
**       after the end of the allocated sample data! This sample
**       is sometimes written to before entering the mixing loop,
**       and restored afterwards.
*/

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "sbpro.h"
#include "sbpromixmacros.h"
#include "../dig.h"
#include "../digdata.h"

static void SBPro_MixCh(zchn_t *ch, float *fMixBufL, float *fMixBufR, int32_t numSamples)
{
	const int8_t *smpPtr;
	int32_t i, samplesToMix, samplesLeft;
	float fSample, fSample2, fVolL, fVolR, *fAudioMixL, *fAudioMixR;

	SBPRO_GET_MIXER_VARS
	SBPRO_GET_VOLUMES
	SBPRO_FIX_LERP_SAMPLE

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		SBPRO_LIMIT_MIX_NUM
		for (i = 0; i < (samplesToMix & 3); i++)
		{
			SBPRO_RENDER_SMP
			SBPRO_INC_POS
		}
		samplesToMix >>= 2;
		for (i = 0; i < samplesToMix; i++)
		{
			SBPRO_RENDER_SMP
			SBPRO_INC_POS
			SBPRO_RENDER_SMP
			SBPRO_INC_POS
			SBPRO_RENDER_SMP
			SBPRO_INC_POS
			SBPRO_RENDER_SMP
			SBPRO_INC_POS
		}
		SBPRO_HANDLE_SAMPLE_END
	}

	SBPRO_RESTORE_LERP_SAMPLE
}

static void SBPro_SilenceMix(zchn_t *ch, int32_t numSamples)
{
	const uint64_t samplesToMix = (uint64_t)ch->delta * (uint32_t)numSamples; // fixed-point

	const uint32_t samples = (uint32_t)(samplesToMix >> MIX_FRAC_BITS);
	const uint64_t samplesFrac = (samplesToMix & MIX_FRAC_MASK) + ch->frac;

	ch->m_pos += samples + (uint32_t)(samplesFrac >> MIX_FRAC_BITS);
	ch->frac = (uint32_t)(samplesFrac & MIX_FRAC_MASK);

	if (ch->m_pos >= ch->m_end)
	{
		if (ch->m_loop != 65535)
		{
			const uint32_t loopLength = (uint32_t)(ch->m_end - ch->m_loop);
			do
			{
				ch->m_pos -= loopLength;
			}
			while (ch->m_pos >= ch->m_end);
		}
		else // no loop
		{
			ch->m_speed = 0; // stop sample
		}
	}
}

void SBPro_Mix(float *fMixBufL, float *fMixBufR, int32_t numSamples)
{
	zchn_t *ch = song._zchn;
	for (uint32_t i = 0; i < 16; i++, ch++)
	{
		if (ch->m_speed == 0 || ch->m_pos == 0xFFFFFFFF || ch->m_base == NULL || ch->m_pos >= ch->m_end)
			continue;
		
		// if current volume is zero, do fast silence mixing (only update position/frac)
		if (ch->m_vol == 0)
		{
			SBPro_SilenceMix(ch, numSamples);
			continue;
		}

		SBPro_MixCh(ch, fMixBufL, fMixBufR, numSamples);
	}
}
