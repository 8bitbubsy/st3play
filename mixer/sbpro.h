#pragma once

#include <stdint.h>
#include <stdbool.h>

void SBPro_Init(int32_t audioOutputFrequency, uint8_t timeConstant, bool stereo);
bool SBPro_StereoMode(void);
double SBPro_GetOutputRate(void);
void SBPro_RenderSamples(float *fMixBufL, float *fMixBufR, int32_t numSamples);
