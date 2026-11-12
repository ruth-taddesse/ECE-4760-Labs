#ifndef TEST_PICO_STDLIB
#define TEST_PICO_STDLIB
#include <stdint.h>
#include <stdbool.h>
#define PICO_ERROR_TIMEOUT (-1)
uint64_t time_us_64(void);
void stdio_init_all(void);
void stdio_flush(void);
void sleep_us(uint64_t us);
void tight_loop_contents(void);
int getchar_timeout_us(uint32_t us);
#endif
