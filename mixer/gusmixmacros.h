#pragma once

#define GUS_GET_MIXBUF \
	fAudioMixL = fMixBufL; \
	fAudioMixR = fMixBufR;

#define GUS_FIX_LERP_SAMPLE \
	const uint8_t oldSample = *v->SAE; \
	if (v->SACI & SACI_LOOP_FWD) \
		*v->SAE = *v->SAS; \
	else \
		*v->SAE = 0;

#define GUS_RESTORE_LERP_SAMPLE \
	*v->SAE = oldSample;

#define GUS_INC_POS \
	v->ADDfr += v->SFCI; \
	v->SA += v->ADDfr >> MIX_FRAC_BITS; \
	v->ADDfr &= MIX_FRAC_MASK;

#define GUS_RAMP_UP \
	v->SVLI += v->SVRI; \
	if (v->SVLI >= v->SVEI) \
	{ \
		v->SVLI = v->SVEI; /* destination reached */ \
		v->SVCI |= SVCI_STOPPED; /* turn off volume ramping */ \
	}

#define GUS_RAMP_DOWN \
	v->SVLI -= v->SVRI; \
	if (v->SVLI <= v->SVSI) \
	{ \
		v->SVLI = v->SVSI; /* destination reached */ \
		v->SVCI |= SVCI_STOPPED; /* turn off volume ramping */ \
	}

#define GUS_GET_LINEAR_VOLUME \
	const int32_t vol = v->SVLI >> VOL_FRAC_BITS; \
	logVolL = vol - v->LOff; \
	logVolR = vol - v->ROff; \
	logVolL &= ~(logVolL >> 31); \
	logVolR &= ~(logVolR >> 31); \
	fVolL = fLog2LinearVolTable[logVolL]; \
	fVolR = fLog2LinearVolTable[logVolR];

#define GUS_GET_SMP \
	fSample  = v->SA[0] * (1.0f / 128.0f); \
	fSample2 = v->SA[1] * (1.0f / 128.0f); \
	fSample += (fSample2-fSample) * ((int32_t)((uint32_t)v->ADDfr >> 1) * (1.0f / (MIX_FRAC_SCALE/2.0f)));

#define GUS_RENDER_SMP \
	GUS_GET_SMP \
	*fAudioMixL++ += fSample * fVolL; \
	*fAudioMixR++ += fSample * fVolR;

#define GUS_RENDER_SMP_RAMP \
	GUS_GET_SMP \
	GUS_GET_LINEAR_VOLUME \
	*fAudioMixL++ += fSample * fVolL; \
	*fAudioMixR++ += fSample * fVolR;

#define GUS_LIMIT_MIX_NUM \
	samplesToMix = INT32_MAX; \
	if (v->SFCI > 0) \
	{ \
		i = (int32_t)(v->SAE-v->SA); /* samples until end address (always 8-bit!) */ \
		uint64_t tmp64 = ((uint64_t)i << MIX_FRAC_BITS) - v->ADDfr; \
		samplesToMix = (int32_t)(tmp64 / v->SFCI) + 1; \
	} \
	\
	if (samplesToMix > samplesLeft) \
		samplesToMix = samplesLeft; \
	\
	samplesLeft -= samplesToMix;

#define GUS_HANDLE_SAMPLE_END \
	if (v->SA >= v->SAE) \
	{ \
		if (v->SACI & SACI_LOOP_FWD) \
		{ \
			const uint32_t loopLength = (uint32_t)(v->SAE-v->SAS); /* always 8-bit! */ \
			do \
			{ \
				v->SA -= loopLength; \
			} \
			while (v->SA >= v->SAE); \
		} \
		else /* no loop */ \
		{ \
			v->SA = v->SAE-1; \
			v->SACI |= SACI_STOPPED; /* turn off voice */ \
			GUS_RESTORE_LERP_SAMPLE \
			break; \
		} \
	}
