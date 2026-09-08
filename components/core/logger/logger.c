#include "logger.h"

static log_level_t s_level = LOGGER_DEFAULT_LEVEL;

void logger_set_level(log_level_t level) {
    if (level < LOG_LEVEL_DEBUG) level = LOG_LEVEL_DEBUG;
    if (level > LOG_LEVEL_NONE) level = LOG_LEVEL_NONE;
    s_level = level;
}

log_level_t logger_get_level(void) {
    return s_level;
}

int logger_level_enabled(log_level_t level) {
    return (int)level >= (int)s_level;
}
