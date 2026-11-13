#include <drivers/motor/stepper.h>
#include <platform/stepper.h>

#include "stepper_internal.h"

const uint8_t stepper_default_sequence[4] = {
  PLATFORM_COIL_A | PLATFORM_COIL_B,
  PLATFORM_COIL_B | PLATFORM_COIL_C,
  PLATFORM_COIL_C | PLATFORM_COIL_D,
  PLATFORM_COIL_D | PLATFORM_COIL_A,
};

static stepper_config_t s_cfg;
static uint8_t s_index;

uint8_t stepper_next_index(uint8_t index, uint8_t len, int dir) {
  int next = (int)index + dir;
  if (next < 0) {
    next += len;
  } else if (next >= len) {
    next -= len;
  }
  return (uint8_t)next;
}

void stepper_init(const stepper_config_t *cfg) {
  if (cfg) {
    s_cfg = *cfg;
  }
  if (!s_cfg.coil_sequence || s_cfg.sequence_len == 0) {
    s_cfg.coil_sequence = stepper_default_sequence;
    s_cfg.sequence_len = 4;
  }
  if (s_cfg.steps_per_rev == 0) {
    s_cfg.steps_per_rev = 200;
  }
  if (s_cfg.step_interval_us == 0) {
    s_cfg.step_interval_us = 1000;
  }
  s_index = 0;
  platform_motor_write_coils(0);
  platform_motor_enable(false);
}

void stepper_move_steps(int32_t steps) {
  if (steps == 0) {
    return;
  }
  int dir = (steps > 0) ? 1 : -1;
  uint32_t n = (steps > 0) ? (uint32_t)steps : (uint32_t)(-steps);
  platform_motor_enable(true);
  for (uint32_t i = 0; i < n; i++) {
    s_index = stepper_next_index(s_index, s_cfg.sequence_len, dir);
    platform_motor_write_coils(s_cfg.coil_sequence[s_index]);
    platform_delay_us(s_cfg.step_interval_us);
  }
  platform_motor_write_coils(0);
  platform_motor_enable(false);
}

void stepper_set_enabled(int enabled) {
  platform_motor_enable(enabled != 0);
}
