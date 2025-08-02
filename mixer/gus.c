/*
** Simple 'Gravis Ultrasound' GF1 emulator for st3play - by 8bitbubsy
**
** NOTE: This code expects that there's room for 1 extra sample
**       after the end of the allocated sample data! This sample
**       is sometimes written to before entering the mixing loop,
**       and restored afterwards.
**
** This emulator has just enough logic implemented to support st3play.
** In other words, it's lacking a lot of stuff that we don't need.
**
** Built using the excellent low-level hardware documentation found in
** "InterWave IC Am78C201 Programmers Guide v2, (SDK) 1996.pdf"
** (https://16-bits.org/etc/guspnp.pdf).
**
** Even though that PDF is a documentation of GUS PnP, most of the things
** are the same for GUS GF1. There are some differences though, like
** 6.10fp position precision instead of 6.9fp (GUS GF1), etc.
**
** Mixing macros can be found in gusmixmacros.h.
**
** WARNING: This code is *not* thread-safe, and GUS functions shall
** only be called from the thread that calls GUS_Mix() itself!
** We only use it like this in st3play anyway.
*/

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "gus.h"
#include "gusmixmacros.h"
#include "../dig.h"
#include "../digread.h"

#define GF1_SAMPLE_CLK_HZ 19756800 /* crystal clock, in hertz */
#define GF1_SAMPLE_CYCLES 32 /* amount of clocks taken to process one voice sample */

// GUS GF1 has 3 extra bits of volume precision (for ramping)
#define GUS_VOL_FRAC_BITS 3

// actual bits of volume precision in our mixer
#define VOL_FRAC_BITS 18

enum // GUS GF1 voice control flags (the ones needed for ST3 playback, that is)
{
	SACI_STOPPED  = 1,
	SACI_STOP     = 2,
	SACI_LOOP_FWD = 8
};

enum // GUS GF1 volume control flags (the ones needed for ST3 playback, that is)
{
	SVCI_STOPPED = 1,
	SVCI_STOP    = 2,
	SVCI_DECREASING_RAMP = 64
};

typedef struct gusVoice_t
{
	const int8_t *SA; // current address
	const int8_t *SAS; // start address
	int8_t *SAE; // end address (must ne modifiable)
	uint8_t SACI; // voice control (flags)
	uint8_t SVCI; // volume control (flags)
	uint16_t LOff, ROff; // current pan offsets
	int32_t SVRI; // volume rate
	int32_t SVLI; // current volume
	int32_t SVSI; // volume start
	int32_t SVEI; // volume end
	uint64_t SFCI; // frequency/delta (GUS GF1 = 6.9fp, but we do 32.32fp)
	uint64_t ADDfr; // position fraction (GUS GF1 = 9 bits, but we do 32 bits)
} gusVoice_t;

/* These exact rounded numbers are from
** AMD InterWave programmer's guide.
** These are possibly the values that the GF1
** chip uses internally..? It creates *very*
** accurate results compared to a real GUS GF1.
**
** Formula: x = round[-128 * log2((15-x) / 15)]
*/
static const int16_t panOffsTable[16] =
{
	   0,   13,   26,   41,
	  57,   75,   94,  116,
	 141,  169,  203,  244,
	 297,  372,  500, 4095
};

static bool volTableCalculated = false;
static float fLog2LinearVolTable[4096];
static double dGUSVolDelta2MixVolDelta;
static gusVoice_t gusVoice[MAX_GUS_VOICES];
static gusVoice_t *gv = gusVoice; // initialize to voice #0

void GUS_VoiceSelect(uint8_t voiceNum)
{
	if (voiceNum >= MAX_GUS_VOICES)
		voiceNum = MAX_GUS_VOICES-1;

	gv = &gusVoice[voiceNum];
}

void GUS_SetFrequency(uint64_t freq) // 32.32fp (upgraded from a lousy 6.9fp)
{
	gv->SFCI = freq;
}

void GUS_SetCurrVolume(uint16_t volume) // logarithmic
{
	const int32_t SVLI = volume >> 1; // the least significant bit is not used in GUS GF1
	
	gv->SVLI = SVLI << (VOL_FRAC_BITS-GUS_VOL_FRAC_BITS); // scaled to our true amount of bits
}

void GUS_SetStartVolume(uint8_t volume)
{
	gv->SVSI = volume << (4+VOL_FRAC_BITS); // scaled to our true amount of bits
}

void GUS_SetEndVolume(uint8_t volume)
{
	gv->SVEI = volume << (4+VOL_FRAC_BITS); // scaled to our true amount of bits
}

/* Note: This is in units of output samples (frames), so the
** actual ramp duration depends on the GUS mixing frequency.
*/
void GUS_SetVolumeRate(uint8_t rate)
{
	const int32_t delta = rate & ((1 << 6)-1);
	rate >>= 6;

	const int32_t SVRI = (delta << GUS_VOL_FRAC_BITS) >> rate;

	gv->SVRI = (int32_t)((SVRI * dGUSVolDelta2MixVolDelta) + 0.5);
}

