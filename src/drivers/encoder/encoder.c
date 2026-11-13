#include <drivers/encoder/encoder.h>
#include <platform/i2c.h>

#define ENCODER_READ_LEN 3u
#define ENCODER_STATUS_MD (1u << 5)
#define ENCODER_STATUS_ML (1u << 4)
#define ENCODER_STATUS_MH (1u << 3)

static uint8_t s_addr = ENCODER_I2C_ADDRESS;
static uint8_t s_buf[ENCODER_READ_LEN];
static bool s_started;
static uint32_t s_errors;

static bool encoder_parse(encoder_sample_t *sample) {
  if (sample == 0) {
    return false;
  }

  uint8_t status = s_buf[0];
  uint16_t raw = ((uint16_t)(s_buf[1] & 0x0Fu) << 8) | s_buf[2];

  sample->raw = raw;
  sample->millidegrees =
      ((uint32_t)raw * 360000u) / ENCODER_COUNTS_PER_REV;
  sample->magnet_detected = (status & ENCODER_STATUS_MD) != 0u;
  sample->magnet_too_weak = (status & ENCODER_STATUS_ML) != 0u;
  sample->magnet_too_strong = (status & ENCODER_STATUS_MH) != 0u;
  sample->valid = sample->magnet_detected &&
                  !sample->magnet_too_weak &&
                  !sample->magnet_too_strong;
  return true;
}

void encoder_init(void) {
  s_started = false;
  s_errors = 0u;
}

void encoder_set_address(uint8_t addr) {
  s_addr = addr;
}

bool encoder_start_read(void) {
  if (s_started) {
    return false;
  }

  s_started = platform_i2c_read_reg_start(s_addr, ENCODER_STATUS_REG,
                                          s_buf, ENCODER_READ_LEN);
  if (!s_started) {
    s_errors++;
  }
  return s_started;
}

bool encoder_poll(encoder_sample_t *sample) {
  if (!s_started || platform_i2c_busy()) {
    return false;
  }

  s_started = false;
  if (!platform_i2c_ok()) {
    s_errors++;
    return false;
  }
  return encoder_parse(sample);
}

bool encoder_read(encoder_sample_t *sample, uint32_t timeout_ms) {
  if (!encoder_start_read()) {
    return false;
  }
  if (!platform_i2c_read_reg_wait(timeout_ms)) {
    s_started = false;
    s_errors++;
    return false;
  }
  return encoder_poll(sample);
}

bool encoder_busy(void) {
  return s_started && platform_i2c_busy();
}

uint32_t encoder_error_count(void) {
  return s_errors;
}
