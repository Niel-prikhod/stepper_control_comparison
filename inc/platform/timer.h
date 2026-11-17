#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Step timer callback invoked from the timer interrupt.
 *
 * Why: Lets measurement modes perform one step and decide whether the timer
 * should continue without adding mode-specific logic to the platform layer.
 *
 * @param timestamp_cycles DWT cycle count captured at ISR entry.
 * @param period_cycles Cycles elapsed since the previous callback, or since
 *        timer start for the first callback.
 * @return true to keep the timer running; false to stop after this callback.
 */
typedef bool (*platform_step_callback_t)(uint32_t timestamp_cycles,
                                         uint32_t period_cycles);

/** Snapshot of step-timer interrupt statistics. */
typedef struct {
  /** Number of timer interrupts serviced. */
  uint32_t isr_count;
  /** Sum of measured ISR execution cycles. */
  uint64_t isr_cycles;
  /** Sum of measured timer periods in cycles. */
  uint64_t period_cycles;
  /** Longest observed ISR execution time in cycles. */
  uint32_t peak_isr_cycles;
  /** Most recent measured timer period in cycles. */
  uint32_t last_period_cycles;
} platform_step_stats_t;

/**
 * @brief Initialize the step timer peripheral.
 *
 * Why: Configures TIM1 for one-microsecond ticks and clears callback/stat
 * state before the first measurement.
 *
 * @return None.
 */
void platform_step_timer_init(void);

/**
 * @brief Start the step timer with a callback and period.
 *
 * Why: Begins timer-driven stepping with a mode-specific callback and a
 * validated period.
 *
 * @param period_us Timer period in microseconds; clamped to the supported
 *        range.
 * @param callback Function called from each timer interrupt; must not be null.
 * @return true if the timer started; false if it is already running or HAL
 *         start failed.
 */
bool platform_step_timer_start(uint32_t period_us,
                               platform_step_callback_t callback);

bool platform_step_timer_start_free_running(uint32_t period_us,
                                            platform_step_callback_t callback);

/**
 * @brief Stop the step timer and clear the active callback.
 *
 * Why: Ensures a measurement or failed run cannot continue generating
 * interrupts.
 *
 * @return None.
 */
void platform_step_timer_stop(void);

/**
 * @brief Change the step timer period while it is running.
 *
 * Why: Supports ramped measurements that adjust frequency from the timer
 * callback.
 *
 * @param period_us New timer period in microseconds; clamped to the supported
 *        range.
 * @return true when the new period was applied; false when the platform has no
 *         timer support.
 */
bool platform_step_timer_set_period_us(uint32_t period_us);

/**
 * @brief Report whether the step timer is currently running.
 *
 * Why: Lets callers avoid double starts and lets self-tests observe the
 * callback-driven stop condition.
 *
 * @return true while the timer is running; otherwise false.
 */
bool platform_step_timer_is_running(void);

/**
 * @brief Read the timer prescaler register.
 *
 * Why: Mode 0 reports the actual PSC and ARR values so host diagnostics can
 * verify the 1 MHz timer tick independently of the requested frequency.
 *
 * @return Raw PSC register value, or zero when the platform has no timer.
 */
uint32_t platform_step_timer_get_prescaler(void);

/**
 * @brief Read the timer auto-reload register.
 *
 * Why: Mode 0 reports the actual ARR value used for each frequency.
 *
 * @return Raw ARR register value, or zero when the platform has no timer.
 */
uint32_t platform_step_timer_get_autoreload(void);

/**
 * @brief Clear accumulated step-timer statistics.
 *
 * Why: Starts each measurement condition from a known statistics baseline.
 *
 * @return None.
 */
void platform_step_timer_reset_stats(void);

/**
 * @brief Copy a consistent snapshot of step-timer statistics.
 *
 * Why: Gives measurement code an interrupt-safe view of ISR count, load, and
 * peak timing.
 *
 * @param stats Destination for the snapshot; null is ignored.
 * @return None.
 */
void platform_step_timer_get_stats(platform_step_stats_t *stats);

/**
 * @brief Verify that the step timer generates callbacks.
 *
 * Why: Catches timer configuration or interrupt-routing failures before
 * measurements depend on them.
 *
 * @return true if the expected callback count was observed; otherwise false.
 */
bool platform_step_timer_selftest(void);

#ifdef __cplusplus
}
#endif
