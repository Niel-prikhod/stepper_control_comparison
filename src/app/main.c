#include <app/control.h>
#include <platform/stepper.h>

/**
 * @brief Firmware entry point.
 *
 * Why: Initializes board resources and the application, then runs the
 * non-blocking command loop forever.
 *
 * @return Never returns in normal operation; the return value exists for
 *         hosted builds.
 */
int main(void) {
  platform_init();
  app_control_init();
  while (1) {
    app_control_loop();
  }
  return 0;
}
