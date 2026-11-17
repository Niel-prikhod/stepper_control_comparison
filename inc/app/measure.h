#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Lowest accepted step frequency, in Hz. */
#define APP_MIN_STEP_HZ 20u
/** Highest accepted step frequency, in Hz. */
#define APP_MAX_STEP_HZ 5000u
/** Longest accepted mode 1 duration, in milliseconds. */
#define APP_MAX_DURATION_MS 20000u
/** Largest accepted mode 2 sample count. */
#define APP_MAX_SAMPLES 100000u
/** Largest accepted mode 4 trial count. */
#define APP_MAX_TRIALS 10000u
/** Largest accepted mode 5 frequency-point count. */
#define APP_MAX_MODE5_POINTS 100u
/** Motor steps per revolution used by the measurement application. */
#define APP_STEPS_PER_REV 2048u

/**
 * @brief Initialize the measurement subsystem.
 *
 * Why: Configures the stepper driver with the project defaults and leaves the
 * motor de-energized so every measurement starts from a safe state.
 *
 * @return None.
 */
void app_measure_init(void);

/**
 * @brief Measure actual step frequency over a fixed duration.
 *
 * Why: Verifies that the commanded rate matches the TIM1-generated step rate,
 * catching clock-tree, prescaler, and auto-reload errors.
 *
 * @param target_hz Requested step frequency in Hz.
 * @param duration_ms Requested run duration in milliseconds.
 * @return true when at least two steps were captured and the result was
 *         reported; false on timer start, stall, or timing failure.
 */
bool app_measure_mode1(uint32_t target_hz, uint32_t duration_ms);

/**
 * @brief Measure step-interval jitter at a fixed frequency.
 *
 * Why: Detects missed interrupts, unstable scheduling, and long ISR latency
 * that an average-frequency measurement can hide.
 *
 * @param target_hz Requested step frequency in Hz.
 * @param samples Number of timer intervals to collect after the first step.
 * @return true when the requested number of samples was collected; false on
 *         timer start, stall, or timeout failure.
 */
bool app_measure_mode2(uint32_t target_hz, uint32_t samples);

/**
 * @brief Measure timer ISR load in idle, constant, and ramp conditions.
 *
 * Why: Quantifies the CPU cost of the step timer interrupt and shows how motor
 * stepping and timer-period changes affect that cost.
 *
 * @param target_hz Requested step frequency in Hz for the constant condition.
 * @param samples Number of timer interrupts to collect per condition.
 * @return true when all three conditions completed; false if any condition
 *         stalls or times out.
 */
bool app_measure_mode3(uint32_t target_hz, uint32_t samples);

/**
 * @brief Measure command-to-first-step latency over several trials.
 *
 * Why: Estimates control-loop and UART handling overhead when a command starts
 * motor motion.
 *
 * @param trials Number of start/stop latency trials to run.
 * @param cmd_len Length of the received command line, in bytes.
 * @param initial_rx_cycles DWT timestamp captured when the command line was
 *        received; used for the first-trial UART-to-step measurement.
 * @return true when every trial produced a first step; false on timer start,
 *         stall, or timeout failure.
 */
bool app_measure_mode4(uint32_t trials, uint32_t cmd_len,
                       uint32_t initial_rx_cycles);

/**
 * @brief Sweep step frequency and verify rotation with encoder feedback.
 *
 * Why: Finds the highest commanded rate that still produces the expected
 * number of motor steps, revealing torque loss, resonance, or driver limits.
 *
 * @param start_hz First and ramp-start frequency in Hz.
 * @param step_hz Frequency increment between sweep points, in Hz.
 * @param max_hz Last frequency in the sweep, in Hz.
 * @return true when the sweep completed; false on encoder read failure or a
 *         motor stall.
 */
bool app_measure_mode5(uint32_t start_hz, uint32_t step_hz, uint32_t max_hz);

#ifdef __cplusplus
}
#endif
