#pragma once

#include <stdint.h>

/** Built-in full-step coil sequence used when no sequence is supplied. */
extern const uint8_t stepper_default_sequence[4];

/**
 * @brief Advance a sequence index with wrap-around.
 *
 * Why: Keeps the motor driver's coil index valid in both directions and is
 * exposed to the private header so it can be unit-tested.
 *
 * @param index Current sequence index.
 * @param len Number of entries in the sequence; must be non-zero.
 * @param dir Positive advances forward, negative advances backward.
 * @return Wrapped sequence index.
 */
uint8_t stepper_next_index(uint8_t index, uint8_t len, int dir);
