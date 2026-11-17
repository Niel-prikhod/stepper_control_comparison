#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Coil A bit in the motor coil mask. */
#define PLATFORM_COIL_A (1u << 0)
/** Coil B bit in the motor coil mask. */
#define PLATFORM_COIL_B (1u << 1)
/** Coil C bit in the motor coil mask. */
#define PLATFORM_COIL_C (1u << 2)
/** Coil D bit in the motor coil mask. */
#define PLATFORM_COIL_D (1u << 3)

/**
 * @brief Initialize board-level platform resources.
 *
 * Why: Brings up clocks, GPIO, LED, DWT, I2C, logging UART, and the step timer
 * before the application starts.
 *
 * @return None.
 */
void platform_init(void);

/**
 * @brief Write a four-coil output pattern.
 *
 * Why: Abstract board-specific GPIO pins so the motor driver only deals with
 * coil bitmasks.
 *
 * @param mask Bitmask of PLATFORM_COIL_* values to energize.
 * @return None.
 */
void platform_motor_write_coils(uint8_t mask);

/**
 * @brief Enable or disable the motor driver output.
 *
 * Why: Provides a board hook for driver-enable pins or power control without
 * changing the motor driver API.
 *
 * @param enabled Non-zero enables the output; zero disables it.
 * @return None.
 */
void platform_motor_enable(bool enabled);

/**
 * @brief Delay for a requested number of microseconds.
 *
 * Why: Provides short timing gaps for blocking moves and settling without
 * requiring every caller to know the clock tree.
 *
 * @param us Delay length in microseconds.
 * @return None.
 */
void platform_delay_us(uint32_t us);

/**
 * @brief Set the board status LED.
 *
 * Why: Gives the status service a hardware-independent way to show idle,
 * active, and fault states.
 *
 * @param on Non-zero turns the LED on; zero turns it off.
 * @return None.
 */
void platform_led_set(bool on);

/**
 * @brief Toggle the board status LED.
 *
 * Why: Supports simple blink patterns when only a toggle operation is needed.
 *
 * @return None.
 */
void platform_led_toggle(void);

#ifdef __cplusplus
}
#endif
