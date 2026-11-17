#include <platform/cpu.h>

#if __has_include("main.h")
#define PLATFORM_HAVE_HAL 1
#include "main.h"
#else
#define PLATFORM_HAVE_HAL 0
#endif

void platform_cpu_wait_for_event(void) {
#if PLATFORM_HAVE_HAL
  __WFE();
#else
  __asm__ volatile("nop");
#endif
}
