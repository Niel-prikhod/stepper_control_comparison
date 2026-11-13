#include <platform/log.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#if __has_include("main.h")
#define PLATFORM_HAVE_HAL 1
#include "main.h"
#else
#define PLATFORM_HAVE_HAL 0
#endif

#if PLATFORM_HAVE_HAL
static bool s_log_ready;
#endif

void platform_log_init(void) {
#if PLATFORM_HAVE_HAL
  COM_InitTypeDef com = {0};

  com.BaudRate = 115200;
  com.WordLength = COM_WORDLENGTH_8B;
  com.StopBits = COM_STOPBITS_1;
  com.Parity = COM_PARITY_NONE;
  com.HwFlowCtl = COM_HWCONTROL_NONE;

  if (BSP_COM_Init(COM1, &com) == BSP_ERROR_NONE) {
    (void)BSP_COM_SelectLogPort(COM1);
    s_log_ready = true;
  }
#endif
}

void platform_log(const char *fmt, ...) {
#if PLATFORM_HAVE_HAL
  char buf[128];
  va_list args;

  if (!s_log_ready) {
    return;
  }

  va_start(args, fmt);
  int len = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (len > 0) {
    if (len >= (int)sizeof(buf)) {
      len = (int)sizeof(buf) - 1;
    }
    (void)HAL_UART_Transmit(&hcom_uart[COM1], (uint8_t *)buf,
                            (uint16_t)len, 100u);
  }
#else
  (void)fmt;
#endif
}
