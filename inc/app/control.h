#pragma once

/**
 * @brief Initialize the application control layer.
 *
 * Why: Initializes measurement, status, encoder, and platform self-tests so the
 * firmware can report READY and accept commands from a known state.
 *
 * @return None.
 */
void app_control_init(void);

/**
 * @brief Run one iteration of the application command loop.
 *
 * Why: Services the status LED and consumes at most one complete UART command
 * without blocking the main loop.
 *
 * @return None.
 */
void app_control_loop(void);
