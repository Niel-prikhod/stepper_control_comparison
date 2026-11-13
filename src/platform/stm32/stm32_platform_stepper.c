#include <platform/i2c.h>
#include <platform/log.h>
#include <platform/stepper.h>

#if __has_include("main.h")
#define PLATFORM_HAVE_HAL 1
#include "main.h"
#include "gpio.h"
extern void SystemClock_Config(void);
#else
#define PLATFORM_HAVE_HAL 0
#define MOTOR_A_GPIO_Port 0
#define MOTOR_A_Pin 0
#define MOTOR_B_GPIO_Port 0
#define MOTOR_B_Pin 0
#define MOTOR_C_GPIO_Port 0
#define MOTOR_C_Pin 0
#define MOTOR_D_GPIO_Port 0
#define MOTOR_D_Pin 0
#define GPIO_PIN_SET 1
#define GPIO_PIN_RESET 0
static inline void HAL_GPIO_WritePin(int port, int pin, int state) {
  (void)port;
  (void)pin;
  (void)state;
}
#endif

void platform_init(void) {
#if PLATFORM_HAVE_HAL
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  BSP_LED_Init(LED2);
  platform_i2c_init();
  platform_log_init();
#endif
}

void platform_motor_write_coils(uint8_t mask) {
  HAL_GPIO_WritePin(MOTOR_A_GPIO_Port, MOTOR_A_Pin,
                    (mask & PLATFORM_COIL_A) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(MOTOR_B_GPIO_Port, MOTOR_B_Pin,
                    (mask & PLATFORM_COIL_B) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(MOTOR_C_GPIO_Port, MOTOR_C_Pin,
                    (mask & PLATFORM_COIL_C) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(MOTOR_D_GPIO_Port, MOTOR_D_Pin,
                    (mask & PLATFORM_COIL_D) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void platform_motor_enable(bool enabled) {
  (void)enabled;
}

void platform_delay_us(uint32_t us) {
#if PLATFORM_HAVE_HAL
  if (us >= 1000U) {
    HAL_Delay(us / 1000U);
  } else {
    uint32_t n = us * (HAL_RCC_GetHCLKFreq() / 1000000UL / 5UL);
    for (volatile uint32_t i = 0; i < n; i++) {
      __asm__ volatile("nop");
    }
  }
#else
  for (volatile uint32_t i = 0; i < us * 10UL; i++) {
    __asm__ volatile("nop");
  }
#endif
}

void platform_led_toggle(void) {
#if PLATFORM_HAVE_HAL
  BSP_LED_Toggle(LED2);
#endif
}
