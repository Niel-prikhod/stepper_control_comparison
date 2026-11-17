#include <platform/dwt.h>
#include <platform/timer.h>

#if __has_include("main.h")
#define PLATFORM_HAVE_HAL 1
#include "main.h"
#include "tim.h"
#else
#define PLATFORM_HAVE_HAL 0
#endif

#if PLATFORM_HAVE_HAL
static platform_step_callback_t s_callback;
static volatile bool s_running;
static volatile bool s_have_last;
static volatile uint32_t s_last_ts;
static volatile uint32_t s_start_ts;
static volatile uint32_t s_isr_count;
static volatile uint64_t s_isr_cycles;
static volatile uint64_t s_period_cycles;
static volatile uint32_t s_peak_isr_cycles;
static volatile uint32_t s_last_period_cycles;
static volatile uint32_t s_selftest_count;

/**
 * @brief Stop the timer self-test after a fixed callback count.
 *
 * Why: Proves that timer interrupts and callback dispatch work without
 * involving the motor or measurement state.
 *
 * @param timestamp_cycles DWT timestamp captured at ISR entry; unused.
 * @param period_cycles Measured timer period; unused.
 * @return true while fewer than ten callbacks have occurred.
 */
static bool selftest_callback(uint32_t timestamp_cycles,
                              uint32_t period_cycles) {
  (void)timestamp_cycles;
  (void)period_cycles;
  s_selftest_count++;
  return s_selftest_count < 10u;
}

/**
 * @brief Clamp a requested timer period to the hardware range.
 *
 * Why: TIM1 uses a 16-bit auto-reload value, so zero and oversized periods
 * would otherwise produce invalid or truncated timing.
 *
 * @param period_us Requested period in microseconds.
 * @return Period clamped to 1..65535 microseconds.
 */
static uint32_t clamp_period_us(uint32_t period_us) {
  if (period_us == 0u) {
    return 1u;
  }
  if (period_us > 65535u) {
    return 65535u;
  }
  return period_us;
}
#endif

void platform_step_timer_init(void) {
#if PLATFORM_HAVE_HAL
  MX_TIM1_Init();

  uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();
  uint32_t timer_clock = pclk2;
  if (pclk2 != HAL_RCC_GetHCLKFreq()) {
    timer_clock = pclk2 * 2u;
  }
  uint32_t prescaler = timer_clock / 1000000u;
  if (prescaler == 0u) {
    prescaler = 1u;
  }
  __HAL_TIM_SET_PRESCALER(&htim1, prescaler - 1u);
  htim1.Instance->CR1 &= ~TIM_CR1_ARPE;

  s_callback = 0;
  s_running = false;
  s_have_last = false;
  platform_step_timer_reset_stats();
#endif
}

bool platform_step_timer_start(uint32_t period_us,
                               platform_step_callback_t callback) {
#if PLATFORM_HAVE_HAL
  if (s_running || callback == 0) {
    return false;
  }

  period_us = clamp_period_us(period_us);
  s_callback = callback;
  s_have_last = false;
  s_last_ts = 0u;
  s_start_ts = platform_dwt_read();
  s_running = true;

  __HAL_TIM_SET_AUTORELOAD(&htim1, period_us - 1u);
  __HAL_TIM_SET_COUNTER(&htim1, 0u);

  if (HAL_TIM_Base_Start_IT(&htim1) != HAL_OK) {
    s_running = false;
    s_callback = 0;
    return false;
  }
  return true;
#else
  (void)period_us;
  (void)callback;
  return false;
#endif
}

void platform_step_timer_stop(void) {
#if PLATFORM_HAVE_HAL
  if (s_running) {
    (void)HAL_TIM_Base_Stop_IT(&htim1);
    s_running = false;
  }
  __HAL_TIM_SET_COUNTER(&htim1, 0u);
  s_callback = 0;
#endif
}

bool platform_step_timer_set_period_us(uint32_t period_us) {
#if PLATFORM_HAVE_HAL
  period_us = clamp_period_us(period_us);
  __HAL_TIM_SET_AUTORELOAD(&htim1, period_us - 1u);
  return true;
#else
  (void)period_us;
  return false;
#endif
}

bool platform_step_timer_is_running(void) {
#if PLATFORM_HAVE_HAL
  return s_running;
#else
  return false;
#endif
}

void platform_step_timer_reset_stats(void) {
#if PLATFORM_HAVE_HAL
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  s_isr_count = 0u;
  s_isr_cycles = 0u;
  s_period_cycles = 0u;
  s_peak_isr_cycles = 0u;
  s_last_period_cycles = 0u;
  if (primask == 0u) {
    __enable_irq();
  }
#endif
}

void platform_step_timer_get_stats(platform_step_stats_t *stats) {
#if PLATFORM_HAVE_HAL
  if (stats == 0) {
    return;
  }
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  stats->isr_count = s_isr_count;
  stats->isr_cycles = s_isr_cycles;
  stats->period_cycles = s_period_cycles;
  stats->peak_isr_cycles = s_peak_isr_cycles;
  stats->last_period_cycles = s_last_period_cycles;
  if (primask == 0u) {
    __enable_irq();
  }
#else
  if (stats != 0) {
    stats->isr_count = 0u;
    stats->isr_cycles = 0u;
    stats->period_cycles = 0u;
    stats->peak_isr_cycles = 0u;
    stats->last_period_cycles = 0u;
  }
#endif
}

bool platform_step_timer_selftest(void) {
#if PLATFORM_HAVE_HAL
  s_selftest_count = 0u;

  platform_step_timer_reset_stats();
  if (!platform_step_timer_start(1000u, selftest_callback)) {
    return false;
  }

  uint32_t start = platform_dwt_read();
  uint32_t timeout = (uint32_t)(((uint64_t)100000u * platform_dwt_hz()) /
                                1000000ULL);
  uint32_t spins = 0u;
  bool timed = platform_dwt_available();
  while (s_selftest_count < 10u && platform_step_timer_is_running()) {
    if (timed && (uint32_t)(platform_dwt_read() - start) > timeout) {
      break;
    }
    if (!timed && spins++ > 10000000u) {
      break;
    }
  }
  platform_step_timer_stop();

  platform_step_stats_t stats;
  platform_step_timer_get_stats(&stats);
  return s_selftest_count >= 10u && stats.isr_count >= 10u;
#else
  return false;
#endif
}

#if PLATFORM_HAVE_HAL
/**
 * @brief Handle TIM1 update interrupts for step generation.
 *
 * Why: This is the bridge between the HAL timer interrupt and the measurement
 * callback; it also records the ISR execution-time statistics used by mode 3.
 *
 * @param htim HAL timer handle that generated the interrupt.
 * @return None.
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
  if (htim->Instance != TIM1 || !s_running) {
    return;
  }

  uint32_t entry = platform_dwt_read();
  uint32_t period = s_have_last ? (uint32_t)(entry - s_last_ts)
                                : (uint32_t)(entry - s_start_ts);
  if (period == 0u) {
    period = 1u;
  }

  s_last_ts = entry;
  s_have_last = true;
  s_last_period_cycles = period;
  s_period_cycles += period;
  s_isr_count++;

  bool keep_running = true;
  if (s_callback != 0) {
    keep_running = s_callback(entry, period);
  }

  uint32_t exit = platform_dwt_read();
  uint32_t duration = (uint32_t)(exit - entry);
  s_isr_cycles += duration;
  if (duration > s_peak_isr_cycles) {
    s_peak_isr_cycles = duration;
  }

  if (!keep_running) {
    (void)HAL_TIM_Base_Stop_IT(&htim1);
    s_running = false;
  }
}
#endif
