#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enable and configure the DWT cycle counter.
 *
 * Why: Provides the free-running high-resolution time base used by all
 * measurement modes.
 *
 * @return true if the cycle counter was enabled; false if DWT is unavailable.
 */
bool platform_dwt_init(void);

/**
 * @brief Report whether the DWT cycle counter is usable.
 *
 * Why: Lets callers choose a fallback path when the core does not implement or
 * enable DWT.
 *
 * @return true when the cycle counter is enabled; otherwise false.
 */
bool platform_dwt_available(void);

/**
 * @brief Verify that the DWT cycle counter is advancing.
 *
 * Why: Catches a configured-but-stuck counter before measurements produce
 * meaningless zero-duration results.
 *
 * @return true if the counter advanced during the test; otherwise false.
 */
bool platform_dwt_selftest(void);

/**
 * @brief Read the current DWT cycle count.
 *
 * Why: Supplies timestamps for interval, latency, and duration calculations.
 *
 * @return Current 32-bit cycle count; zero when DWT is unavailable.
 */
uint32_t platform_dwt_read(void);

/**
 * @brief Return the configured DWT clock frequency.
 *
 * Why: Converts cycle counts to microseconds and validates measurement timing.
 *
 * @return DWT clock frequency in Hz.
 */
uint32_t platform_dwt_hz(void);

/**
 * @brief Convert a 32-bit cycle count to microseconds.
 *
 * Why: Presents human-readable timing while protecting callers from overflow.
 *
 * @param cycles Cycle count to convert.
 * @return Equivalent microseconds, saturated at UINT32_MAX.
 */
uint32_t platform_dwt_cycles_to_us(uint32_t cycles);

/**
 * @brief Convert a 64-bit cycle count to microseconds.
 *
 * Why: Avoids losing precision or overflowing when accumulating long ISR and
 * period totals.
 *
 * @param cycles Cycle count to convert.
 * @return Equivalent microseconds.
 */
uint64_t platform_dwt_cycles_to_us64(uint64_t cycles);

#ifdef __cplusplus
}
#endif