void GUS_SetBalance(uint8_t balance) // 0..15, equal power panning (square-law)
{
	balance &= 0x0F;

	gv->LOff = panOffsTable[    balance];
	gv->ROff = panOffsTable[0xF-balance];
}

const int8_t *GUS_GetCurrAddress(void)
{
	return (int8_t *)gv->SA;
}

void GUS_SetCurrAddress(const int8_t *address)
{
	gv->SA = address;

	// clear fractional sampling position (we don't support setting it to a custom value anyway)
	gv->ADDfr = 0;
}

void GUS_SetStartAddress(const int8_t *address) // loop start
{
	gv->SAS = address;
}

void GUS_SetEndAddress(const int8_t *address) // loop end
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

void GUS_StopVoices(void)
{
	lockMixer();

	memset(gusVoice, 0, sizeof (gusVoice));

	gusVoice_t *v = gusVoice;
	for (int32_t i = 0; i < MAX_GUS_VOICES; i++, v++)
	{
		v->SACI = SACI_STOPPED; // voice stopped
		v->SVCI = SVCI_STOPPED; // volume ramp stopped
	}

	gv = gusVoice; // current voice points to first voice

	unlockMixer();
}

void GUS_Reset(int32_t audioOutputFrequency)
{
	lockMixer();

	GUS_StopVoices();

	if (!volTableCalculated)
	{
		// Fast approximation for volume conversion. Seems to be the formula GUS GF1 uses internally (?).
		for (int32_t i = 0; i < 4096; i++)
		{
			const double dX = (double)(256 + (i & 0xFF)) / (double)(1 << (24 - (i >> 8))); // 0.0 (0) .. ~0.998 (4095)
			fLog2LinearVolTable[i] = (float)dX;
		}

		volTableCalculated = true;
	}

	if (audioOutputFrequency <= 0)
		audioOutputFrequency = 44100;

	// pretend we run at 16 voices while we actually use 32 (ST3 compatibility)
	const double dGUSRate = (GF1_SAMPLE_CLK_HZ / (double)GF1_SAMPLE_CYCLES) / 16;

	// calculate multipliers used to convert from GUS scales to our custom scales with more precision
	const double dFreqRatio = dGUSRate / audioOutputFrequency;
	dGUSVolDelta2MixVolDelta = (dFreqRatio / (double)(1 << GUS_VOL_FRAC_BITS)) * (double)(1 << VOL_FRAC_BITS);

	unlockMixer();
}

static void GUS_MixCh(gusVoice_t *v, float *fMixBufL, float *fMixBufR, int32_t numSamples)
{
	int32_t logVolL, logVolR, i, samplesToMix, samplesLeft;
	float fSample, fSample2, fVolL, fVolR, *fAudioMixL, *fAudioMixR;

	GUS_GET_MIXBUF
	GUS_GET_LINEAR_VOLUME
	GUS_FIX_LERP_SAMPLE

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		GUS_HANDLE_SAMPLE_END

		GUS_LIMIT_MIX_NUM
		for (i = 0; i < (samplesToMix & 3); i++)
		{
			GUS_RENDER_SMP
			GUS_INC_POS
		}
		samplesToMix >>= 2;
		for (i = 0; i < samplesToMix; i++)
		{
			GUS_RENDER_SMP
			GUS_INC_POS
			GUS_RENDER_SMP
			GUS_INC_POS
			GUS_RENDER_SMP
			GUS_INC_POS
			GUS_RENDER_SMP
			GUS_INC_POS
		}
	}

	GUS_RESTORE_LERP_SAMPLE
}

static void GUS_MixChRampUp(gusVoice_t *v, float *fMixBufL, float *fMixBufR, int32_t numSamples)
{
	int32_t logVolL, logVolR, i, samplesToMix, samplesLeft;
	float fSample, fSample2, fVolL, fVolR, *fAudioMixL, *fAudioMixR;

	GUS_GET_MIXBUF
	GUS_FIX_LERP_SAMPLE

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		GUS_HANDLE_SAMPLE_END
		GUS_LIMIT_MIX_NUM

		/* No need to unroll, really. Ramp mode happens in very short bursts in st3play.
		** Branch prediction will make it fast enough.
		*/
		for (i = 0; i < samplesToMix; i++)
		{
			GUS_RENDER_SMP_RAMP
			GUS_INC_POS
			GUS_RAMP_UP
		}
	}

	GUS_RESTORE_LERP_SAMPLE
}

static void GUS_MixChRampDown(gusVoice_t *v, float *fMixBufL, float *fMixBufR, int32_t numSamples)
{
	int32_t logVolL, logVolR, i, samplesToMix, samplesLeft;
	float fSample, fSample2, fVolL, fVolR, *fAudioMixL, *fAudioMixR;

	GUS_GET_MIXBUF
	GUS_FIX_LERP_SAMPLE

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		GUS_HANDLE_SAMPLE_END
		GUS_LIMIT_MIX_NUM

		/* No need to unroll, really. Ramp mode happens in very short bursts in st3play.
		** Branch prediction will make it fast enough.
		*/
		for (i = 0; i < samplesToMix; i++)
		{
			GUS_RENDER_SMP_RAMP
			GUS_INC_POS
			GUS_RAMP_DOWN
		}
	}

	GUS_RESTORE_LERP_SAMPLE
}

