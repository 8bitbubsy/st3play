// channel mixer for Sound Blaster Pro mode

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include "../dig.h"

static const uint8_t xvol_st3[65] =
{
	0,4,8,12,16,20,24,28,32,36,40,44,48,52,56,60,
	64,68,72,76,80,85,89,93,97,101,105,109,113,117,
	121,125,129,133,137,141,145,149,153,157,161,165,
	170,174,178,182,186,190,194,198,202,206,210,214,
	218,222,226,230,234,238,242,246,250,255,

	0 // added this, just in case
};

static bool stereoMode;
static float fSampleBufferL[SINC_WIDTH], fSampleBufferR[SINC_WIDTH], fResampleRatio, fSampleAccum, fSincPhaseMul;
static double dSBProOutputRate;

void SBPro_Init(int32_t audioOutputFrequency, uint8_t timeConstant, bool stereo)
{
	stereoMode = stereo;
	dSBProOutputRate = 1000000.0 / (256 - timeConstant);
	fResampleRatio = (float)(audioOutputFrequency / dSBProOutputRate);
	fSincPhaseMul = SINC_PHASES / fResampleRatio;
	fSampleAccum = 0.0f;
}

bool SBPro_StereoMode(void)
{
	return stereoMode;
}

double SBPro_GetOutputRate(void)
{
	return dSBProOutputRate;
}

static void outputSBProSample(float *outL, float *outR)
{
	int16_t L = 0, R = 0;

	zchn_t *ch = song._zchn;
	for (int32_t i = 0; i < 16; i++, ch++)
	{
		if (ch->m_speed == 0 || ch->m_pos == 0xFFFFFFFF || ch->m_base == NULL || ch->m_pos >= ch->m_end)
			continue;

		const int16_t output = (ch->m_base[ch->m_pos] * (int16_t)xvol_st3[ch->m_vol]) >> 8;
		if (stereoMode)
		{
			if (ch->amixtype == 1) // swap output channels for this voice (efx SA1 = swap L/R, SA0 = normal)
			{
				if (ch->channelnum < 8)
					L += output;
				else
					R += output;
			}
			else
			{
				if (ch->channelnum < 8)
					R += output;
				else
					L += output;
			}
		}
		else
		{
			L += output;
			R += output;
		}

		ch->m_poslow += ch->m_speed;
		ch->m_pos += ch->m_poslow >> 16;
		ch->m_poslow &= 0xFFFF;

		if (ch->m_pos >= ch->m_end)
		{
			if (ch->m_loop != 65535) // loop enabled?
			{
				const uint32_t loopLength = (uint32_t)(ch->m_end - ch->m_loop);
				const uint32_t overflowSamples = ch->m_pos - ch->m_end;

				ch->m_pos = ch->m_loop + (overflowSamples % loopLength);
			}
			else // no loop
			{
				ch->m_speed = 0; // stop sample
			}
		}
	}

	L = (L * audio.mastermul) >> 7;
	R = (R * audio.mastermul) >> 7;

	*outL = L * (0.5f / 128.0f);
	*outR = R * (0.5f / 128.0f);
}

static void SBPro_Output(float *outL, float *outR)
{
	while (fSampleAccum >= fResampleRatio)
	{
		fSampleAccum -= fResampleRatio;

		// advance resampling ring buffer
		for (int32_t i = 0; i < SINC_WIDTH-1; i++)
		{
			fSampleBufferL[i] = fSampleBufferL[1+i];
			fSampleBufferR[i] = fSampleBufferR[1+i];
		}

		float inL, inR;
		outputSBProSample(&inL, &inR);

		fSampleBufferL[SINC_WIDTH-1] = inL;
		fSampleBufferR[SINC_WIDTH-1] = inR;
	}

	const uint32_t phase = (int32_t)(fSampleAccum * fSincPhaseMul);
	assert(phase < SINC_PHASES);
	fSampleAccum += 1.0;

	const float *sL = fSampleBufferL, *sR = fSampleBufferR;
	const float *l = &fSincLUT[phase << SINC_WIDTH_BITS];

	float L = 0.0f;
	float R = 0.0f;
	for (int32_t i = 0; i < SINC_WIDTH; i++)
	{
		const float c = l[i];
		L += sL[i] * c;
		R += sR[i] * c;
	}

	*outL = L;
	*outR = R;
}

void SBPro_RenderSamples(float *fMixBufL, float *fMixBufR, int32_t numSamples)
{
	for (int32_t i = 0; i < numSamples; i++)
		SBPro_Output(fMixBufL++, fMixBufR++);
}
