#pragma once

#include <stdint.h>

typedef struct {
  uint32_t steps_per_rev;
  uint32_t step_interval_us;
  const uint8_t *coil_sequence;
  uint8_t sequence_len;
} stepper_config_t;

void stepper_init(const stepper_config_t *cfg);
void stepper_move_steps(int32_t steps);
void stepper_set_enabled(int enabled);
