// channel mixer for Sound Blaster Pro mode

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../dig.h"

#define ST3_PCM_CHANNELS 16

static const uint8_t xvol_st3[64+1] =
{
	0,4,8,12,16,20,24,28,32,36,40,44,48,52,56,60,
	64,68,72,76,80,85,89,93,97,101,105,109,113,117,
	121,125,129,133,137,141,145,149,153,157,161,165,
	170,174,178,182,186,190,194,198,202,206,210,214,
	218,222,226,230,234,238,242,246,250,255,

	255 // overflow byte added just in case volume is >63 (shouldn't happen)
};

static int8_t postTable[2048];
static float fSampleBufferL[SINC_WIDTH], fSampleBufferR[SINC_WIDTH];
static double dResampleRatio, dSampleAccum, dSincPhaseMul, dSBProOutputRate;

void SBPro_Init(int32_t audioOutputFrequency, uint8_t timeConstant)
{
	dSBProOutputRate = 1000000.0 / (256 - timeConstant);
	dResampleRatio = (double)audioOutputFrequency / dSBProOutputRate;
	dSincPhaseMul = SINC_PHASES / dResampleRatio;
	dSampleAccum = 0.0;

	// create post table (aka. "squeeze volume table")

	uint8_t mastervol = song.header.mastermul & 127;
	if (mastervol < 16)
		mastervol = 16;

	const uint16_t c = (2048 * 16) / mastervol;
	const uint16_t a = (2048 - c) / 2;
	const uint16_t b = a + c;
	const uint16_t delta16 = (uint16_t)(65536 / c);

	uint16_t smp16 = 0;
	for (int32_t i = 0; i < 2048; i++)
	{
		if (i < a)
		{
			postTable[i] = -128;
		}
		else if (i < b)
		{
			postTable[i] = (smp16 >> 8) ^ 0x80;
			smp16 += delta16;
		}
		else
		{
			postTable[i] = 127;
		}
	}
}

double SBPro_GetOutputRate(void)
{
	return dSBProOutputRate;
}

static void outputSBProSample(float *fOutL, float *fOutR)
{
	uint16_t L = 1024, R = 1024;

	zchn_t *ch = song._zchn;
	for (int32_t i = 0; i < ST3_PCM_CHANNELS; i++, ch++)
	{
		if (ch->m_speed == 0 || ch->m_pos == 0xFFFFFFFF || ch->m_base == NULL || ch->m_pos >= ch->m_end)
			continue;

		const int16_t smp = (ch->m_base[ch->m_pos] * (int16_t)xvol_st3[ch->m_vol]) >> 8;
		if (song.stereomode)
		{
			if (ch->amixtype == 0 || ch->amixtype == 2)
			{
				// normal mix
				if (ch->channelnum >= 8)
					L += smp;
				else
					R += smp;
			}
			else if (ch->amixtype == 1 || ch->amixtype == 3)
			{
				// swap L/R channels
				if (ch->channelnum < 8)
					L += smp;
				else
					R += smp;
			}
			else
			{
				// centered
				L += smp;
				R += smp;
			}
		}
		else
		{
			// centered (mono mode)
			L += smp;
			R += smp;
		}

		ch->m_poslow += ch->m_speed;
		ch->m_pos += ch->m_poslow >> 16;
		ch->m_poslow &= 0xFFFF;

		if (ch->m_pos >= ch->m_end)
		{
			if ((uint16_t)ch->m_loop != 65535) // loop enabled?
				ch->m_pos += (int16_t)(ch->m_loop - ch->m_end);
			else // no loop
				ch->m_speed = 0; // stop sample
		}
	}

	// just in case of non-ST3 channel mapping (mix overflow)
	L &= 2047;
	R &= 2047;

	const float f8L = postTable[L];
	const float f8R = postTable[R];

	if (song.adlibused) // give some headroom for OPL output
	{
		*fOutL = f8L * ((2.0f/3.0f) / 128.0f);
		*fOutR = f8R * ((2.0f/3.0f) / 128.0f);
	}
	else
	{
		*fOutL = f8L * (1.0f / 128.0f);
		*fOutR = f8R * (1.0f / 128.0f);
	}
}

static void SBPro_Output(float *outL, float *outR)
{
	while (dSampleAccum >= dResampleRatio)
	{
		dSampleAccum -= dResampleRatio;

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

	const uint32_t phase = (int32_t)(dSampleAccum * dSincPhaseMul);
	assert(phase < SINC_PHASES);
	dSampleAccum += 1.0;

	const float *sL = fSampleBufferL, *sR = fSampleBufferR;
	const float *l = &fSincLUT[phase << SINC_WIDTH_BITS];

	float L = 0.0f, R = 0.0f;
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
