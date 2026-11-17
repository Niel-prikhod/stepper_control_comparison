#include <app/status.h>

#include <platform/dwt.h>
#include <platform/stepper.h>

static app_status_t s_status;
static bool s_led_on;
static uint32_t s_last_cycles;
static uint32_t s_period_cycles;

/**
 * @brief Convert a status value to its LED half-period in DWT cycles.
 *
 * Why: The LED pattern timing must be independent of the main-loop rate and
 * scale correctly with the configured clock.
 *
 * @param status Status whose blink period is required.
 * @return Half-period in DWT cycles, never zero.
 */
static uint32_t status_period_cycles(app_status_t status) {
  uint32_t period_us;
  if (status == APP_STATUS_FAULT) {
    period_us = 50000u;
  } else if (status == APP_STATUS_ACTIVE) {
    period_us = 100000u;
  } else {
    period_us = 500000u;
  }
  uint32_t cycles = (uint32_t)(((uint64_t)period_us * platform_dwt_hz()) /
                               1000000ULL);
  return cycles == 0u ? 1u : cycles;
}

void app_status_init(void) {
  s_status = APP_STATUS_OK;
  s_led_on = false;
  s_last_cycles = platform_dwt_read();
  s_period_cycles = status_period_cycles(s_status);
  platform_led_set(false);
}

void app_status_set(app_status_t status) {
  s_status = status;
  s_led_on = false;
  s_last_cycles = platform_dwt_read();
  s_period_cycles = status_period_cycles(status);
  platform_led_set(false);
}

void app_status_service(void) {
  if (!platform_dwt_available()) {
    static uint32_t fallback;
    if (++fallback < 100000u) {
      return;
    }
    fallback = 0u;
    s_led_on = !s_led_on;
    platform_led_set(s_led_on);
    return;
  }

  uint32_t now = platform_dwt_read();
  if ((uint32_t)(now - s_last_cycles) < s_period_cycles) {
    return;
  }

  s_last_cycles = now;
  s_led_on = !s_led_on;
  platform_led_set(s_led_on);
}
