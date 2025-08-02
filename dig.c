;/***********************************************************************
 **
 **  Main file
 **
 **  (8bb: .ASM -> .C, some files removed)
 **
 **  Contents of DIG*.C files:
 **
 **  DIG.C          general/main
 **  DIGAMG.C       amiga note handling
 **  DIGADL.C       adlib note handling
 **  DIGCMD.C       commands (effects)
 **  DIGDATA.C      data variables
 **  DIGREAD.C      reading new stuff from pattern
 **
 ***********************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dig.h"
#include "digcmd.h"
#include "digread.h"
#include "digdata.h"
#include "dig_gus.h"
#include "digadl.h"
#include "mixer/gus.h"
#include "mixer/sbpro.h"
#include "opl2/opl2.h"

static int32_t randSeed;
static double dPrngStateL, dPrngStateR;

static void setmasterflags(void)
{
	song.masterflags = song.header.flags;

	song.fastvolslide = !!(song.masterflags & 64);

	if (song.masterflags & 16)
	{
		// limit to amiga limits
		song.amigalimits = true;
		song.aspdmin = 453;
		song.aspdmax = 3424;
	}
	else
	{
		song.amigalimits = false;
		song.aspdmin = 64;
		song.aspdmax = 32767;
	}
}

static void checkheader(void)
{
	// ST3.21 defaults (not actually set in this function, but let's do it here)
	song.masterflags = 0;
	audio.mastermul = 48;
	song.header.ultraclick = 16;
	setspeed(6);
	settempo(125);
	setglobalvol(64);
	setMixingVol(audio.mixingVol);
	// --------------------------

	song.stereomode = false;
	if (song.header.mastermul != 0)
	{
		audio.mastermul = song.header.mastermul & 127;

		song.stereomode = !!(song.header.mastermul & 128);
		if (song.stereomode) // multiply mastermul by 11/8 -2 (30->41) {if STEREO/SBPRO}
		{
			uint16_t mastermul = audio.mastermul;

			mastermul *= 11;
			mastermul >>= 3;
			mastermul -= 2; // (just to make 30 => 40)

			if (mastermul > 127)
				mastermul = 127;

			audio.mastermul = (uint8_t)mastermul;
		}

		setMixingVol(audio.mixingVol);
	}

	if (song.header.inittempo != 0)
		settempo(song.header.inittempo);

	if (song.header.initspeed != 255)
		setspeed(song.header.initspeed);

	if (song.header.globalvol != 255)
		setglobalvol(song.header.globalvol);

	if (song.header.flags != 255)
		setmasterflags();

	if (song.header.ultraclick != 16 && song.header.ultraclick != 24 && song.header.ultraclick != 32)
		song.header.ultraclick = 16;
}

void loadheaderparms(void) // and variables
{
	song.oldstvib = !!(song.header.flags & 1);
	checkheader();
}

void loadheaderpans(void) // pannings
{
	zchn_t *c = song._zchn;
	for (int32_t i = 0; i < 32; i++, c++)
	{
		if (song.defaultpan[i] & 32)
			c->apanpos = 0xF0 | (song.defaultpan[i] & 0xF); // 8bb: the 0xF0 part means that this channel has a pan set
	}
}

void setglobalvol(int8_t vol)
{
	song.globalvol = vol;

	if ((uint8_t)vol > 64)
		vol = 64;

	song.useglobalvol = ((unsigned)vol * (256*4)) >> 8; // 8bb: 0..256, for setvol()
}

void shutupsounds(void)
{
	lockMixer();

	memset(song._zchn, 0, sizeof (song._zchn));

	zchn_t *ch = song._zchn;
	for (int8_t i = 0; i < ACHANNELS; i++, ch++)
	{
		ch->channelnum = i;
		ch->achannelused = 128;
		ch->aguschannel = -1;
		ch->lastadlins = 101;
		ch->m_oldvol = 255; // 8bb: from shutupsounds2() (GUS), but let's put it here
		ch->m_oldpos = 0xFFFFFFFF; // 8bb: added this (also for GUS)
	}

	if (audio.soundcardtype == SOUNDCARD_GUS)
	{
		gcmd_inittables();
		GUS_StopVoices();
	}

	song.lastachannelused = 1;

	unlockMixer();
}

static void initmodule(void)
{
	song.jmptoord = -1;
	song.musiccount = 0;
	song.patterndelay = 0;
	song.patloopstart = 0;
	song.patloopcount = 0;
	song.np_row = 0;
	song.np_pat = 0;
	song.startrow = 0;
	song.breakpat = 0; // 8bb: added this one
	shutupsounds();

	song.KxyLxxVolslideType = 0; // 8bb: added this for Kxy/Lxx effect (ST3 uses BP register for this)
}

void updateregs(void) // adlib/gravis
{
	zchn_t *ch = song._zchn;
	for (int32_t i = 0; i < ACHANNELS; i++, ch++)
	{
		ch->achannelused &= 127;

		if (audio.soundcardtype == SOUNDCARD_GUS)
			gcmd_update(ch); // 8bb: update GUS registers (dig_gus.c)
	}

	if (audio.soundcardtype == SOUNDCARD_GUS)
		gcmd_update(NULL); // 8bb: trigger GUS voices (dig_gus.c)

	if (song.adlibused)
		updateadlib();
}

uint16_t roundspd(zchn_t *ch, uint16_t spd) // 8bb: for Gxx with semitones-slide enabled
{
	uint32_t newspd = spd * ch->ac2spd;
	if ((newspd >> 16) >= C2FREQ)
		return spd; // 8bb: div error

	newspd /= C2FREQ;

	// find octave

	int8_t octa = 0;
	uint16_t lastspd = (notespd[12] + notespd[11]) >> 1;

	while (lastspd >= newspd)
	{
		octa++;
		lastspd >>= 1;
	}

	// find note

	int8_t newnote = 0;
	int16_t notemin = 32767;

	for (int8_t i = 0; i < 11; i++)
	{
		int16_t note = notespd[i];
		if (octa > 0)
			note >>= octa;

		note -= (int16_t)newspd;
		if (note < 0)
			note = -note;

		if (note < notemin)
		{
			notemin = note;
			newnote = i;
		}
	}

	// get new speed from new note

	newspd = stnote2herz((octa << 4) | (newnote & 0x0F)) * C2FREQ;
	if ((newspd >> 16) >= ch->ac2spd)
		return spd; // 8bb: div error

	newspd /= ch->ac2spd;
	return (uint16_t)newspd;
}

uint16_t scalec2spd(zchn_t *ch, uint16_t spd)
{
	uint32_t tmpspd = spd * C2FREQ;
	if ((tmpspd >> 16) >= ch->ac2spd)
		return 32767; // 8bb: div error

	tmpspd /= ch->ac2spd;
	if (tmpspd > 32767)
		tmpspd = 32767;

	return (uint16_t)tmpspd;
}

void setspd(zchn_t *ch)
{
	ch->achannelused |= 128;

	const bool amigalimits = !!(song.masterflags & 16);

	if (amigalimits)
	{
		if ((uint16_t)ch->aorgspd > song.aspdmax)
			ch->aorgspd = song.aspdmax;

		if (ch->aorgspd < song.aspdmin)
			ch->aorgspd = song.aspdmin;
	}

	int16_t tmpspd = ch->aspd;
	if ((uint16_t)tmpspd > song.aspdmax)
	{
		tmpspd = song.aspdmax;
		if (amigalimits)
			ch->aspd = tmpspd;
	}

	if (tmpspd == 0)
	{
		ch->m_speed = 0;

		// 8bb: these two are for AdLib
		ch->addherzretrig = 254;
		ch->addherzhi &= 32767;

		return;
	}

	if (tmpspd < song.aspdmin)
	{
		tmpspd = song.aspdmin;
		if (amigalimits)
			ch->aspd = tmpspd;
	}

	// 8bb: Yes, 14317056. Not 14317456 (8363*1712)
	const int32_t hz = 14317056UL / (uint32_t)tmpspd;

	// 8bb: for AdLib
	ch->addherzhi = hz >> 16;
	ch->addherzlo = hz & 0xFFFF;
	// ------------------

	/* 8bb: Use double-precision float for fast delta calculation.
	** Also eliminates the need for a 64-bit division if 'hz' is higher
	** than 65535. This is still bit-accurate to how ST3 calculates the
	** 16.16fp delta.
	*/
	ch->m_speed = (int32_t)(hz * audio.dHz2ST3Delta); // 8bb: 16.16fp

	// 8bb: calculate delta for our mixer (we need this extra step in st3play)
	ch->delta = (uint64_t)((int32_t)ch->m_speed * audio.dST3Delta2MixDelta);
}