static void GUS_SilenceMix(gusVoice_t *v, int32_t numSamples)
{
	const uint64_t samplesToMix = (uint64_t)v->SFCI * (uint32_t)numSamples; // fixed-point

	const uint32_t samples = (uint32_t)(samplesToMix >> MIX_FRAC_BITS);
	const uint64_t samplesFrac = (samplesToMix & MIX_FRAC_MASK) + v->ADDfr;

	v->SA += samples + (uint32_t)(samplesFrac >> MIX_FRAC_BITS);
	v->ADDfr = (uint32_t)(samplesFrac & MIX_FRAC_MASK);

	if (v->SA >= v->SAE)
	{
		if (v->SACI & SACI_LOOP_FWD)
		{
			const uint32_t loopLength = (uint32_t)(v->SAE-v->SAS); // always 8-bit!
			do
			{
				v->SA -= loopLength;
			}
			while (v->SA >= v->SAE);
		}
		else // no loop
		{
			v->SA = v->SAE-1;
			v->SACI |= SACI_STOPPED;
		}
	}
}

static void doVolRampInactiveVoice(gusVoice_t *v, int32_t numSamples)
{
	int64_t SVLI64 = v->SVLI;
	const uint64_t delta64 = (uint64_t)v->SVRI * (uint32_t)numSamples;

	if (v->SVCI & SVCI_DECREASING_RAMP)
	{
		SVLI64 -= delta64;
		if (SVLI64 <= v->SVSI)
		{
			SVLI64 = v->SVSI; /* destination reached */
			v->SVCI |= SVCI_STOPPED; /* turn off volume ramping */
		}
	}
	else
	{
		SVLI64 += delta64;
		if (SVLI64 >= v->SVSI)
		{
			SVLI64 = v->SVSI; /* destination reached */
			v->SVCI |= SVCI_STOPPED; /* turn off volume ramping */
		}
	}

	v->SVLI = (int32_t)SVLI64;
}

void GUS_Mix(float *fMixBufL, float *fMixBufR, int32_t numSamples)
{
	gusVoice_t *v = gusVoice;
	for (int32_t i = 0; i < MAX_GUS_VOICES; i++, v++)
	{
		const bool voiceActive   = !(v->SACI & (SACI_STOP | SACI_STOPPED));
		const bool volRampActive = !(v->SVCI & (SVCI_STOP | SVCI_STOPPED));

		if (!voiceActive)
		{
			// voice is not active, but handle volume ramping (if active)
			if (volRampActive)
				doVolRampInactiveVoice(v, numSamples);

			continue;
		}

		// sanity checking
		if (v->SA == NULL || v->SAE == NULL || ((v->SACI & SACI_LOOP_FWD) && v->SAS == NULL))
		{
			if (volRampActive)
				doVolRampInactiveVoice(v, numSamples);

			continue;
		}

		if (volRampActive) // volume ramp is ongoing
		{
			/* Since we don't support looped volume ramping (ST3 doesn't use it), it's
			** fine to test the direction once before entering the mixing loop.
			*/
			if (v->SVCI & SVCI_DECREASING_RAMP)
				GUS_MixChRampDown(v, fMixBufL, fMixBufR, numSamples);
			else
				GUS_MixChRampUp(v, fMixBufL, fMixBufR, numSamples);
		}
		else // no ongoing volume ramping
		{
			// if current volume is (close to) zero, do fast silence mixing (only update position/frac)
			if ((v->SVLI >> VOL_FRAC_BITS) <= 256) // lowest ST3 GUS volume (0), handle as "zero amplitude" (linear amplitude of <=0.00003)
			{
				GUS_SilenceMix(v, numSamples);
				continue;
			}

			GUS_MixCh(v, fMixBufL, fMixBufR, numSamples);
		}
	}
}

int32_t activeGUSVoices(void) // this routine only makes sense for use with st3play
{
	int32_t activeVoices = 0;

	gusVoice_t *v = gusVoice;
	for (int32_t i = 0; i < MAX_GUS_VOICES; i++, v++)
	{
		if (v->SACI & (SACI_STOP | SACI_STOPPED))
			continue; // voice is not active

		if (v->SA == NULL || v->SAE == NULL || ((v->SACI & SACI_LOOP_FWD) && v->SAS == NULL))
			continue;

		if ((v->SVLI >> VOL_FRAC_BITS) <= 256) // lowest ST3 GUS volume (0), handle as "zero amplitude" (linear amplitude of <=0.00003)
			continue;

		activeVoices++;
	}

	return activeVoices;
}
