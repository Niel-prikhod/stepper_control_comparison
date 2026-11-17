#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ENCODER_I2C_ADDRESS
/** Default 7-bit I2C address for an AS5600-compatible encoder. */
#define ENCODER_I2C_ADDRESS 0x36u
#endif

/** Register containing magnet status flags. */
#define ENCODER_STATUS_REG 0x0Bu
/** First register of the 12-bit raw angle value. */
#define ENCODER_ANGLE_REG 0x0Cu
/** Encoder counts per full mechanical revolution. */
#define ENCODER_COUNTS_PER_REV 4096u

/** One decoded encoder sample. */
typedef struct {
  /** Raw 12-bit angle, 0..4095. */
  uint16_t raw;
  /** Angle converted to millidegrees. */
  uint32_t millidegrees;
  /** Magnet-detect flag from the status register. */
  bool magnet_detected;
  /** Magnet-too-weak flag from the status register. */
  bool magnet_too_weak;
  /** Magnet-too-strong flag from the status register. */
  bool magnet_too_strong;
  /** True when the magnet is detected and its field strength is valid. */
  bool valid;
} encoder_sample_t;

/**
 * @brief Initialize the encoder driver state.
 *
 * Why: Clears pending transfer and error state before the first read.
 *
 * @return None.
 */
void encoder_init(void);

/**
 * @brief Override the encoder I2C address.
 *
 * Why: Supports AS5600L and other compatible devices that use a different
 * address without rebuilding the driver default.
 *
 * @param addr New 7-bit I2C address.
 * @return None.
 */
void encoder_set_address(uint8_t addr);

/**
 * @brief Start a non-blocking encoder status-and-angle read.
 *
 * Why: Allows the caller to overlap the I2C transfer with other work.
 *
 * @return true if the transfer started; false if a read is already pending or
 *         the I2C start failed.
 */
bool encoder_start_read(void);

/**
 * @brief Finish a previously started read and decode the sample.
 *
 * Why: Converts raw status/angle bytes into a validated sample after the
 * non-blocking transfer completes.
 *
 * @param sample Destination for the decoded sample; null is rejected.
 * @return true if the transfer completed successfully and the sample was
 *         decoded; false if no transfer is pending, it is still busy, or it
 *         failed.
 */
bool encoder_poll(encoder_sample_t *sample);

/**
 * @brief Perform a blocking encoder read.
 *
 * Why: Simplifies measurement code that needs a fresh angle without managing
 * the start/poll sequence.
 *
 * @param sample Destination for the decoded sample.
 * @param timeout_ms Maximum time to wait for the I2C transfer.
 * @return true on a valid transfer and decode; false on start, timeout, bus, or
 *         decode failure.
 */
bool encoder_read(encoder_sample_t *sample, uint32_t timeout_ms);

/**
 * @brief Report whether an encoder read is in progress.
 *
 * Why: Lets callers avoid starting a second transfer or overwriting a pending
 * sample.
 *
 * @return true when a transfer is pending; otherwise false.
 */
bool encoder_busy(void);

/**
 * @brief Return the accumulated encoder error count.
 *
 * Why: Provides a cheap diagnostic for detecting intermittent I2C or decoder
 * failures during long tests.
 *
 * @return Number of errors since initialization.
 */
uint32_t encoder_error_count(void);

#ifdef __cplusplus
}
#endif
