#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** High-level firmware state reflected by the status LED. */
typedef enum {
  /** Firmware is ready and idle. */
  APP_STATUS_OK = 0,
  /** A measurement is running. */
  APP_STATUS_ACTIVE,
  /** Startup self-test or hardware fault. */
  APP_STATUS_FAULT,
} app_status_t;

/**
 * @brief Initialize the status LED service.
 *
 * Why: Establishes a known LED state and timing base before the main loop
 * starts servicing status updates.
 *
 * @return None.
 */
void app_status_init(void);

/**
 * @brief Change the high-level status and its LED pattern.
 *
 * Why: Lets control and measurement code report idle, active, and fault states
 * consistently without knowing the LED timing details.
 *
 * @param status New application status.
 * @return None.
 */
void app_status_set(app_status_t status);

/**
 * @brief Advance the non-blocking status LED pattern.
 *
 * Why: Must be called regularly from the main loop to blink the LED without
 * blocking measurements or command processing.
 *
 * @return None.
 */
void app_status_service(void);

#ifdef __cplusplus
}
#endif
