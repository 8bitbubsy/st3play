/***********************************************************************
 **
 **  Gravis Ultrasound driver
 **
 ** 8bitbubsy: Modified to always run in 32-voice mode (with no quality
 **            reduction).
 **
 ***********************************************************************/
 
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "dig.h"
#include "mixer/gus.h"

#define RAMPRATE 15

static int8_t voiceused[MAX_GUS_VOICES];
static uint8_t stchannelpan[ACHANNELS]; // st channels pan settings
static uint8_t channeltrig[MAX_GUS_VOICES];
static uint8_t somevoice;
static uint8_t voicetry; // which voice to try next

// 8bb: ST3 volume -> GUS volume table (from ST3.21)
static const uint16_t gusvoltable[64+1] =
{
	 4096,36848,40944,43008,45040,46080,47104,48128,49136,49664,50176,
	50688,51200,51712,52224,52736,53232,53504,53760,54016,54272,54528,
	54784,55040,55296,55552,55808,56064,56320,56576,56832,57088,57328,
	57472,57600,57728,57856,57984,58112,58240,58368,58496,58624,58752,
	58880,59008,59136,59264,59392,59520,59648,59776,59904,60032,60160,
	60288,60416,60544,60672,60800,60928,61056,61184,61312,

	256 // 8bb: non-audible
};

static void shutupgus(void)
{
	for (uint8_t i = 0; i < MAX_GUS_VOICES; i++)
	{
		GUS_VoiceSelect(i);
		GUS_SetVoiceCtrl(0b00000011);
		GUS_SetCurrVolume(0);
		GUS_SetBalance(7);
		GUS_SetVolumeCtrl(0b00000011);

		voiceused[i] = 0;
	}
}

void gcmd_inittables(void)
{
	shutupgus();

	for (int32_t i = 0; i < MAX_GUS_VOICES; i++)
	{
		voiceused[i] = 0;
		channeltrig[i] = 255;
	}

	voicetry = 0;
	somevoice = 0; // 8bb: added clearing of this
}

void gcmd_setvoices(void)
{
	shutupgus();
	GUS_StopVoices();
}

void gcmd_setstereo(bool stereo) // sets for 16 first dma channels
{
	for (uint8_t i = 0; i < 16; i++)
	{
		uint8_t pan;
		if (stereo)
		{
			if (i < 8)
				pan = 0x3;
			else
				pan = 0xC;
		}
		else
		{
			pan = 7;
		}

		stchannelpan[i] = pan;

		GUS_VoiceSelect(i);
		GUS_SetBalance(pan);
	}
}

static void setvolslide(zchn_t *ch, uint8_t currVol, uint8_t targetVol)
{
	if (currVol == targetVol)
		return;

	ch->m_oldvol = targetVol;

	const uint16_t currLogVol = gusvoltable[currVol];
	const uint16_t targetLogVol = gusvoltable[targetVol];

	GUS_SetVolumeRate(RAMPRATE);
	GUS_SetCurrVolume(currLogVol);

	if (currLogVol < targetLogVol) // slide up
	{
		GUS_SetStartVolume(currLogVol >> 8);
		GUS_SetEndVolume(targetLogVol >> 8);
		GUS_SetVolumeCtrl(0); // increasing ramp
	}
	else // slide down
	{
		GUS_SetStartVolume(targetLogVol >> 8);
		GUS_SetEndVolume(currLogVol >> 8);
		GUS_SetVolumeCtrl(64); // decreasing ramp
	}
}

static void freevoices(void) // finds free voices
{
	// try first to find voices with almost zero volume or stopped
	bool voicesfreed = false;
	for (uint8_t i = 0; i < MAX_GUS_VOICES; i++)
	{
		if (voiceused[i] > 0)
			continue; // playing, don't do

		GUS_VoiceSelect(i);
		if (GUS_GetVoiceCtrl() & 1) // voice stopped
		{
			// Shut down
			voiceused[i] = 0;
			voicesfreed = true;
		}
	}

	if (voicesfreed)
		return; // voices freed! done.

	// find voice with lowest notused count
	// try finding voices that are sliding down
	int8_t notusedcount = -103;
	int8_t voice = -1;
	for (int8_t i = 0; i < MAX_GUS_VOICES; i++)
	{
		if (voiceused[i] > 0)
			continue; // used

		if (voiceused[i] <= notusedcount)
			continue; // smaller than best found so far

		voice = i;
		notusedcount = voiceused[i];
	}

	if (voice != -1)
	{
		voiceused[voice] = 0;
		return;
	}

	// still not found, force some voice to zero
	somevoice++;
	if (somevoice >= MAX_GUS_VOICES)
		somevoice = 0;

	voiceused[somevoice] = 0; // force one free
}

