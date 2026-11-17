#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PLATFORM_UART_LOOPBACK_REQUIRED
/**
 * When non-zero, startup fails unless the UART loopback test line is received.
 * The default is zero because the VCP transmit and receive paths can be tested
 * independently without a physical TX/RX jumper.
 */
#define PLATFORM_UART_LOOPBACK_REQUIRED 0
#endif

/**
 * @brief Initialize the command UART receiver.
 *
 * Why: Enables the LPUART interrupt and arms the first byte receive so commands
 * can be accepted after startup.
 *
 * @return None.
 */
void platform_uart_init(void);

/**
 * @brief Report whether the command UART is ready.
 *
 * Why: Lets startup self-test distinguish a receive-arming failure from a
 * loopback failure.
 *
 * @return true when the first receive is armed; otherwise false.
 */
bool platform_uart_ready(void);

/**
 * @brief Test UART transmit and optional loopback.
 *
 * Why: Verifies the command path before accepting measurements and records
 * whether a physical loopback was confirmed.
 *
 * @return true when the transmit path works and either loopback is verified or
 *         not required; false on transmit failure or required-loopback timeout.
 */
bool platform_uart_selftest(void);

/**
 * @brief Pop one complete received line, if available.
 *
 * Why: Provides a non-blocking command source and a receive timestamp for
 * latency measurements.
 *
 * @param buf Destination buffer for the null-terminated line.
 * @param buf_len Size of @p buf in bytes; must be non-zero.
 * @param rx_cycles Optional destination for the DWT timestamp captured at the
 *        first received byte of the line; may be null.
 * @return true when a line was copied; false when no line is ready or arguments
 *         are invalid.
 */
bool platform_uart_read_line(char *buf, uint32_t buf_len,
                             uint32_t *rx_cycles);

/**
 * @brief Return the configured command UART baud rate.
 *
 * Why: Records the actual baud rate in logs and validates host/firmware
 * agreement.
 *
 * @return Baud rate in bits per second.
 */
uint32_t platform_uart_baud(void);

/**
 * @brief Report whether the loopback test was physically verified.
 *
 * Why: Separates a software-only UART self-test from a confirmed TX-to-RX
 * path in diagnostics.
 *
 * @return true when the loopback test line was received; otherwise false.
 */
bool platform_uart_loopback_verified(void);

/**
 * @brief Return the accumulated UART receive error count.
 *
 * Why: Helps detect framing, noise, or overrun problems during long
 * measurement runs.
 *
 * @return Number of receive errors since initialization.
 */
uint32_t platform_uart_rx_error_count(void);

#ifdef __cplusplus
}
#endif
