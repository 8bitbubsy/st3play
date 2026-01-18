#pragma once

#include <stdint.h>
#include <stdbool.h>

#define SINC_PHASES 8192
#define SINC_WIDTH 16
#define SINC_WIDTH_BITS 4 /* log2(SINC_WIDTH) */

void makeSincKernel(float *fOut, float kaiserBeta);