void setvol(zchn_t *ch)
{
	ch->achannelused |= 128;
	ch->m_vol = ((unsigned)ch->avol * song.useglobalvol) >> 8;
}

uint16_t stnote2herz(uint8_t note)
{
	if (note == 254)
		return 0; // 0hertz/keyoff

	uint16_t noteVal = notespd[note & 0x0F];

	const uint8_t shiftVal = octavediv[note >> 4];
	if (shiftVal > 0)
		noteVal >>= shiftVal & 0x1F;

	return noteVal;
}

static inline int32_t random32(void) // 8bb: added this (LCG 32-bit random)
{
	randSeed *= 134775813;
	randSeed++;
	return randSeed;
}

void musmixer(int16_t *buffer, int32_t samples) // 8bb: not directly ported
{
	if (samples <= 0)
		return;

	if (!audio.playing || audio.samplesPerTick64 == 0)
	{
		memset(buffer, 0, samples * 2 * sizeof (int16_t));
		return;
	}

	float *fMixL = audio.fMixBufferL;
	float *fMixR = audio.fMixBufferR;

	int32_t samplesLeft = samples;
	while (samplesLeft > 0)
	{
		if (audio.tickSampleCounter64 <= 0)
		{
			dorow(); // 8bb: digread.c (replayer ticker)
			updateregs(); // 8bb: dig.c (GUS & AdLib updating)
			audio.tickSampleCounter64 += audio.samplesPerTick64;
		}

		// 8bb: use fractional precision for "samples per tick" (needed for GUS PIT timing precision)
		const int32_t remainingTick = (audio.tickSampleCounter64 + UINT32_MAX) >> 32; // ceil (rounded upwards)

		int32_t samplesToMix = samplesLeft;
		if (samplesToMix > remainingTick)
			samplesToMix = remainingTick;

		// 8bb: mix PCM voices
		if (audio.soundcardtype == SOUNDCARD_GUS)
			GUS_Mix(fMixL, fMixR, samplesToMix);
		else
			SBPro_Mix(fMixL, fMixR, samplesToMix);

		// 8bb: mix AdLib (OPL2) voices
		if (song.adlibused)
		{
			for (int32_t i = 0; i < samplesToMix; i++)
			{
				const float fSample = OPL2_Output() * (3.0f * (1.0f / 32768.0f));

				fMixL[i] += fSample;
				fMixR[i] += fSample;
			}
		}

		fMixL += samplesToMix;
		fMixR += samplesToMix;

		samplesLeft -= samplesToMix;
		audio.tickSampleCounter64 -= (int64_t)samplesToMix << 32;
	}

	double dOut, dPrng;
	int32_t out32;
	for (int32_t i = 0; i < samples; i++)
	{
		// 8bb: left channel - 1-bit triangular dithering
		dPrng = random32() * (0.5 / INT32_MAX); // 8bb: -0.5 .. 0.5
		dOut = audio.fMixBufferL[i] * audio.fMixNormalize;
		dOut = (dOut + dPrng) - dPrngStateL;
		dPrngStateL = dPrng;
		out32 = (int32_t)dOut;
		CLAMP16(out32);
		*buffer++ = (int16_t)out32;

		// 8bb: right channel - 1-bit triangular dithering
		dPrng = random32() * (0.5 / INT32_MAX); // 8bb: -0.5 .. 0.5
		dOut = audio.fMixBufferR[i] * audio.fMixNormalize;
		dOut = (dOut + dPrng) - dPrngStateR;
		dPrngStateR = dPrng;
		out32 = (int32_t)dOut;
		CLAMP16(out32);
		*buffer++ = (int16_t)out32;

		// 8bb: clear what we read
		audio.fMixBufferL[i] = 0.0f;
		audio.fMixBufferR[i] = 0.0f;
	}
}

