#pragma once

#include <stdint.h>
#include <stdbool.h>

void gcmd_inittables(void);
void gcmd_setvoices(void);
void gcmd_setstereo(bool stereo);
void gcmd_update(zchn_t *ch);
