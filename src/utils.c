/*
 * This file is part of lmapd.
 *
 * lmapd is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * lmapd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with lmapd. If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>

#include "lmap.h"
#include "utils.h"
#include "parser.h"
#include "runner.h"

static lmap_log_handler *log_handler = lmap_vlog_default;

void lmap_vlog(int level, const char *func, const char *format, va_list args)
{
    if (log_handler) {
	log_handler(level, func, format, args);
    }
}

void lmap_set_log_handler(lmap_log_handler handler)
{
    log_handler = handler;
}

/**
 * @brief Default logger with varargs arguments
 *
 * Function to write a log message using printf style format. The log
 * message is written to standard error if standard error is
 * associated with a tty. Otherwise, the log message is sent to
 * syslog.
 *
 * @param level level of log message
 * @param func name of the function generating the log message
 * @param format printf style of format for the log message
 * @param args arguments according to the format string
 */

void lmap_vlog_default(int level, const char *func, const char *format, va_list args)
{
    char *level_name = NULL;
    
    if (isatty(STDERR_FILENO)) {
	fprintf(stderr, "lmapd[%d]: ", getpid());
	switch (level) {
	case LOG_ERR:
	    level_name = "ERR";
	    break;
	case LOG_WARNING:
	    level_name = "WRN";
	    break;
	case LOG_DEBUG:
	    level_name = "DBG";
	    break;
	}
	if (level_name) {
	    fprintf(stderr, "[%s] ", level_name);
	} else {
	    fprintf(stderr, "[%d] ", level);
	}
	if (func) {
	    fprintf(stderr, "%s: ", func);
	}
	vfprintf(stderr, format, args);
	fputs("\n", stderr);
    } else {
	vsyslog(level, format, args);
    }
}

/**
 * @brief Custom logger with variable arguments
 *
 * Function to write a log message using printf style format. The real
 * work is done by lmap_vlog().
 *
 * @param level level of log message
 * @param func name of the function generating the log message
 * @param format printf style of format for the log message
 */

void lmap_log(int level, const char *func, const char *format, ...)
{
    va_list args;
    
    va_start(args, format);
    lmap_vlog(level, func, format, args);
    va_end(args);
}