void zgotosong(int16_t order, int16_t row)
{
	lockMixer();

	song.startrow = row & 0x3F;
	song.np_ord = order & 0xFF;
	song.breakpat = 1;
	song.musiccount = song.musicmax;

	unlockMixer();
	audio.playing = true;
}

bool zplaysong(int16_t order)
{
	if (!song.moduleLoaded)
		return false;

	song.adlibused = false; // 8bb: set in digadl.c if AdLib channels are handled

	/* 8bb: Set ST3 mixing frequency value.
	** This is only used to calculate a 16.16fp delta value in setspd() (dig.c),
	** then we convert it to work with the actual mixing rate we use in st3play.
	*/
	if (audio.soundcardtype == SOUNDCARD_GUS)
		audio.notemixingspeed = 38587; // 8bb: Yes, ST3 sets this to 38587 regardless of active GUS voices.
	else
		audio.notemixingspeed = song.stereomode ? 22000 : 43478;

	// 8bb: use floating-point for convenience

	audio.dBPM2SamplesPerTick = (audio.outputFreq * 2.5) * (UINT32_MAX+1.0);
	audio.dPIT2SamplesPerTick = (audio.outputFreq / (double)PC_PIT_CLK) * (UINT32_MAX+1.0);
	audio.dHz2ST3Delta = (double)(1 << ST3_FRAC_BITS) / audio.notemixingspeed;

	const double dFreqRatio = (double)audio.notemixingspeed / audio.outputFreq;
	audio.dST3Delta2MixDelta = (dFreqRatio / (double)(1 << ST3_FRAC_BITS)) * (double)(1ULL << MIX_FRAC_BITS);

	OPL2_Init(audio.outputFreq);

	initadlib(); // initialize adlib
	loadheaderparms();
	initmodule();

	if (audio.soundcardtype == SOUNDCARD_GUS)
	{
		/* 8bb: ST3 handles song.header.ultraclick here (amount of GUS voices), but
		** we don't use that. We always use 32 GUS voices, because our GUS emulator
		** doesn't degrade the quality based on number of allocated voices. Also, the
		** ST3 GUS driver is a little buggy, so allocating 32 voices makes it not glitch out.
		*/
		GUS_Reset(audio.outputFreq);
		gcmd_setvoices();
	}

	loadheaderpans();
	if (audio.soundcardtype == SOUNDCARD_GUS)
		gcmd_setstereo(song.stereomode);

	// 8bb: added these two for protection
	song.np_patseg = NULL;
	song.np_patoff = -1;

	song.np_ord = order & 255;
	neworder();

	song.musiccount = 0; // 8bb: added this
	
	resetAudioDither();
	audio.tickSampleCounter64 = 0; // 8bb: zero tick sample counter so that it will instantly initiate a tick

	audio.playing = true;
	return true;
}

