#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void platform_log_init(void);
void platform_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
