#pragma once

#include <stdint.h>

extern const uint8_t stepper_default_sequence[4];

uint8_t stepper_next_index(uint8_t index, uint8_t len, int dir);
