#include <app/control.h>

#include <app/measure.h>
#include <app/status.h>
#include <app/version.h>
#include <drivers/encoder/encoder.h>
#include <platform/cpu.h>
#include <platform/dwt.h>
#include <platform/i2c.h>
#include <platform/log.h>
#include <platform/stepper.h>
#include <platform/timer.h>
#include <platform/uart.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef APP_BOARD
#define APP_BOARD "unknown"
#endif

#ifndef APP_MCU
#define APP_MCU "unknown"
#endif

#define DEFAULT_MODE1_HZ 1000u
#define DEFAULT_MODE1_DURATION_MS 1000u
#define DEFAULT_MODE2_HZ 1000u
#define DEFAULT_MODE2_SAMPLES 10000u
#define DEFAULT_MODE3_HZ 1000u
#define DEFAULT_MODE3_SAMPLES 1000u
#define DEFAULT_MODE4_TRIALS 100u
#define DEFAULT_MODE5_START_HZ 100u
#define DEFAULT_MODE5_STEP_HZ 200u
#define DEFAULT_MODE5_MAX_HZ 5000u

static bool s_ready;
static bool s_busy;
static bool s_dwt_ok;
static bool s_timer_ok;
static bool s_uart_ok;
static bool s_encoder_ok;

/**
 * @brief Split the next whitespace-delimited token from a command line.
 *
 * Why: The command parser needs a small allocation-free tokenizer that can
 * process optional numeric arguments.
 *
 * @param cursor Pointer to the current parse position; advanced past the token.
 * @return Pointer to the null-terminated token, or null at end of line.
 */
static char *next_token(char **cursor) {
  while (**cursor == ' ' || **cursor == '\t') {
    (*cursor)++;
  }
  if (**cursor == '\0') {
    return 0;
  }

  char *token = *cursor;
  while (**cursor != '\0' && **cursor != ' ' && **cursor != '\t') {
    (*cursor)++;
  }
  if (**cursor != '\0') {
    **cursor = '\0';
    (*cursor)++;
  }
  return token;
}

/**
 * @brief Parse a complete unsigned decimal integer.
 *
 * Why: Command parameters must be validated without accepting signs, overflow,
 * or trailing characters.
 *
 * @param text Null-terminated text to parse.
 * @param value Destination for the parsed value.
 * @return true on a valid 32-bit decimal value; otherwise false.
 */
static bool parse_u32(const char *text, uint32_t *value) {
  if (text == 0 || *text == '\0') {
    return false;
  }

  uint32_t result = 0u;
  while (*text != '\0') {
    if (*text < '0' || *text > '9') {
      return false;
    }
    uint32_t digit = (uint32_t)(*text - '0');
    if (result > (0xFFFFFFFFu - digit) / 10u) {
      return false;
    }
    result = result * 10u + digit;
    text++;
  }
  *value = result;
  return true;
}

/**
 * @brief Parse an optional unsigned integer argument.
 *
 * Why: Each mode accepts trailing parameters in order while keeping the
 * firmware defaults for omitted values.
 *
 * @param cursor Pointer to the current parse position.
 * @param value Destination for the parsed value when present.
 * @param present Set to true when a token was consumed and parsed.
 * @return true when the token is absent or valid; false on invalid syntax.
 */
static bool parse_optional_u32(char **cursor, uint32_t *value,
                               bool *present) {
  char *token = next_token(cursor);
  if (token == 0) {
    *present = false;
    return true;
  }
  if (!parse_u32(token, value)) {
    return false;
  }
  *present = true;
  return true;
}

/**
 * @brief Emit a protocol error response.
 *
 * Why: Gives every parser and command failure a consistent `ERR` prefix for the
 * host scripts.
 *
 * @param error Error name to report.
 * @return None.
 */
static void log_error(const char *error) {
  platform_log("ERR %s\r\n", error);
}

/**
 * @brief Mark the start of a measurement and announce it.
 *
 * Why: Sets the busy flag and active LED state before work begins so new
 * commands are rejected consistently.
 *
 * @param mode Measurement mode number.
 * @return None.
 */
static void begin_measurement(uint32_t mode) {
  s_busy = true;
  app_status_set(APP_STATUS_ACTIVE);
  platform_log("BEGIN %lu\r\n", (unsigned long)mode);
}

/**
 * @brief Mark the end of a measurement and announce its result.
 *
 * Why: Clears the busy flag and restores the idle LED state even when the
 * measurement failed.
 *
 * @param mode Measurement mode number.
 * @param ok true for success, false for failure.
 * @return None.
 */
