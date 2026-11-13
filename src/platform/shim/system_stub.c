// Provides SystemInit + dummy SystemCoreClock when real CubeMX
// system_stm32xx.c is not yet present. CubeMX files take precedence
// (see platform/CMakeLists.txt) so this file is ignored afterwards.
#include <stdint.h>
uint32_t SystemCoreClock = 16000000UL;
void SystemInit(void) {}
