#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void platform_i2c_init(void);

bool platform_i2c_read_reg_start(uint8_t dev_addr, uint8_t reg,
                                 uint8_t *data, uint16_t len);
bool platform_i2c_read_reg_wait(uint32_t timeout_ms);
bool platform_i2c_read_reg(uint8_t dev_addr, uint8_t reg,
                           uint8_t *data, uint16_t len, uint32_t timeout_ms);
bool platform_i2c_read_reg_blocking(uint8_t dev_addr, uint8_t reg,
                                    uint8_t *data, uint16_t len,
                                    uint32_t timeout_ms);
bool platform_i2c_probe(uint8_t dev_addr, uint32_t trials, uint32_t timeout_ms);

bool platform_i2c_busy(void);
bool platform_i2c_ok(void);
uint32_t platform_i2c_error_count(void);
uint32_t platform_i2c_last_status(void);
uint32_t platform_i2c_last_error(void);
uint32_t platform_i2c_last_state(void);

#ifdef __cplusplus
}
#endif
