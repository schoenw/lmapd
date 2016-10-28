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

#ifndef WORKSPACE_H
#define WORKSPACE_H

#include "lmap.h"
#include "lmapd.h"

extern int lmapd_workspace_init(struct lmapd *lmapd);
extern int lmapd_workspace_clean(struct lmapd *lmapd);
extern int lmapd_workspace_update(struct lmapd *lmapd);

extern int lmapd_workspace_action_clean(struct lmapd *lmapd, struct action *action);
extern int lmapd_workspace_action_move(struct lmapd *lmapd, struct schedule *schedule, struct action *action, struct schedule *destination);

extern int lmapd_workspace_action_open_data(struct schedule *schedule, struct action *action, int flags);

extern int lmapd_workspace_action_open_meta(struct schedule *schedule, struct action *action, int flags);

extern int lmapd_workspace_action_meta_add_start(struct schedule *schedule, struct action *action, struct task *task);
extern int lmapd_workspace_action_meta_add_end(struct schedule *schedule, struct action *action);

extern int lmapd_workspace_read_results(struct lmapd *lmapd);

#endif