static void end_measurement(uint32_t mode, bool ok) {
  platform_log("END %lu %s\r\n", (unsigned long)mode, ok ? "OK" : "ERR");
  s_busy = false;
  app_status_set(APP_STATUS_OK);
}

/**
 * @brief Check a requested step frequency against firmware limits.
 *
 * Why: Rejects frequencies that the timer and motor control cannot represent
 * before a measurement starts.
 *
 * @param hz Frequency to validate in Hz.
 * @return true when the frequency is within APP_MIN_STEP_HZ..APP_MAX_STEP_HZ.
 */
static bool valid_step_hz(uint32_t hz) {
  return hz >= APP_MIN_STEP_HZ && hz <= APP_MAX_STEP_HZ;
}

/**
 * @brief Report board identity, self-test state, and encoder diagnostics.
 *
 * Why: Mode 0 is the quick health check that records which hardware produced a
 * data set and whether the peripherals are usable.
 *
 * @return None.
 */
static void run_mode0(void) {
  begin_measurement(0u);

  platform_log("board=%s\r\n", APP_BOARD);
  platform_log("mcu=%s\r\n", APP_MCU);
  platform_log("firmware=%s\r\n", APP_FIRMWARE_VERSION);
  platform_log("clock_hz=%lu\r\n", (unsigned long)platform_dwt_hz());
  platform_log("dwt=%s\r\n", s_dwt_ok ? "ok" : "fail");
  platform_log("timer=%s\r\n", s_timer_ok ? "ok" : "fail");
  platform_log("uart=%s\r\n", s_uart_ok ? "ok" : "fail");
  platform_log("uart_baud=%lu\r\n", (unsigned long)platform_uart_baud());
  platform_log("uart_loopback=%s\r\n",
               platform_uart_loopback_verified() ? "verified" : "software");
  platform_log("uart_rx_errors=%lu\r\n",
               (unsigned long)platform_uart_rx_error_count());
  platform_log("i2c=%s\r\n", s_encoder_ok ? "ok" : "fail");
  platform_log("encoder_addr=0x%02X\r\n", (unsigned)ENCODER_I2C_ADDRESS);
  platform_log("encoder_counts_per_rev=%lu\r\n",
               (unsigned long)ENCODER_COUNTS_PER_REV);
  platform_log("steps_per_rev=%lu\r\n", (unsigned long)APP_STEPS_PER_REV);

  static const uint32_t diag_hz[] = {100u, 333u, 500u, 777u, 1000u,
                                     1234u, 2000u, 2345u, 4000u};
  for (uint32_t i = 0u; i < sizeof(diag_hz) / sizeof(diag_hz[0]); i++) {
    uint32_t hz = diag_hz[i];
    uint32_t period_us = 1000000u / hz;
    if (period_us == 0u) {
      period_us = 1u;
    }
    if (period_us > 65535u) {
      period_us = 65535u;
    }
    (void)platform_step_timer_set_period_us(period_us);
    uint32_t psc = platform_step_timer_get_prescaler();
    uint32_t arr = platform_step_timer_get_autoreload();
    uint32_t actual_period_us = arr + 1u;
    uint64_t actual_micro_hz = 1000000000000ULL / actual_period_us;
    uint64_t requested_micro_hz = (uint64_t)hz * 1000000ULL;
    int64_t error_ppm =
        ((int64_t)actual_micro_hz - (int64_t)requested_micro_hz) * 1000000LL /
        (int64_t)requested_micro_hz;
    uint32_t actual_milli_hz = (uint32_t)(actual_micro_hz / 1000ULL);
    platform_log("timing_hz=%lu\r\n", (unsigned long)hz);
    platform_log("timing_psc=%lu\r\n", (unsigned long)psc);
    platform_log("timing_arr=%lu\r\n", (unsigned long)arr);
    platform_log("timing_period_us=%lu\r\n",
                 (unsigned long)actual_period_us);
    platform_log("timing_actual_hz=%lu.%03lu\r\n",
                 (unsigned long)(actual_milli_hz / 1000u),
                 (unsigned long)(actual_milli_hz % 1000u));
    platform_log("timing_error_ppm=%ld\r\n", (long)error_ppm);
  }

  uint8_t diag[3] = {0u, 0u, 0u};
  if (platform_i2c_read_reg_blocking(ENCODER_I2C_ADDRESS, ENCODER_STATUS_REG,
                                     diag, sizeof(diag), 20u)) {
    uint16_t raw = (uint16_t)(((uint16_t)(diag[1] & 0x0Fu) << 8) | diag[2]);
    platform_log("encoder_status=0x%02X\r\n", (unsigned)diag[0]);
    platform_log("encoder_raw=%u\r\n", (unsigned)raw);
  } else {
    platform_log("encoder_read=fail\r\n");
  }

  end_measurement(0u, true);
}