void gcmd_update(zchn_t *ch)
{
	if (ch == NULL) // 8bb: handle GUS triggers
	{
		for (uint8_t i = 0; i < MAX_GUS_VOICES; i++)
		{
			if (voiceused[i] < 0)
				voiceused[i]++;

			if (channeltrig[i] != 255)
			{
				GUS_VoiceSelect(i);
				GUS_SetVoiceCtrl(channeltrig[i]);
				channeltrig[i] = 255;
			}
		}

		return;
	}

	if (ch->aguschannel >= 0)
		GUS_VoiceSelect(ch->aguschannel);

	if (ch->aguschannel == -1 || ch->m_oldpos != ch->m_pos)
	{
		if (ch->aguschannel >= 0)
		{
			// slide old channel to zero
			setvolslide(ch, ch->m_oldvol, 64); // 8bb: 64 is quieter than 0 in gusvoltable

			// flip channel
			voiceused[ch->aguschannel] = -4; // shutting down
		}

		uint8_t voice = 0; // 8bb: initialize to 0 to prevent compiler warning

		bool channelFound = false;
		while (!channelFound)
		{
			ch->m_oldpos = ch->m_pos;

			voice = voicetry;
			for (int32_t i = 0; i < MAX_GUS_VOICES; i++)
			{
				voice++;
				if (voice >= MAX_GUS_VOICES)
					voice = 0;

				if (voiceused[voice] == 0)
				{
					channelFound = true;
					break;
				}
			}

			if (!channelFound)
				freevoices(); // changes setvoice
		}

		voicetry = voice;

		if ((uint16_t)ch->m_end == 0) // 8bb: test lower word here
		{
			voiceused[voicetry] = 0;
			ch->aguschannel = -1;
			return;
		}

		// 8bb: assign channel
		voiceused[voicetry] = 1;
		ch->aguschannel = voicetry;
		GUS_VoiceSelect(voicetry);

		if (ch->m_end == 0)
		{
			// channel quiet, don't mark it used
			voiceused[ch->aguschannel] = -1;
			ch->aguschannel = -1;
			ch->m_oldvol = ch->m_vol = 0;
		}

		// clear volume & stop voice
		GUS_SetVoiceCtrl(0b00000010); // stop
		GUS_SetCurrVolume(0);

		// set pan
		if (song.stereomode && ch->apanpos >= 0xF0)
			GUS_SetBalance(ch->apanpos & 0x0F);
		else
			GUS_SetBalance(stchannelpan[ch->channelnum]);

		GUS_SetVoiceCtrl(0b00000010); // stop (8bb: again?)

		GUS_SetEndAddress(ch->m_base + ch->m_end); // loop end

		// loop start
		if ((uint16_t)ch->m_loop == 65535) // 8bb: no loop
			GUS_SetStartAddress(ch->m_base);
		else
			GUS_SetStartAddress(ch->m_base + ch->m_loop);

		GUS_SetCurrAddress(ch->m_base + ch->m_pos); // begin/curpos

		// hz
		GUS_SetFrequency(ch->delta);

		if (ch->m_end == 0)
			channeltrig[ch->aguschannel] = 0b00000010; // stop
		else if ((uint16_t)ch->m_loop == 65535)
			channeltrig[ch->aguschannel] = 0b00000000; // noloop
		else
			channeltrig[ch->aguschannel] = 0b00001000; // loop

		// finally slide volumeon (8bb: what's this 1->2 volume logic..?)
		if (ch->m_vol == 1)
			setvolslide(ch, 2, ch->m_vol);
		else
			setvolslide(ch, 1, ch->m_vol);
	}
	else
	{
		// update m_pos & m_oldpos

		if (ch->m_speed != 0)
		{
			uint32_t pos = (uint32_t)(GUS_GetCurrAddress() - ch->m_base);
			if (pos >= 65536)
				pos = 0;

			ch->m_oldpos = ch->m_pos = pos;
		}

		// hz
		GUS_SetFrequency(ch->delta);

		setvolslide(ch, ch->m_oldvol, ch->m_vol);
	}
}
