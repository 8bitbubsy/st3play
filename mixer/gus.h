#pragma once

#include <stdint.h>
#include <stdbool.h>

#define MAX_GUS_VOICES 48 /* why? Well, to make sure all 16 voices can safely ramp at once in st3play! */

// these are NOT thread-safe and must only be called from the thread that calls GUS_Mix()!
void GUS_VoiceSelect(uint8_t voiceNum);
void GUS_SetFrequency(uint64_t freq); // 32.32fp (upgraded from a lousy 6.9fp)
void GUS_SetCurrVolume(uint16_t volume); // logarithmic
void GUS_SetStartVolume(uint8_t volume);
void GUS_SetEndVolume(uint8_t volume);
void GUS_SetVolumeRate(uint8_t rate);
void GUS_SetBalance(uint8_t balance); // 0..15
const int8_t *GUS_GetCurrAddress(void);
void GUS_SetCurrAddress(const int8_t *address);
void GUS_SetStartAddress(const int8_t *address); // loop start
void GUS_SetEndAddress(const int8_t *address); // loop end
void GUS_SetVolumeCtrl(uint8_t flags);
void GUS_SetVoiceCtrl(uint8_t flags);
uint8_t GUS_GetVoiceCtrl(void);
// --------------------------------------------

void GUS_StopVoices(void);
void GUS_Reset(int32_t audioOutputFrequency);
void GUS_Mix(float *fMixBufL, float *fMixBufR, int32_t numSamples);

int32_t activeGUSVoices(void);
