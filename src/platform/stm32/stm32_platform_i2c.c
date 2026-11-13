#include <platform/i2c.h>

#if __has_include("main.h")
#define PLATFORM_HAVE_HAL 1
#include "main.h"
#include "dma.h"
#include "i2c.h"
#else
#define PLATFORM_HAVE_HAL 0
#endif

#if PLATFORM_HAVE_HAL
static volatile bool s_busy;
static volatile bool s_done;
static volatile bool s_error;
static volatile uint32_t s_error_count;
static volatile uint32_t s_last_status;
static volatile uint32_t s_last_error;
static volatile uint32_t s_last_state;
static uint8_t s_addr;

static void platform_i2c_recover(void) {
  HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
  HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);
  (void)HAL_I2C_DeInit(&hi2c1);
  MX_I2C1_Init();
  HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
  HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
}
#endif

void platform_i2c_init(void) {
#if PLATFORM_HAVE_HAL
  MX_DMA_Init();
  MX_I2C1_Init();

  HAL_NVIC_SetPriority(I2C1_EV_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
  HAL_NVIC_SetPriority(I2C1_ER_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
#endif
}

bool platform_i2c_read_reg_start(uint8_t dev_addr, uint8_t reg,
                                 uint8_t *data, uint16_t len) {
#if PLATFORM_HAVE_HAL
  if (data == 0 || len == 0u || s_busy) {
    return false;
  }

  s_addr = dev_addr;
  s_done = false;
  s_error = false;
  s_busy = true;

  HAL_StatusTypeDef status =
      HAL_I2C_Mem_Read_DMA(&hi2c1, (uint16_t)(dev_addr << 1), reg,
                           I2C_MEMADD_SIZE_8BIT, data, len);
  if (status != HAL_OK) {
    s_last_status = (uint32_t)status;
    s_last_error = hi2c1.ErrorCode;
    s_last_state = (uint32_t)HAL_I2C_GetState(&hi2c1);
    s_busy = false;
    s_error = true;
    s_error_count++;
    return false;
  }
  return true;
#else
  (void)dev_addr;
  (void)reg;
  (void)data;
  (void)len;
  return false;
#endif
}

bool platform_i2c_read_reg_wait(uint32_t timeout_ms) {
#if PLATFORM_HAVE_HAL
  uint32_t start = HAL_GetTick();

  while (s_busy && !s_done && !s_error) {
    if ((HAL_GetTick() - start) > timeout_ms) {
      s_last_status = 0xFFFFFFFFu;
      s_last_error = hi2c1.ErrorCode;
      s_last_state = (uint32_t)HAL_I2C_GetState(&hi2c1);
      s_busy = false;
      s_error = true;
      s_error_count++;
      platform_i2c_recover();
      return false;
    }
  }
  return s_done && !s_error;
#else
  (void)timeout_ms;
  return false;
#endif
}

bool platform_i2c_read_reg(uint8_t dev_addr, uint8_t reg,
                           uint8_t *data, uint16_t len, uint32_t timeout_ms) {
  if (!platform_i2c_read_reg_start(dev_addr, reg, data, len)) {
    return false;
  }
  return platform_i2c_read_reg_wait(timeout_ms);
}

bool platform_i2c_read_reg_blocking(uint8_t dev_addr, uint8_t reg,
                                    uint8_t *data, uint16_t len,
                                    uint32_t timeout_ms) {
#if PLATFORM_HAVE_HAL
  if (s_busy) {
    return false;
  }

  HAL_StatusTypeDef status =
      HAL_I2C_Mem_Read(&hi2c1, (uint16_t)(dev_addr << 1), reg,
                       I2C_MEMADD_SIZE_8BIT, data, len, timeout_ms);
  s_last_status = (uint32_t)status;
  s_last_error = hi2c1.ErrorCode;
  s_last_state = (uint32_t)HAL_I2C_GetState(&hi2c1);
  return status == HAL_OK;
#else
  (void)dev_addr;
  (void)reg;
  (void)data;
  (void)len;
  (void)timeout_ms;
  return false;
#endif
}

bool platform_i2c_probe(uint8_t dev_addr, uint32_t trials, uint32_t timeout_ms) {
#if PLATFORM_HAVE_HAL
  if (s_busy) {
    return false;
  }
  return HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(dev_addr << 1),
                               trials, timeout_ms) == HAL_OK;
#else
  (void)dev_addr;
  (void)trials;
  (void)timeout_ms;
  return false;
#endif
}

bool platform_i2c_busy(void) {
#if PLATFORM_HAVE_HAL
  return s_busy;
#else
  return false;
#endif
}

bool platform_i2c_ok(void) {
#if PLATFORM_HAVE_HAL
  return s_done && !s_error;
#else
  return false;
#endif
}

uint32_t platform_i2c_error_count(void) {
#if PLATFORM_HAVE_HAL
  return s_error_count;
#else
  return 0u;
#endif
}

uint32_t platform_i2c_last_status(void) {
#if PLATFORM_HAVE_HAL
  return s_last_status;
#else
  return 0u;
#endif
}

uint32_t platform_i2c_last_error(void) {
#if PLATFORM_HAVE_HAL
  return s_last_error;
#else
  return 0u;
#endif
}

uint32_t platform_i2c_last_state(void) {
#if PLATFORM_HAVE_HAL
  return s_last_state;
#else
  return 0u;
#endif
}

#if PLATFORM_HAVE_HAL
void I2C1_EV_IRQHandler(void) {
  HAL_I2C_EV_IRQHandler(&hi2c1);
}

void I2C1_ER_IRQHandler(void) {
  HAL_I2C_ER_IRQHandler(&hi2c1);
}

void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c) {
  if (hi2c->Instance == I2C1) {
    s_done = true;
    s_busy = false;
  }
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
  if (hi2c->Instance == I2C1) {
    s_last_error = hi2c->ErrorCode;
    s_last_state = (uint32_t)HAL_I2C_GetState(hi2c);
    s_error = true;
    s_busy = false;
    s_error_count++;
  }
}
#endif
