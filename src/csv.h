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

#ifndef LMAP_CSV_H
#define LMAP_CSV_H

#include <stdio.h>

extern void csv_start(FILE *file, char delimiter, const char *field);
extern void csv_append(FILE *file, char delimiter, const char *field);
extern void csv_end(FILE *file);
extern char* csv_next(FILE *file, char delimiter);

extern void csv_append_key_value(FILE *file, char delimiter,
				 const char *key, const char *value);
extern void csv_next_key_value(FILE *file, char delimiter,
			       char **key, char **value);
    
#endif
