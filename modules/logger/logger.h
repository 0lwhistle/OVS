#ifndef LOGGER_H
#define LOGGER_H
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define LOGI(tag, fmt, ...) printf("\033[32m%s: " fmt "\033[0m\n", (tag), ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) printf("\033[33m%s: " fmt "\033[0m\n", (tag), ##__VA_ARGS__)
#define LOGE(tag, fmt, ...) printf("\033[31m%s: " fmt "\033[0m\n", (tag), ##__VA_ARGS__)


#endif // __LOGGER_H__