static void freeinsmem(int32_t a)
{
	ds_smp *ins = &song.ins[a];
	if (ins->type == 1 && ins->baseptr != NULL)
	{
		free(ins->baseptr);
		ins->baseptr = NULL;
	}
}

// 8bb: custom routines

void resetAudioDither(void)
{
	randSeed = 0x12345000;
	dPrngStateL = 0.0;
	dPrngStateR = 0.0;
}

void setMixingVol(int32_t vol) // 0..256
{
	vol = CLAMP(vol, 0, 256);
	audio.mixingVol = vol; // copy of volume, not used in mixer

	if (audio.soundcardtype == SOUNDCARD_GUS)
		audio.mastermul = 48;

	vol *= audio.mastermul; // 0..32512

	audio.fMixNormalize = 32768.0f * (vol * (1.0f / (256.0f * 128.0f)));
}

void closeMusic(void)
{
	closeMixer();

	if (audio.fMixBufferL != NULL)
	{
		free(audio.fMixBufferL);
		audio.fMixBufferL = NULL;
	}

	if (audio.fMixBufferR != NULL)
	{
		free(audio.fMixBufferR);
		audio.fMixBufferR = NULL;
	}

	// free pattern data
	for (int32_t i = 0; i < MAX_PATTERNS; i++)
	{
		if (song.patp[i] != NULL)
		{
			free(song.patp[i]);
			song.patp[i] = NULL;
		}
	}

	// free sample data
	for (int32_t i = 0; i < MAX_INSTRUMENTS; i++)
		freeinsmem(i);

	memset(&song.header, 0, sizeof (song.header));
	memset(song.order, 255, MAX_ORDERS);
	memset(song.ins, 0, sizeof (song.ins));

	song.adlibused = false;
}

bool initMusic(int32_t audioFrequency, int32_t audioBufferSize)
{
	audio.outputFreq = CLAMP(audioFrequency, 8000, 768000);

	closeMixer();
	closeMusic();
	memset(song._zchn, 0, sizeof (song._zchn));

	setMixingVol(256);

	audio.playing = true;
	audio.samplesPerTick64 = 0;

	audio.fMixBufferL = (float *)calloc(audioBufferSize, sizeof (float));
	audio.fMixBufferR = (float *)calloc(audioBufferSize, sizeof (float));

	if (audio.fMixBufferL == NULL || audio.fMixBufferR == NULL)
	{
		closeMusic();
		return false;
	}

	if (!openMixer(audio.outputFreq, audioBufferSize))
	{
		closeMusic();
		return false;
	}

	return true;
}

void togglePause(void)
{
	audio.playing ^= 1;
}

int32_t activePCMVoices(void)
{
	if (audio.soundcardtype == SOUNDCARD_GUS)
	{
		return activeGUSVoices();
	}
	else
	{
		int32_t activeVoices = 0;

		zchn_t *ch = song._zchn;
		for (int32_t i = 0; i < 16; i++, ch++)
		{
			if (ch->m_base != NULL && ch->m_speed != 0 && ch->m_pos != 0xFFFFFFFF && ch->m_vol > 0)
				activeVoices++;
		}

		return activeVoices;
	}
}

int32_t activeAdLibVoices(void)
{
	int32_t activeVoices = 0;

	zchn_t *ch = &song._zchn[16];
	for (int32_t i = 0; i < 9; i++, ch++)
	{
		if (ch->m_speed != 0 && ch->m_vol > 0 && ch->lastadlins > 0)
			activeVoices++;
	}

	return activeVoices;
}
