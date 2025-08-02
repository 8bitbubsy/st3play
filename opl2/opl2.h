#pragma once

#include <stdint.h>

void OPL2_Init(double dSampleRate);
void OPL2_WritePort(uint16_t reg_num, uint8_t val);
float OPL2_Output(void);
