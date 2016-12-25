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

#ifndef LMAP_JSON_IO_H
#define LMAP_JSON_IO_H

#include "lmap.h"

#define LMAPC_JSON_NAMESPACE "ietf-lmap-control"

#define LMAPR_JSON_NAMESPACE "ietf-lmap-report"

extern int lmap_json_parse_config_path(struct lmap *lmap, const char *path);
extern int lmap_json_parse_config_file(struct lmap *lmap, const char *file);
extern int lmap_json_parse_config_string(struct lmap *lmap, const char *string);

extern int lmap_json_parse_state_file(struct lmap *lmap, const char *file);
extern int lmap_json_parse_state_string(struct lmap *lmap, const char *string);

extern int lmap_json_parse_report_file(struct lmap *lmap, const char *file);
extern int lmap_json_parse_report_string(struct lmap *lmap, const char *string);

extern char * lmap_json_render_config(struct lmap *lmap);
extern char * lmap_json_render_state(struct lmap *lmap);
extern char * lmap_json_render_report(struct lmap *lmap);

#endif