/**
 * @brief Parse and run mode 1.
 *
 * Why: Validates optional frequency and duration arguments before starting a
 * timed frequency measurement.
 *
 * @param cursor Parser position immediately after the mode name.
 * @return None.
 */
static void handle_mode1(char **cursor) {
  uint32_t hz = DEFAULT_MODE1_HZ;
  uint32_t duration_ms = DEFAULT_MODE1_DURATION_MS;
  bool present;

  if (!parse_optional_u32(cursor, &hz, &present)) {
    log_error("PARAM");
    return;
  }
  if (present && !parse_optional_u32(cursor, &duration_ms, &present)) {
    log_error("PARAM");
    return;
  }
  if (next_token(cursor) != 0) {
    log_error("PARAM");
    return;
  }
  if (!valid_step_hz(hz) || duration_ms == 0u ||
      duration_ms > APP_MAX_DURATION_MS) {
    log_error("PARAM");
    return;
  }

  begin_measurement(1u);
  end_measurement(1u, app_measure_mode1(hz, duration_ms));
}

/**
 * @brief Parse and run mode 2.
 *
 * Why: Validates optional frequency and sample-count arguments before starting
 * interval-jitter collection.
 *
 * @param cursor Parser position immediately after the mode name.
 * @return None.
 */
static void handle_mode2(char **cursor) {
  uint32_t hz = DEFAULT_MODE2_HZ;
  uint32_t samples = DEFAULT_MODE2_SAMPLES;
  bool raw_dump = false;
  bool present;

  if (!parse_optional_u32(cursor, &hz, &present)) {
    log_error("PARAM");
    return;
  }
  if (present && !parse_optional_u32(cursor, &samples, &present)) {
    log_error("PARAM");
    return;
  }
  char *token = next_token(cursor);
  if (token != 0) {
    if (strcmp(token, "raw=1") == 0) {
      raw_dump = true;
    } else {
      log_error("PARAM");
      return;
    }
  }
  if (next_token(cursor) != 0) {
    log_error("PARAM");
    return;
  }
  if (!valid_step_hz(hz) || samples < 2u || samples > APP_MAX_SAMPLES) {
    log_error("PARAM");
    return;
  }
  if (raw_dump && samples > APP_MAX_RAW_SAMPLES) {
    log_error("PARAM");
    return;
  }

  begin_measurement(2u);
  end_measurement(2u, app_measure_mode2(hz, samples, raw_dump));
}

/**
 * @brief Parse and run mode 3.
 *
 * Why: Validates optional frequency and sample-count arguments before starting
 * the ISR-load comparison.
 *
 * @param cursor Parser position immediately after the mode name.
 * @return None.
 */
static void handle_mode3(char **cursor) {
  uint32_t hz = DEFAULT_MODE3_HZ;
  uint32_t samples = DEFAULT_MODE3_SAMPLES;
  bool present;

  if (!parse_optional_u32(cursor, &hz, &present)) {
    log_error("PARAM");
    return;
  }
  if (present && !parse_optional_u32(cursor, &samples, &present)) {
    log_error("PARAM");
    return;
  }
  if (next_token(cursor) != 0) {
    log_error("PARAM");
    return;
  }
  if ((hz != 0u && !valid_step_hz(hz)) || samples == 0u) {
    log_error("PARAM");
    return;
  }
  if (hz == 0u && samples > APP_MAX_DURATION_MS) {
    log_error("PARAM");
    return;
  }
  if (hz != 0u && samples > 10000u) {
    log_error("PARAM");
    return;
  }

  begin_measurement(3u);
  end_measurement(3u, app_measure_mode3(hz, samples));
}

/**
 * @brief Parse and run mode 4.
 *
 * Why: Validates the optional trial count and forwards command metadata needed
 * for latency reporting.
 *
 * @param cursor Parser position immediately after the mode name.
 * @param cmd_len Length of the received command line in bytes.
 * @return None.
 */
