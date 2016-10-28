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

#ifndef LMAP_SIGNALS_H
#define LMAP_SIGNALS_H

#include "lmap.h"
#include "lmapd.h"

extern void lmapd_sigint_cb(evutil_socket_t sig, short events, void *context);
extern void lmapd_sigterm_cb(evutil_socket_t sig, short events, void *context);
extern void lmapd_sighub_cb(evutil_socket_t sig, short events, void *context);
extern void lmapd_sigchld_cb(evutil_socket_t sig, short events, void *context);
extern void lmapd_sigusr1_cb(evutil_socket_t sig, short events, void *context);
extern void lmapd_sigusr2_cb(evutil_socket_t sig, short events, void *context);

#endif
