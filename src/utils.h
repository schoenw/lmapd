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

#ifndef LMAP_UTILS_H
#define LMAP_UTILS_H

#include <syslog.h>
#include <stdarg.h>

/*
 * The following macros are the most frequently used interface to the
 * logging API of this LMAP implementation.
 */

#define lmap_err(...) \
    lmap_log(LOG_ERR, __FUNCTION__, __VA_ARGS__)

#define lmap_wrn(...) \
    lmap_log(LOG_WARNING, __FUNCTION__, __VA_ARGS__)

#define lmap_dbg(...) \
    lmap_log(LOG_DEBUG, __FUNCTION__, __VA_ARGS__)

/*
 * The following functions define the low-level logging interface.
 */

extern void lmap_log(int level, const char *func,
		     const char *format, ...)
#ifdef __GNUC__
    __attribute__ ((format (printf, 3, 4)))
#endif
    ;
extern void lmap_vlog(int level, const char *func,
		      const char *format, va_list ap);

/*
 * The following definitions allow applications to provide their own
 * logging handlers.
 */

typedef void (lmap_log_handler) (int level, const char *func,
				 const char *format, va_list ap);

extern void lmap_set_log_handler(lmap_log_handler handler);

extern void lmap_vlog_default(int level, const char *func,
			      const char *format, va_list ap);

#endif
