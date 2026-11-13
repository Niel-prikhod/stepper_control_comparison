#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ENCODER_I2C_ADDRESS
#define ENCODER_I2C_ADDRESS 0x36u
#endif

#define ENCODER_STATUS_REG 0x0Bu
#define ENCODER_ANGLE_REG 0x0Cu
#define ENCODER_COUNTS_PER_REV 4096u

typedef struct {
  uint16_t raw;
  uint32_t millidegrees;
  bool magnet_detected;
  bool magnet_too_weak;
  bool magnet_too_strong;
  bool valid;
} encoder_sample_t;

void encoder_init(void);
void encoder_set_address(uint8_t addr);

bool encoder_start_read(void);
bool encoder_poll(encoder_sample_t *sample);
bool encoder_read(encoder_sample_t *sample, uint32_t timeout_ms);

bool encoder_busy(void);
uint32_t encoder_error_count(void);

#ifdef __cplusplus
}
#endif
