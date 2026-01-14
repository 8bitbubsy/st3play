/*
** Simple 'Gravis Ultrasound' GF1 emulator for st3play
**
** This emulator has just enough logic implemented to support st3play.
** In other words, it's lacking a lot of stuff that we don't need.
**
** Built using the excellent low-level hardware documentation found in
** "InterWave IC Am78C201 Programmers Guide v2, (SDK) 1996.pdf"
** (https://16-bits.org/etc/guspnp.pdf).
**
** WARNING: This code is *not* thread-safe, and GUS functions shall
** only be called from the thread that calls GUS_RenderSamples() itself!
** We only use it like this in st3play anyway.
*/

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "gus_gf1.h"
#include "../dig.h"
#include "../digread.h"

#define GF1_MIN_VOICES 14
#define GF1_MAX_VOICES 32
#define GF1_SMP_ADD_FRAC_BITS 9
#define GF1_SMP_ADD_FRAC_MASK ((1 << GF1_SMP_ADD_FRAC_BITS)-1)
#define GF1_VOL_FRAC_BITS 3

enum // GUS GF1 voice/volume control flags (the ones needed for ST3 playback, that is)
{
	SACI_STOPPED         = 1,
	SACI_STOP            = 2,
	SACI_LOOP_FWD        = 8,
	SVCI_STOPPED         = 1,
	SVCI_STOP            = 2,
	SVCI_DECREASING_RAMP = 64
};

typedef struct gusVoice_t
{
	const int8_t *SA; // current address
	const int8_t *SAS; // start address (used when loop is enabled)
	const int8_t *SAE; // end address
	uint16_t SA_frac; // current address fraction (6.9fp)
	uint8_t SACI, SVCI; // voice/volume control (flags)
	uint16_t LOff, ROff; // current pan offsets
	uint16_t SVRI; // volume rate
	uint16_t SVLI; // current volume
	uint16_t SVSI; // volume start
	uint16_t SVEI; // volume end
	uint16_t SFCI; // frequency/delta (6.9fp)
} gusVoice_t;

// formula: x = round[-128 * log2((15-x) / 15)]
static const int16_t panOffsTable[16] =
{
	   0,   13,   26,   41,
	  57,   75,   94,  116,
	 141,  169,  203,  244,
	 297,  372,  500, 4095
};

static bool resamplingNeeded;
static int32_t activeVoices, gusOutputRate;
static float fSampleBufferL[SINC_WIDTH], fSampleBufferR[SINC_WIDTH], fResampleRatio, fSampleAccum, fSincPhaseMul;
static double dGUSOutputRate;
static gusVoice_t gusVoice[GF1_MAX_VOICES];
static gusVoice_t *gv = gusVoice; // initialize to voice #0

void GUS_VoiceSelect(int32_t voiceNum)
{
	gv = &gusVoice[CLAMP(voiceNum, 0, activeVoices-1)];
}

void GUS_SetFrequency(uint16_t freq) // 6.10fp
{
	gv->SFCI = freq >> 1; // GUS GF1: LSB is not used
}

void GUS_SetCurrVolume(uint16_t volume) // 12.4fp
{
	gv->SVLI = volume >> 1; // GUS GF1: LSB is not used
}

void GUS_SetStartVolume(uint8_t volume)
{
	gv->SVSI = volume << (4+GF1_VOL_FRAC_BITS);
}

void GUS_SetEndVolume(uint8_t volume)
{
	gv->SVEI = volume << (4+GF1_VOL_FRAC_BITS);
}

void GUS_SetVolumeRate(uint8_t rate)
{
	gv->SVRI = (rate & 63) << (GF1_VOL_FRAC_BITS - (rate >> 6));
}

void GUS_SetBalance(uint8_t balance) // 0..15
{
	balance &= 15;
	gv->LOff = panOffsTable[   balance];
	gv->ROff = panOffsTable[15-balance];
}

const int8_t *GUS_GetCurrAddress(void)
{
	return (int8_t *)gv->SA;
}

void GUS_SetCurrAddress(const int8_t *address)
{
	gv->SA = address;
	gv->SA_frac = 0;
}

void GUS_SetStartAddress(const int8_t *address) // loop start
{
	gv->SAS = address;
}

void GUS_SetEndAddress(const int8_t *address)
{
	gv->SAE = (int8_t *)address;
}

void GUS_SetVolumeCtrl(uint8_t flags)
{
	gv->SVCI = flags;
}

void GUS_SetVoiceCtrl(uint8_t flags)
{
	gv->SACI = flags;
}

uint8_t GUS_GetVoiceCtrl(void)
{
	return gv->SACI;
}

void GUS_Init(int32_t audioOutputFrequency, int32_t numVoices)
{
	if (audioOutputFrequency <= 0)
		audioOutputFrequency = 44100;

	numVoices = CLAMP(numVoices, GF1_MIN_VOICES, GF1_MAX_VOICES);

	lockMixer();

	// set defaults register values

	gusVoice_t *v = gusVoice;
	for (int32_t i = 0; i < GF1_MAX_VOICES; i++, v++)
	{
		v->SA = v->SAS = v->SAE = NULL;
		v->SA_frac = 0;
		v->SACI = SACI_STOPPED;
		v->SVCI = SVCI_STOPPED;
		v->LOff = panOffsTable[   7];
		v->ROff = panOffsTable[15-7];
		v->SVRI = v->SVLI = v->SVSI = v->SVEI = 0;
		v->SFCI = 1 << GF1_SMP_ADD_FRAC_BITS; // 1.0
	}

	gv = &gusVoice[0]; // currently selected voice = first voice

#if 0
	activeVoices = numVoices;
	dGUSOutputRate = (14.0 * 44100.0) / activeVoices;
#else
	// hack for buggy ST3 GUS driver
	activeVoices = 32;
	dGUSOutputRate = (14.0 * 44100.0) / 16; // 38587.5Hz (ST3 default)
#endif

	resamplingNeeded = ((double)audioOutputFrequency != dGUSOutputRate);
	if (resamplingNeeded)
	{
		fResampleRatio = (float)(audioOutputFrequency / dGUSOutputRate);
		fSincPhaseMul = SINC_PHASES / fResampleRatio;
		fSampleAccum = 0.0f;
	}

	unlockMixer();
}

