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

#ifndef LMAP_PIDFILE_H
#define LMAP_PIDFILE_H

#include <sys/types.h>

#include "lmap.h"
#include "lmapd.h"

pid_t lmapd_pid_read(struct lmapd *lmapd);
int   lmapd_pid_check (struct lmapd *lmapd);
int   lmapd_pid_write(struct lmapd *lmapd);
int   lmapd_pid_remove(struct lmapd *lmapd);

#endif