static void handle_mode4(char **cursor, uint32_t cmd_len) {
  uint32_t trials = DEFAULT_MODE4_TRIALS;
  bool present;

  if (!parse_optional_u32(cursor, &trials, &present)) {
    log_error("PARAM");
    return;
  }
  if (next_token(cursor) != 0) {
    log_error("PARAM");
    return;
  }
  if (trials == 0u || trials > APP_MAX_TRIALS) {
    log_error("PARAM");
    return;
  }

  begin_measurement(4u);
  end_measurement(4u, app_measure_mode4(trials, cmd_len));
}

/**
 * @brief Parse and run mode 5.
 *
 * Why: Validates the frequency sweep parameters and point-count limit before
 * starting the encoder-verified step-rate test.
 *
 * @param cursor Parser position immediately after the mode name.
 * @return None.
 */
static void handle_mode5(char **cursor) {
  uint32_t start_hz = DEFAULT_MODE5_START_HZ;
  uint32_t step_hz = DEFAULT_MODE5_STEP_HZ;
  uint32_t max_hz = DEFAULT_MODE5_MAX_HZ;
  bool present;

  if (!parse_optional_u32(cursor, &start_hz, &present)) {
    log_error("PARAM");
    return;
  }
  if (present && !parse_optional_u32(cursor, &step_hz, &present)) {
    log_error("PARAM");
    return;
  }
  if (present && !parse_optional_u32(cursor, &max_hz, &present)) {
    log_error("PARAM");
    return;
  }
  if (next_token(cursor) != 0) {
    log_error("PARAM");
    return;
  }

  if (max_hz > APP_MAX_STEP_HZ) {
    log_error("LIMIT");
    return;
  }
  if (start_hz < APP_MIN_STEP_HZ || start_hz > max_hz || step_hz == 0u) {
    log_error("PARAM");
    return;
  }
  if ((max_hz - start_hz) / step_hz + 1u > APP_MAX_MODE5_POINTS) {
    log_error("LIMIT");
    return;
  }

  begin_measurement(5u);
  end_measurement(5u, app_measure_mode5(start_hz, step_hz, max_hz));
}

/**
 * @brief Dispatch one complete command line.
 *
 * Why: Centralizes command-name matching and ensures unknown commands and
 * extra arguments are reported consistently.
 *
 * @param line Null-terminated command line to parse; modified in place.
 * @return None.
 */
static void handle_command(char *line) {
  uint32_t cmd_len = (uint32_t)strlen(line);
  char *cursor = line;
  char *command = next_token(&cursor);
  if (command == 0) {
    return;
  }

  if (strcmp(command, "mode0") == 0) {
    if (next_token(&cursor) != 0) {
      log_error("PARAM");
      return;
    }
    run_mode0();
  } else if (strcmp(command, "mode1") == 0) {
    handle_mode1(&cursor);
  } else if (strcmp(command, "mode2") == 0) {
    handle_mode2(&cursor);
  } else if (strcmp(command, "mode3") == 0) {
    handle_mode3(&cursor);
  } else if (strcmp(command, "mode4") == 0) {
    handle_mode4(&cursor, cmd_len);
  } else if (strcmp(command, "mode5") == 0) {
    handle_mode5(&cursor);
  } else {
    log_error("UNKNOWN_CMD");
  }
}

void app_control_init(void) {
  app_measure_init();
  app_status_init();
  encoder_init();
  platform_led_set(false);

  s_dwt_ok = platform_dwt_selftest();
  s_timer_ok = s_dwt_ok && platform_step_timer_selftest();
  s_uart_ok = platform_uart_selftest();

  encoder_sample_t sample;
  s_encoder_ok = false;
  if (platform_i2c_probe(ENCODER_I2C_ADDRESS, 3u, 20u) &&
      encoder_read(&sample, 50u)) {
    s_encoder_ok = sample.valid;
  }

  s_ready = s_dwt_ok && s_timer_ok && s_uart_ok && s_encoder_ok;
  if (s_ready) {
    app_status_set(APP_STATUS_OK);
    platform_log("READY\r\n");
  } else {
    app_status_set(APP_STATUS_FAULT);
    platform_log("ERR SELFTEST dwt=%u timer=%u uart=%u encoder=%u\r\n",
                 s_dwt_ok ? 1u : 0u, s_timer_ok ? 1u : 0u,
                 s_uart_ok ? 1u : 0u, s_encoder_ok ? 1u : 0u);
  }
}

void app_control_loop(void) {
  app_status_service();

  if (!s_ready || s_busy) {
    return;
  }

  char line[96];
  if (platform_uart_read_line(line, sizeof(line), 0)) {
    handle_command(line);
    return;
  }

  platform_cpu_wait_for_event();
}
