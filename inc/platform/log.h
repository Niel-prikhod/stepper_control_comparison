#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the logging UART backend.
 *
 * Why: Opens the VCP/log port before the firmware emits self-test or
 * measurement output.
 *
 * @return None.
 */
void platform_log_init(void);

/**
 * @brief Format and transmit one log message.
 *
 * Why: Gives application code a printf-style output path without depending on
 * a specific UART or HAL implementation.
 *
 * @param fmt printf-compatible format string.
 * @param ... Arguments referenced by @p fmt.
 * @return None.
 */
void platform_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