double GUS_GetOutputRate(void)
{
	return dGUSOutputRate;
}

int32_t GUS_GetNumberOfVoices(void)
{
	return activeVoices;
}

int32_t GUS_GetNumberOfRunningVoices(void)
{
	int32_t voices = 0;

	gusVoice_t *v = gusVoice;
	for (int32_t i = 0; i < activeVoices; i++, v++)
	{
		if (!(v->SACI & SACI_STOPPED))
			voices++;
	}

	return voices;
}

static void outputGUSSample(float *outL, float *outR)
{
	int32_t L = 0, R = 0;

	gusVoice_t *v = gusVoice;
	for (int32_t i = 0; i < activeVoices; i++, v++)
	{
		if (v->SACI & SACI_STOP) v->SACI |= SACI_STOPPED;
		if (v->SVCI & SVCI_STOP) v->SVCI |= SVCI_STOPPED;

		if (v->SA == NULL || v->SAE == NULL || ((v->SACI & SACI_LOOP_FWD) && v->SAS != NULL))
			v->SACI |= SACI_STOPPED;
		
		if (!(v->SACI & SACI_STOPPED)) // run sample engine
		{
			const uint16_t vol = v->SVLI >> GF1_VOL_FRAC_BITS;
			uint16_t volL = vol - v->LOff;
			uint16_t volR = vol - v->ROff;
			volL &= ~((int16_t)volL >> 15);
			volR &= ~((int16_t)volR >> 15);

			// linear interpolation
			int16_t sample2, sample = v->SA[0] << 8;
			if (v->SA+1 >= v->SAE)
			{
				if (v->SACI & SACI_LOOP_FWD)
					sample2 = v->SAS[0] << 8;
				else
					sample2 = 0;
			}
			else
			{
				sample2 = v->SA[1] << 8;
			}
			sample += ((sample2-sample) * (int16_t)v->SA_frac) >> GF1_SMP_ADD_FRAC_BITS;

			// this is how GUS GF1 (or at least GUS PnP) does its volume conversion
			L += (sample * (256 + (volL & 0xFF))) >> (24 - (volL >> 8));
			R += (sample * (256 + (volR & 0xFF))) >> (24 - (volR >> 8));

			v->SA_frac += v->SFCI;
			v->SA += v->SA_frac >> GF1_SMP_ADD_FRAC_BITS;
			v->SA_frac &= GF1_SMP_ADD_FRAC_MASK;

			// handle end-of-sample
			if (v->SA >= v->SAE)
			{
				if (v->SACI & SACI_LOOP_FWD)
				{
					const uint32_t overflowSamples = (uint32_t)(v->SA - v->SAE);

					uint32_t loopLength = (uint32_t)(v->SAE - v->SAS);
					if (loopLength == 0)
						v->SA = v->SAS;
					else
						v->SA = v->SAS + (overflowSamples % loopLength);
				}
				else // no loop
				{
					v->SACI |= SACI_STOPPED;
				}
			}
		}

		if (!(v->SVCI & SVCI_STOPPED)) // run volume ramping engine
		{
			if (v->SVCI & SVCI_DECREASING_RAMP)
			{
				v->SVLI -= v->SVRI;
				if ((int16_t)v->SVLI <= (int16_t)v->SVSI)
				{
					v->SVLI = v->SVSI;
					v->SVCI |= SVCI_STOPPED;
				}
			}
			else
			{
				v->SVLI += v->SVRI;
				if (v->SVLI >= v->SVEI)
				{
					v->SVLI = v->SVEI;
					v->SVCI |= SVCI_STOPPED;
				}
			}
		}
	}

	*outL = (float)L * (0.5f / 32768.0f);
	*outR = (float)R * (0.5f / 32768.0f);
}

static void GUS_Output(float *outL, float *outR)
{
	float L, R;
	if (resamplingNeeded)
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
			outputGUSSample(&inL, &inR);

			fSampleBufferL[SINC_WIDTH-1] = inL;
			fSampleBufferR[SINC_WIDTH-1] = inR;
		}

		const uint32_t phase = (int32_t)(fSampleAccum * fSincPhaseMul);
		assert(phase < SINC_PHASES);
		fSampleAccum += 1.0;

		const float *sL = fSampleBufferL, *sR = fSampleBufferR;
		const float *l = &fSincLUT[phase << SINC_WIDTH_BITS];

		L = R = 0.0f;
		for (int32_t i = 0; i < SINC_WIDTH; i++)
		{
			const float c = l[i];
			L += sL[i] * c;
			R += sR[i] * c;
		}
	}
	else
	{
		outputGUSSample(&L, &R);
	}

	*outL = L;
	*outR = R;
}

void GUS_RenderSamples(float *fMixBufL, float *fMixBufR, int32_t numSamples)
{
	for (int32_t i = 0; i < numSamples; i++)
		GUS_Output(fMixBufL++, fMixBufR++);
}
