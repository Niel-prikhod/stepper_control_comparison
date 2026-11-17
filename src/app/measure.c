#include <app/measure.h>

#include <app/status.h>
#include <drivers/encoder/encoder.h>
#include <drivers/motor/stepper.h>
#include <platform/dwt.h>
#include <platform/log.h>
#include <platform/stepper.h>
#include <platform/timer.h>
#include <platform/uart.h>

#define MODE5_STEPS_PER_POINT 200u
#define MODE5_TOLERANCE_STEPS 2u
#define MODE5_RAMP_STEPS 50u
#define STALL_TIMEOUT_US 2000000u

static volatile bool s_done;
static volatile uint32_t s_count;
static volatile uint32_t s_samples;
static volatile uint32_t s_first_ts;
static volatile uint32_t s_last_ts;
static volatile uint32_t s_start_ts;
static volatile uint32_t s_duration_cycles;
static volatile uint32_t s_target_samples;
static volatile uint32_t s_target_hz;
static volatile uint32_t s_ramp_start_hz;
static volatile uint32_t s_ramp_steps;
static volatile uint32_t s_step_target;
static volatile uint32_t s_step_index;
static volatile uint8_t s_condition;
static volatile uint32_t s_latency_first;

static uint32_t s_stat_n;
static int64_t s_stat_mean;
static int64_t s_stat_m2;
static uint32_t s_stat_min;
static uint32_t s_stat_max;

/**
 * @brief Convert a step frequency to a timer period.
 *
 * Why: The timer compares against a 16-bit auto-reload value, so invalid or
 * out-of-range frequencies must be converted to a safe period before use.
 *
 * @param hz Requested step frequency in Hz; zero is treated as 1 Hz.
 * @return Timer period in microseconds, clamped to 1..65535.
 */
static uint32_t hz_to_period_us(uint32_t hz) {
  if (hz == 0u) {
    hz = 1u;
  }
  uint32_t period = 1000000u / hz;
  if (period == 0u) {
    period = 1u;
  }
  if (period > 65535u) {
    period = 65535u;
  }
  return period;
}

/**
 * @brief Convert microseconds to DWT cycles.
 *
 * Why: Stall and timeout checks compare against the free-running DWT counter,
 * not a millisecond tick.
 *
 * @param us Time in microseconds.
 * @return Equivalent cycles, saturated at UINT32_MAX.
 */
static uint32_t us_to_cycles(uint32_t us) {
  uint64_t cycles = ((uint64_t)us * platform_dwt_hz()) / 1000000ULL;
  if (cycles > 0xFFFFFFFFULL) {
    return 0xFFFFFFFFu;
  }
  return (uint32_t)cycles;
}

/**
 * @brief Stop timer-driven output and de-energize the motor.
 *
 * Why: Every measurement path, including failures, must leave the motor safe
 * and prevent further timer interrupts.
 *
 * @return None.
 */
static void stop_output(void) {
  platform_step_timer_stop();
  stepper_deenergize();
}

/**
 * @brief Drain incoming commands while a measurement is active.
 *
 * Why: Keeps the UART receive path responsive and tells the host that new
 * commands were rejected because the firmware is busy.
 *
 * @return None.
 */
static void service_busy(void) {
  char line[96];
  uint32_t rx_cycles;
  while (platform_uart_read_line(line, sizeof(line), &rx_cycles)) {
    platform_log("ERR BUSY\r\n");
  }
}

/**
 * @brief Wait for measurement progress or a stall timeout.
 *
 * Why: Centralizes status servicing, busy-command handling, and stall
 * detection so a wedged motor or ISR cannot hang the firmware.
 *
 * @param progress Counter that must change to indicate forward progress.
 * @param stall_us Maximum time without a progress change, in microseconds.
 * @return true when the measurement completed; false when it stalled.
 */
