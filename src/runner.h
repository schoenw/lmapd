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

#ifndef RUNNER_H
#define RUNNER_H

#include "lmap.h"
#include "lmapd.h"

extern int lmapd_run(struct lmapd *lmapd);
extern void lmapd_stop(struct lmapd *lmapd);
extern void lmapd_restart(struct lmapd *lmapd);

extern void lmapd_cleanup(struct lmapd *lmapd);

#endif
