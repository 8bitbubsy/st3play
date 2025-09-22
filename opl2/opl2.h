#pragma once

#include <stdint.h>

void OPL2_Init(int32_t audioOutputFrequency);
void OPL2_WritePort(uint16_t reg_num, uint8_t val);
float OPL2_Output(void);
