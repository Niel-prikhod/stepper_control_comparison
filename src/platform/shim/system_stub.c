// Provides SystemInit + dummy SystemCoreClock when real CubeMX
// system_stm32xx.c is not yet present. CubeMX files take precedence
// (see platform/CMakeLists.txt) so this file is ignored afterwards.
#include <stdint.h>
/** Fallback core clock used by shim builds without CubeMX system files. */
uint32_t SystemCoreClock = 16000000UL;

/**
 * @brief Provide a no-op startup hook for shim builds.
 *
 * Why: CMSIS startup code calls SystemInit before main; boards without CubeMX
 * output still need the symbol, but there is no clock tree to configure here.
 *
 * @return None.
 */
void SystemInit(void) {}
