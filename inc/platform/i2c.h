#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the I2C peripheral and DMA resources.
 *
 * Why: Prepares the bus, DMA channels, and interrupt priorities used by all
 * encoder reads.
 *
 * @return None.
 */
void platform_i2c_init(void);

/**
 * @brief Start a non-blocking register read over I2C DMA.
 *
 * Why: Lets the application overlap an encoder transfer with motor timing and
 * avoids blocking the main loop.
 *
 * @param dev_addr 7-bit I2C device address.
 * @param reg Register address to read from.
 * @param data Destination buffer for the returned bytes.
 * @param len Number of bytes to read; must be non-zero.
 * @return true if the DMA transfer started; false for invalid arguments, a
 *         busy bus, or a HAL start error.
 */
bool platform_i2c_read_reg_start(uint8_t dev_addr, uint8_t reg,
                                 uint8_t *data, uint16_t len);

/**
 * @brief Wait for the current non-blocking I2C read to finish.
 *
 * Why: Gives blocking callers a bounded wait while preserving the non-blocking
 * start API for other users.
 *
 * @param timeout_ms Maximum wait time in milliseconds.
 * @return true if the transfer completed successfully; false on timeout or bus
 *         error.
 */
bool platform_i2c_read_reg_wait(uint32_t timeout_ms);

/**
 * @brief Perform a register read using the start/wait sequence.
 *
 * Why: Provides the common blocking read without duplicating start and wait
 * logic at every call site.
 *
 * @param dev_addr 7-bit I2C device address.
 * @param reg Register address to read from.
 * @param data Destination buffer for the returned bytes.
 * @param len Number of bytes to read.
 * @param timeout_ms Maximum wait time in milliseconds.
 * @return true on success; false on start, timeout, or bus failure.
 */
bool platform_i2c_read_reg(uint8_t dev_addr, uint8_t reg,
                           uint8_t *data, uint16_t len, uint32_t timeout_ms);

/**
 * @brief Perform a fully blocking register read using HAL polling.
 *
 * Why: Provides a simple read path for startup diagnostics and self-tests
 * where DMA concurrency is not needed.
 *
 * @param dev_addr 7-bit I2C device address.
 * @param reg Register address to read from.
 * @param data Destination buffer for the returned bytes.
 * @param len Number of bytes to read.
 * @param timeout_ms HAL timeout in milliseconds.
 * @return true on success; false if the bus is busy or HAL reports an error.
 */
bool platform_i2c_read_reg_blocking(uint8_t dev_addr, uint8_t reg,
                                    uint8_t *data, uint16_t len,
                                    uint32_t timeout_ms);

/**
 * @brief Check whether a device acknowledges its I2C address.
 *
 * Why: Distinguishes a missing or misaddressed encoder from a data-transfer
 * failure during startup self-test.
 *
 * @param dev_addr 7-bit I2C device address.
 * @param trials Number of HAL probe attempts.
 * @param timeout_ms Timeout per attempt in milliseconds.
 * @return true if the device acknowledged; otherwise false.
 */
bool platform_i2c_probe(uint8_t dev_addr, uint32_t trials, uint32_t timeout_ms);

/**
 * @brief Report whether an I2C transfer is in progress.
 *
 * Why: Prevents overlapping transfers and lets callers poll completion.
 *
 * @return true while a transfer is active; otherwise false.
 */
bool platform_i2c_busy(void);

/**
 * @brief Report whether the last transfer completed successfully.
 *
 * Why: Separates transfer completion from success so pollers can detect bus
 * errors.
 *
 * @return true when the last transfer completed without error; otherwise false.
 */
bool platform_i2c_ok(void);

/**
 * @brief Return the accumulated I2C error count.
 *
 * Why: Supports long-run diagnostics for intermittent wiring or bus faults.
 *
 * @return Number of errors since initialization.
 */
uint32_t platform_i2c_error_count(void);

/**
 * @brief Return the HAL status captured for the last transfer.
 *
 * Why: Preserves the exact failure cause after the HAL state has moved on.
 *
 * @return Last HAL status, or a platform-specific sentinel on timeout.
 */
uint32_t platform_i2c_last_status(void);

/**
 * @brief Return the HAL error code captured for the last transfer.
 *
 * Why: Helps distinguish arbitration, acknowledge, and bus errors during
 * debugging.
 *
 * @return Last HAL error code, or zero when none was captured.
 */
uint32_t platform_i2c_last_error(void);

/**
 * @brief Return the HAL state captured for the last transfer.
 *
 * Why: Shows whether the peripheral was busy, ready, or in an error state when
 * the failure was recorded.
 *
 * @return Last HAL I2C state, or zero when none was captured.
 */
uint32_t platform_i2c_last_state(void);

#ifdef __cplusplus
}
#endif
