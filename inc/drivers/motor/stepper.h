#pragma once

#include <stdint.h>

/** Runtime configuration for the stepper motor driver. */
typedef struct {
  /** Motor steps per full revolution. */
  uint32_t steps_per_rev;
  /** Default delay between steps for blocking moves, in microseconds. */
  uint32_t step_interval_us;
  /** Coil bitmask sequence; null selects the built-in full-step sequence. */
  const uint8_t *coil_sequence;
  /** Number of entries in @ref coil_sequence. */
  uint8_t sequence_len;
} stepper_config_t;

/**
 * @brief Initialize the stepper driver.
 *
 * Why: Stores the motor configuration, fills in safe defaults for missing
 * fields, and leaves the coils de-energized before the first move.
 *
 * @param cfg Configuration to copy; null keeps the current/default values.
 * @return None.
 */
void stepper_init(const stepper_config_t *cfg);

/**
 * @brief Perform a blocking move by a signed number of steps.
 *
 * Why: Provides a simple way to move the motor when no timer-driven
 * measurement is active.
 *
 * @param steps Number of steps; positive and negative values select direction.
 * @return None.
 */
void stepper_move_steps(int32_t steps);

/**
 * @brief Enable or disable the motor driver output.
 *
 * Why: Allows callers to control motor energization independently of coil
 * writes.
 *
 * @param enabled Non-zero enables the driver; zero disables it.
 * @return None.
 */
void stepper_set_enabled(int enabled);

/**
 * @brief Advance one step in the requested direction.
 *
 * Why: Updates the coil sequence index and writes the next coil pattern; this
 * is the primitive used by timer-driven measurements.
 *
 * @param dir Positive advances forward, negative advances backward.
 * @return Coil bitmask written to the motor driver.
 */
uint8_t stepper_advance(int dir);

/**
 * @brief Turn off all coils and disable the motor driver.
 *
 * Why: Removes holding torque and reduces power after a move or failed
 * measurement.
 *
 * @return None.
 */
void stepper_deenergize(void);
