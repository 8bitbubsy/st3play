#pragma once

#define SINC_TAPS 16
#define SINC_TAPS_BITS 4 /* log2(SINC_TAPS) */
#define SINC_OVERSAMPLING 256

extern const float fSincLUT[(SINC_OVERSAMPLING+1) * SINC_TAPS];