static bool wait_progress(volatile uint32_t *progress, uint32_t stall_us) {
  uint32_t last = *progress;
  uint32_t start = platform_dwt_read();
  uint32_t timeout = us_to_cycles(stall_us);

  while (!s_done) {
    app_status_service();
    service_busy();

    uint32_t now = platform_dwt_read();
    if (*progress != last) {
      last = *progress;
      start = now;
    } else if ((uint32_t)(now - start) > timeout) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Reset the streaming interval statistics.
 *
 * Why: Each mode 2 and mode 4 run must start with an empty Welford accumulator
 * and fresh min/max values.
 *
 * @return None.
 */
static void stats_reset(void) {
  s_stat_n = 0u;
  s_stat_mean = 0;
  s_stat_m2 = 0;
  s_stat_min = 0xFFFFFFFFu;
  s_stat_max = 0u;
}

/**
 * @brief Add one value to the streaming interval statistics.
 *
 * Why: Updates mean, variance accumulator, minimum, and maximum in constant
 * memory, which keeps the ISR path lightweight.
 *
 * @param value Interval or latency sample to add.
 * @return None.
 */
static void stats_add(uint32_t value) {
  s_stat_n++;
  int64_t delta = (int64_t)value - s_stat_mean;
  s_stat_mean += delta / (int64_t)s_stat_n;
  s_stat_m2 += delta * ((int64_t)value - s_stat_mean);
  if (value < s_stat_min) {
    s_stat_min = value;
  }
  if (value > s_stat_max) {
    s_stat_max = value;
  }
}

/**
 * @brief Compute the integer square root of a 64-bit value.
 *
 * Why: Standard deviation needs a square root, but the firmware avoids linking
 * the floating-point math library for a small integer calculation.
 *
 * @param value Non-negative value to take the square root of.
 * @return Floor of the square root.
 */
static uint32_t isqrt64(uint64_t value) {
  uint64_t low = 0u;
  uint64_t high = 0xFFFFFFFFULL;
  while (low < high) {
    uint64_t mid = (low + high + 1u) / 2u;
    if (mid <= value / mid) {
      low = mid;
    } else {
      high = mid - 1u;
    }
  }
  return (uint32_t)low;
}

/**
 * @brief Format a 64-bit unsigned value as a decimal string.
 *
 * Why: The embedded newlib configuration does not print `%llu` correctly, so
 * large ISR and period totals need an explicit formatter.
 *
 * @param value Value to format.
 * @param buf Destination buffer.
 * @param buf_len Size of @p buf in bytes.
 * @return None.
 */
static void u64_to_dec(uint64_t value, char *buf, uint32_t buf_len) {
  char tmp[21];
  uint32_t n = 0u;

  if (buf_len == 0u) {
    return;
  }

  do {
    tmp[n++] = (char)('0' + (value % 10u));
    value /= 10u;
  } while (value != 0u);

  if (n >= buf_len) {
    n = buf_len - 1u;
  }
  for (uint32_t i = 0u; i < n; i++) {
    buf[i] = tmp[n - 1u - i];
  }
  buf[n] = '\0';
}

/**
 * @brief Step the motor and stop mode 1 at the requested duration.
 *
 * Why: Mode 1 needs timer-driven stepping with first/last timestamps and a
 * deterministic stop without polling from the main loop.
 *
 * @param timestamp_cycles DWT timestamp captured at ISR entry.
 * @param period_cycles Measured timer period; unused by mode 1.
 * @return true to continue stepping; false when the duration has elapsed.
 */
static bool mode1_callback(uint32_t timestamp_cycles,
                           uint32_t period_cycles) {
  (void)period_cycles;
  (void)stepper_advance(1);
  if (s_count == 0u) {
    s_first_ts = timestamp_cycles;
  }
  s_last_ts = timestamp_cycles;
  s_count++;
  if ((uint32_t)(timestamp_cycles - s_start_ts) >= s_duration_cycles) {
    s_done = true;
    return false;
  }
  return true;
}

bool app_measure_mode1(uint32_t target_hz, uint32_t duration_ms) {
  s_done = false;
  s_count = 0u;
  s_first_ts = 0u;
  s_last_ts = 0u;
  s_target_hz = target_hz;
  s_duration_cycles =
      (uint32_t)(((uint64_t)duration_ms * platform_dwt_hz()) / 1000ULL);
  s_start_ts = platform_dwt_read();

  if (!platform_step_timer_start(hz_to_period_us(target_hz), mode1_callback)) {
    platform_log("error=timer\r\n");
    return false;
  }

  bool ok = wait_progress(&s_count, STALL_TIMEOUT_US);
  stop_output();

  if (!ok || s_count < 2u) {
    platform_log("error=timeout\r\n");
    return false;
  }

  uint32_t observed = (uint32_t)(s_last_ts - s_first_ts);
  if (observed == 0u) {
    platform_log("error=timing\r\n");
    return false;
  }

  uint32_t intervals = s_count - 1u;
  uint64_t measured_milli =
      ((uint64_t)intervals * platform_dwt_hz() * 1000ULL) / observed;
  uint32_t target_milli = target_hz * 1000u;
  uint32_t abs_milli = measured_milli > target_milli
                           ? (uint32_t)(measured_milli - target_milli)
                           : (uint32_t)(target_milli - measured_milli);
  uint32_t rel_milli_pct =
      target_milli == 0u
          ? 0u
          : (uint32_t)(((uint64_t)abs_milli * 100000ULL) / target_milli);
  uint32_t duration_us = platform_dwt_cycles_to_us(observed);

  platform_log("mode=1\r\n");
  platform_log("target_hz=%lu\r\n", (unsigned long)target_hz);
  platform_log("measured_hz=%lu.%03lu\r\n",
               (unsigned long)(measured_milli / 1000ULL),
               (unsigned long)(measured_milli % 1000ULL));
  platform_log("abs_error_hz=%lu.%03lu\r\n",
               (unsigned long)(abs_milli / 1000u),
               (unsigned long)(abs_milli % 1000u));
  platform_log("rel_error_pct=%lu.%03lu\r\n",
               (unsigned long)(rel_milli_pct / 1000u),
               (unsigned long)(rel_milli_pct % 1000u));
  platform_log("step_count=%lu\r\n", (unsigned long)s_count);
  platform_log("duration_ms=%lu\r\n", (unsigned long)(duration_us / 1000u));
  platform_log("clock_hz=%lu\r\n", (unsigned long)platform_dwt_hz());
  return true;
}

/**
 * @brief Step the motor and collect mode 2 interval samples.
 *
 * Why: Mode 2 needs per-interval timing data from the timer ISR while keeping
 * the statistics work bounded and allocation-free.
 *
 * @param timestamp_cycles DWT timestamp captured at ISR entry.
 * @param period_cycles Measured timer period in DWT cycles.
 * @return true to continue sampling; false after the requested sample count.
 */
static bool mode2_callback(uint32_t timestamp_cycles,
                           uint32_t period_cycles) {
  (void)stepper_advance(1);

  if (s_count == 0u) {
    s_first_ts = timestamp_cycles;
    s_last_ts = timestamp_cycles;
    s_count++;
    return true;
  }

  s_count++;
  s_last_ts = timestamp_cycles;
  uint32_t interval_us = platform_dwt_cycles_to_us(period_cycles);
  if (interval_us == 0u) {
    interval_us = 1u;
  }
  stats_add(interval_us);
  s_samples++;

  if (s_samples >= s_target_samples) {
    s_done = true;
    return false;
  }
  return true;
}

bool app_measure_mode2(uint32_t target_hz, uint32_t samples) {
  s_done = false;
  s_count = 0u;
  s_samples = 0u;
  s_target_samples = samples;
  s_target_hz = target_hz;
  s_first_ts = 0u;
  s_last_ts = 0u;
  stats_reset();

  if (!platform_step_timer_start(hz_to_period_us(target_hz), mode2_callback)) {
    platform_log("error=timer\r\n");
    return false;
  }

  bool ok = wait_progress(&s_samples, STALL_TIMEOUT_US);
  stop_output();

  if (!ok || s_stat_n < samples) {
    platform_log("error=timeout\r\n");
    return false;
  }

  uint32_t mean_us = (uint32_t)s_stat_mean;
  uint32_t p2p_us = s_stat_max - s_stat_min;
  uint64_t jitter_milli_pct64 =
      mean_us == 0u
          ? 0u
          : (((uint64_t)p2p_us * 100000ULL) / mean_us);
  uint32_t jitter_milli_pct =
      jitter_milli_pct64 > 0xFFFFFFFFULL
          ? 0xFFFFFFFFu
          : (uint32_t)jitter_milli_pct64;
  int64_t variance = s_stat_m2;
  if (variance < 0) {
    variance = 0;
  }
  uint32_t stddev_us =
      s_stat_n > 1u ? isqrt64((uint64_t)variance / (s_stat_n - 1u)) : 0u;
  uint32_t measured_milli =
      mean_us == 0u ? 0u : (uint32_t)(1000000000ULL / mean_us);

  platform_log("mode=2\r\n");
  platform_log("target_hz=%lu\r\n", (unsigned long)target_hz);
  platform_log("measured_hz=%lu.%03lu\r\n",
               (unsigned long)(measured_milli / 1000u),
               (unsigned long)(measured_milli % 1000u));
  platform_log("samples=%lu\r\n", (unsigned long)s_stat_n);
  platform_log("mean_interval_us=%lu\r\n", (unsigned long)mean_us);
  platform_log("stddev_us=%lu\r\n", (unsigned long)stddev_us);
  platform_log("min_interval_us=%lu\r\n", (unsigned long)s_stat_min);
  platform_log("max_interval_us=%lu\r\n", (unsigned long)s_stat_max);
  platform_log("p2p_jitter_us=%lu\r\n", (unsigned long)p2p_us);
  platform_log("jitter_pct=%lu.%03lu\r\n",
               (unsigned long)(jitter_milli_pct / 1000u),
               (unsigned long)(jitter_milli_pct % 1000u));
  platform_log("clock_hz=%lu\r\n", (unsigned long)platform_dwt_hz());
  return true;
}

/**
 * @brief Run one mode 3 timer interrupt condition.
 *
 * Why: Mode 3 needs to count interrupts in idle, constant, and ramped modes
 * while optionally stepping and adjusting the timer period.
 *
 * @param timestamp_cycles DWT timestamp captured at ISR entry; unused.
 * @param period_cycles Measured timer period; unused.
 * @return true to continue the condition; false after the requested samples.
 */
static bool mode3_callback(uint32_t timestamp_cycles,
                           uint32_t period_cycles) {
  (void)timestamp_cycles;
  (void)period_cycles;

  if (s_condition != 0u) {
    (void)stepper_advance(1);
  }

  s_count++;

  if (s_condition == 2u) {
    uint32_t half = s_target_samples / 2u;
    uint32_t frequency;
    if (half == 0u) {
      frequency = s_target_hz;
    } else if (s_count < half) {
      frequency = s_ramp_start_hz +
                  (uint32_t)(((uint64_t)(s_target_hz - s_ramp_start_hz) *
                              s_count) /
                             half);
    } else {
      uint32_t remaining =
          s_count < s_target_samples ? s_target_samples - s_count : 0u;
      frequency = s_ramp_start_hz +
                  (uint32_t)(((uint64_t)(s_target_hz - s_ramp_start_hz) *
                              remaining) /
                             half);
    }
    if (frequency < APP_MIN_STEP_HZ) {
      frequency = APP_MIN_STEP_HZ;
    }
    if (frequency > APP_MAX_STEP_HZ) {
      frequency = APP_MAX_STEP_HZ;
    }
    (void)platform_step_timer_set_period_us(hz_to_period_us(frequency));
  }

  if (s_count >= s_target_samples) {
    s_done = true;
    return false;
  }
  return true;
}

/**
 * @brief Run one mode 3 condition and capture its timer statistics.
 *
 * Why: Keeps the idle, constant, and ramp runs identical apart from the
 * condition flag, making their load numbers directly comparable.
 *
 * @param condition 0 = idle, 1 = constant, 2 = ramp.
 * @param target_hz Target step frequency in Hz.
 * @param samples Number of timer interrupts to collect.
 * @param ramp_start_hz Start frequency for the ramp condition.
 * @param stats Destination for the captured timer statistics.
 * @return true when the condition completed; false on timer start failure or
 *         stall.
 */
static bool run_condition(uint8_t condition, uint32_t target_hz,
                          uint32_t samples, uint32_t ramp_start_hz,
                          platform_step_stats_t *stats) {
  s_condition = condition;
  s_target_hz = target_hz;
  s_target_samples = samples;
  s_ramp_start_hz = ramp_start_hz;
  s_count = 0u;
  s_done = false;

  platform_step_timer_reset_stats();
  uint32_t start_hz = condition == 2u ? ramp_start_hz : target_hz;
  if (!platform_step_timer_start(hz_to_period_us(start_hz), mode3_callback)) {
    return false;
  }

  bool ok = wait_progress(&s_count, STALL_TIMEOUT_US);
  stop_output();
  platform_step_timer_get_stats(stats);
  return ok && s_count >= samples;
}

/**
 * @brief Emit one mode 3 condition's statistics.
 *
 * Why: Keeps the idle, constant, and ramp output fields identical so host
 * scripts and humans can compare them directly.
 *
 * @param name Condition name written to the log.
 * @param stats Statistics snapshot to report.
 * @return None.
 */
static void log_condition(const char *name,
                          const platform_step_stats_t *stats) {
  uint32_t load_milli =
      stats->period_cycles == 0u
          ? 0u
          : (uint32_t)((stats->isr_cycles * 100000ULL) / stats->period_cycles);
  uint32_t peak_us = platform_dwt_cycles_to_us(stats->peak_isr_cycles);
  char isr_cycles[21];
  char period_cycles[21];

  u64_to_dec(stats->isr_cycles, isr_cycles, sizeof(isr_cycles));
  u64_to_dec(stats->period_cycles, period_cycles, sizeof(period_cycles));

  platform_log("condition=%s\r\n", name);
  platform_log("load_pct=%lu.%03lu\r\n",
               (unsigned long)(load_milli / 1000u),
               (unsigned long)(load_milli % 1000u));
  platform_log("isr_count=%lu\r\n", (unsigned long)stats->isr_count);
  platform_log("isr_cycles=%s\r\n", isr_cycles);
  platform_log("period_cycles=%s\r\n", period_cycles);
  platform_log("peak_isr_cycles=%lu\r\n",
               (unsigned long)stats->peak_isr_cycles);
  platform_log("peak_isr_us=%lu\r\n", (unsigned long)peak_us);
}

bool app_measure_mode3(uint32_t target_hz, uint32_t samples) {
  uint32_t ramp_start_hz = target_hz / 10u;
  if (ramp_start_hz < APP_MIN_STEP_HZ) {
    ramp_start_hz = APP_MIN_STEP_HZ;
  }
  if (ramp_start_hz > target_hz) {
    ramp_start_hz = target_hz;
  }

  platform_step_stats_t idle_stats;
  platform_step_stats_t constant_stats;
  platform_step_stats_t ramp_stats;

  if (!run_condition(0u, target_hz, samples, ramp_start_hz, &idle_stats)) {
    platform_log("error=timeout condition=idle\r\n");
    return false;
  }
  if (!run_condition(1u, target_hz, samples, ramp_start_hz, &constant_stats)) {
    platform_log("error=timeout condition=constant\r\n");
    return false;
  }
  if (!run_condition(2u, target_hz, samples, ramp_start_hz, &ramp_stats)) {
    platform_log("error=timeout condition=ramp\r\n");
    return false;
  }

  platform_log("mode=3\r\n");
  platform_log("target_hz=%lu\r\n", (unsigned long)target_hz);
  platform_log("samples=%lu\r\n", (unsigned long)samples);
  platform_log("ramp_start_hz=%lu\r\n", (unsigned long)ramp_start_hz);
  platform_log("clock_hz=%lu\r\n", (unsigned long)platform_dwt_hz());
  log_condition("idle", &idle_stats);
  log_condition("constant", &constant_stats);
  log_condition("ramp", &ramp_stats);
  return true;
}

/**
 * @brief Perform the first mode 4 step and stop the timer.
 *
 * Why: Mode 4 needs a single timer-triggered step so it can measure the time
 * from command processing to the first motor action.
 *
 * @param timestamp_cycles DWT timestamp captured at ISR entry; unused.
 * @param period_cycles Measured timer period; unused.
 * @return false, because mode 4 only needs one step per trial.
 */
static bool mode4_callback(uint32_t timestamp_cycles,
                           uint32_t period_cycles) {
  (void)timestamp_cycles;
  (void)period_cycles;
  (void)stepper_advance(1);
  s_latency_first = platform_dwt_read();
  s_count++;
  s_done = true;
  return false;
}

bool app_measure_mode4(uint32_t trials, uint32_t cmd_len,
                       uint32_t initial_rx_cycles) {
  stats_reset();
  uint32_t first_uart_latency_us = 0u;

  for (uint32_t trial = 0u; trial < trials; trial++) {
    s_done = false;
    s_count = 0u;
    s_latency_first = 0u;
    uint32_t command_ts = platform_dwt_read();

    if (!platform_step_timer_start(100u, mode4_callback)) {
      platform_log("error=timer\r\n");
      return false;
    }

    bool ok = wait_progress(&s_count, 1000000u);
    stop_output();
    if (!ok || s_count == 0u) {
      platform_log("error=timeout\r\n");
      return false;
    }

    uint32_t latency_us =
        platform_dwt_cycles_to_us((uint32_t)(s_latency_first - command_ts));
    stats_add(latency_us);

    if (trial == 0u) {
      first_uart_latency_us = platform_dwt_cycles_to_us(
          (uint32_t)(s_latency_first - initial_rx_cycles));
    }

    if (trial + 1u < trials) {
      platform_delay_us(1000u);
    }
  }

  uint32_t mean_us = (uint32_t)s_stat_mean;
  int64_t variance = s_stat_m2;
  if (variance < 0) {
    variance = 0;
  }
  uint32_t stddev_us =
      s_stat_n > 1u ? isqrt64((uint64_t)variance / (s_stat_n - 1u)) : 0u;

  platform_log("mode=4\r\n");
  platform_log("num_trials=%lu\r\n", (unsigned long)s_stat_n);
  platform_log("baud=%lu\r\n", (unsigned long)platform_uart_baud());
  platform_log("cmd_len=%lu\r\n", (unsigned long)cmd_len);
  platform_log("latency_ref=cmd_process\r\n");
  platform_log("uart_rx_to_first_step_us=%lu\r\n",
               (unsigned long)first_uart_latency_us);
  platform_log("mean_us=%lu\r\n", (unsigned long)mean_us);
  platform_log("stddev_us=%lu\r\n", (unsigned long)stddev_us);
  platform_log("min_us=%lu\r\n", (unsigned long)s_stat_min);
  platform_log("max_us=%lu\r\n", (unsigned long)s_stat_max);
  platform_log("clock_hz=%lu\r\n", (unsigned long)platform_dwt_hz());
  return true;
}

/**
 * @brief Compute the timer period for one step in a mode 5 ramp.
 *
 * Why: Mode 5 ramps from the sweep start frequency to the test frequency over
 * a fixed number of steps, then holds the test frequency.
 *
 * @param index Zero-based step index within the point.
 * @return Timer period in microseconds for that step.
 */
static uint32_t mode5_period_for_index(uint32_t index) {
  uint32_t frequency;
  if (s_ramp_steps == 0u || index >= s_ramp_steps) {
    frequency = s_target_hz;
  } else {
    frequency =
        s_ramp_start_hz +
        (uint32_t)(((uint64_t)(s_target_hz - s_ramp_start_hz) * index) /
                   s_ramp_steps);
  }
  if (frequency < APP_MIN_STEP_HZ) {
    frequency = APP_MIN_STEP_HZ;
  }
  if (frequency > APP_MAX_STEP_HZ) {
    frequency = APP_MAX_STEP_HZ;
  }
  return hz_to_period_us(frequency);
}

/**
 * @brief Step the motor and advance the mode 5 ramp.
 *
 * Why: Mode 5 needs each point to execute exactly the configured step count
 * while applying its frequency ramp from inside the timer ISR.
 *
 * @param timestamp_cycles DWT timestamp captured at ISR entry; unused.
 * @param period_cycles Measured timer period; unused.
 * @return true while steps remain; false after the final step.
 */
static bool mode5_callback(uint32_t timestamp_cycles,
                           uint32_t period_cycles) {
  (void)timestamp_cycles;
  (void)period_cycles;

  (void)stepper_advance(1);
  s_step_index++;

  if (s_step_index >= s_step_target) {
    s_done = true;
    return false;
  }

  (void)platform_step_timer_set_period_us(
      mode5_period_for_index(s_step_index));
  return true;
}

/**
 * @brief Run a fixed number of steps at one mode 5 frequency.
 *
 * Why: Isolates one sweep point so encoder sampling and loss classification
 * stay outside the timer callback.
 *
 * @param target_hz Test frequency in Hz.
 * @param steps Number of steps to execute for this point.
 * @param ramp_start_hz Ramp start frequency in Hz.
 * @param ramp_steps Number of steps used for the ramp.
 * @return true when all requested steps completed; false on timer start
 *         failure or stall.
 */
static bool run_steps_at_hz(uint32_t target_hz, uint32_t steps,
                            uint32_t ramp_start_hz, uint32_t ramp_steps) {
  s_target_hz = target_hz;
  s_ramp_start_hz = ramp_start_hz;
  s_ramp_steps = ramp_steps;
  s_step_target = steps;
  s_step_index = 0u;
  s_done = false;

  if (!platform_step_timer_start(mode5_period_for_index(0u),
                                 mode5_callback)) {
    return false;
  }

  bool ok = wait_progress(&s_step_index, STALL_TIMEOUT_US);
  stop_output();
  return ok && s_step_index >= steps;
}

bool app_measure_mode5(uint32_t start_hz, uint32_t step_hz, uint32_t max_hz) {
  uint32_t ramp_steps = MODE5_RAMP_STEPS;
  if (ramp_steps >= MODE5_STEPS_PER_POINT / 2u) {
    ramp_steps = MODE5_STEPS_PER_POINT / 2u;
  }

  uint32_t max_stable_hz = 0u;
  uint32_t first_loss_hz = 0u;
  uint32_t failing_points = 0u;
  uint32_t points = 0u;

  platform_log("mode=5\r\n");
  platform_log("steps_per_point=%lu\r\n",
               (unsigned long)MODE5_STEPS_PER_POINT);
  platform_log("tolerance_steps=%lu\r\n",
               (unsigned long)MODE5_TOLERANCE_STEPS);
  platform_log("ramp_start_hz=%lu\r\n", (unsigned long)start_hz);
  platform_log("ramp_steps=%lu\r\n", (unsigned long)ramp_steps);
  platform_log("encoder_counts_per_rev=%lu\r\n",
               (unsigned long)ENCODER_COUNTS_PER_REV);
  platform_log("steps_per_rev=%lu\r\n", (unsigned long)APP_STEPS_PER_REV);
  platform_log("clock_hz=%lu\r\n", (unsigned long)platform_dwt_hz());

  for (uint32_t hz = start_hz; hz <= max_hz; hz += step_hz) {
    encoder_sample_t before;
    encoder_sample_t after;

    if (!encoder_read(&before, 50u)) {
      platform_log("error=i2c phase=before freq_hz=%lu\r\n",
                   (unsigned long)hz);
      return false;
    }

    if (!run_steps_at_hz(hz, MODE5_STEPS_PER_POINT, start_hz, ramp_steps)) {
      platform_log("error=timeout freq_hz=%lu\r\n", (unsigned long)hz);
      return false;
    }

    platform_delay_us(5000u);

    if (!encoder_read(&after, 50u)) {
      platform_log("error=i2c phase=after freq_hz=%lu\r\n",
                   (unsigned long)hz);
      return false;
    }

    int32_t delta = (int32_t)after.raw - (int32_t)before.raw;
    if (delta > (int32_t)(ENCODER_COUNTS_PER_REV / 2u)) {
      delta -= (int32_t)ENCODER_COUNTS_PER_REV;
    } else if (delta < -(int32_t)(ENCODER_COUNTS_PER_REV / 2u)) {
      delta += (int32_t)ENCODER_COUNTS_PER_REV;
    }
    uint32_t abs_delta =
        delta < 0 ? (uint32_t)(-delta) : (uint32_t)delta;
    uint32_t measured_steps = (abs_delta + 1u) / 2u;
    uint32_t diff = measured_steps > MODE5_STEPS_PER_POINT
                        ? measured_steps - MODE5_STEPS_PER_POINT
                        : MODE5_STEPS_PER_POINT - measured_steps;
    bool loss = diff > MODE5_TOLERANCE_STEPS;

    platform_log("point=%lu freq_hz=%lu expected_steps=%lu "
                 "measured_steps=%lu raw_before=%u raw_after=%u loss=%u\r\n",
                 (unsigned long)points, (unsigned long)hz,
                 (unsigned long)MODE5_STEPS_PER_POINT,
                 (unsigned long)measured_steps, (unsigned)before.raw,
                 (unsigned)after.raw, loss ? 1u : 0u);

    if (loss) {
      failing_points++;
      if (first_loss_hz == 0u) {
        first_loss_hz = hz;
      }
    } else if (hz > max_stable_hz) {
      max_stable_hz = hz;
    }

    points++;
    if (step_hz > max_hz - hz) {
      break;
    }
  }

  platform_log("points=%lu\r\n", (unsigned long)points);
  platform_log("max_stable_hz=%lu\r\n", (unsigned long)max_stable_hz);
  platform_log("first_loss_hz=%lu\r\n", (unsigned long)first_loss_hz);
  platform_log("failing_points=%lu\r\n", (unsigned long)failing_points);
  return true;
}

void app_measure_init(void) {
  const stepper_config_t cfg = {
      .steps_per_rev = APP_STEPS_PER_REV,
      .step_interval_us = 1000u,
      .coil_sequence = 0,
      .sequence_len = 0,
  };
  stepper_init(&cfg);
  stepper_deenergize();
}
