#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_COIL_A (1u << 0)
#define PLATFORM_COIL_B (1u << 1)
#define PLATFORM_COIL_C (1u << 2)
#define PLATFORM_COIL_D (1u << 3)

void platform_init(void);
void platform_motor_write_coils(uint8_t mask);
void platform_motor_enable(bool enabled);
void platform_delay_us(uint32_t us);
void platform_led_toggle(void);

#ifdef __cplusplus
}
#endif
