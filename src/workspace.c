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

#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <ftw.h>
#include <signal.h>
#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>

#include "lmap.h"
#include "lmapd.h"
#include "utils.h"
#include "workspace.h"

#define JSON
    
#ifdef JSON
#include <json.h>
#endif

/**
 * @brief Append a field to a CSV file
 *
 * Appends a string value as a field to a CSV file. The string is
 * quoted if the string value contains the delimiter or white space or
 * the quote character. See RFC 4180 for more details.
 *
 * @param file pointer to the open FILE stream
 * @param delimiter delimiter character
 * @param s string value
 */

static void
csv_append(FILE *file, char delimiter, const char *s)
{
    int i, need_quote = 0;
    const char quote = '"';
    
    if (delimiter) {
	fputc(delimiter, file);
    }

    for (i = 0; s && s[i]; i++) {
	if ((delimiter && s[i] == delimiter)
	    || s[i] == quote || isspace(s[i])) {
	    need_quote = 1;
	    break;
	}
    }
    
    if (need_quote) {
	fputc(quote, file);
	for (i = 0; s && s[i]; i++) {
	    fputc(s[i], file);
	    if (s[i] == quote) {
		fputc(s[i], file);
	    }
	}
	fputc(quote, file);
    } else {
	fputs(s, file);
    }
}

/* XXX buffer overflow checks */

char*
csv_next(FILE *file, char delimiter)
{
    int c, i, quoted = 0;
    static char buf[1234];
    const char quote = '"';

    i = 0;
    while ((c = fgetc(file)) != EOF) {
	// lmap_dbg(".. %c", c);
	if (!quoted && c == delimiter) {
	    break;
	}
	if (c == '\n') {
	    if (i == 0) {
		return NULL;
	    } else {
		ungetc(c, file);
		break;
	    }
	}
	if (i == 0 && !quoted && isspace(c)) {
	    continue;
	}
	if (i == 0 && c == quote) {
	    quoted = 1;
	    continue;
	}
	if (c == quote) {
	    if (quoted) {
		if ((c = fgetc(file)) == EOF) {
		    break;
		}
		if (c == delimiter) {
		    break;
		}
		buf[i] = c;
	    } else {
		buf[i] = c;
	    }
	} else {
	    buf[i] = c;
	}
	i++;
    }

    buf[i] = 0;
    return buf;
}

/**
 * @brief End a record in a CSV file
 *
 * Ends a record (aka line) in a CSV file.
 *
 * @param file pointer to the open FILE stream
 */

static void
csv_end(FILE *file)
{
    fprintf(file, "\n");
}

/**
 * @brief Create a safe filesystem name
 *
 * Creates a safe filesystem name. Unsafe characters are %-encoded if
 * necessary.
 *
 * @param name file system name
 * @return pointer to a safe filesystem name (static buffer)
 */

static char*
mksafe(const char *name)
{
    int i, j;
    const char safe[] = "-.,_";
    const char hex[] = "0123456789ABCDEF";
    static char save_name[NAME_MAX];
    
    for (i = 0, j = 0; name[i] && j < NAME_MAX-1; i++) {
	if (isalnum(name[i]) || strchr(safe, name[i])) {
	    save_name[j++] = name[i];
	} else {
	    /* %-escape the char if there is enough space left */
	    if (j < NAME_MAX - 4) {
		save_name[j++] = '%';
		save_name[j++] = hex[(name[i]>>4) & 0x0f];
		save_name[j++] = hex[name[i] & 0x0f];;
	    } else {
		break;
	    }
	}
    }
    save_name[j] = 0;
    return save_name;
}

/**
 * @brief Callback for removing a file
 *
 * Callback called by nftw when traversing a directory tree.
 *
 * @param fpath path to the file which is to be removed
 * @param sb unused
 * @param typeflag unused
 * @param ftwbuf unused
 * @return 0 on success, -1 on error
 */

static int
remove_cb(const char *fpath, const struct stat *sb,
	  int typeflag, struct FTW *ftwbuf)
{
    (void) sb;
    (void) typeflag;
    (void) ftwbuf;
    
    if (remove(fpath)) {
	lmap_err("cannot remove %s", fpath);
	/* we continue to remove the rest */
    }
    return 0;
}

/**
 * @brief Recursively removes a directory/file
 *
 * Function to recursively remove a directory/file from the
 * filesystem. A directory can include other files or directories.
 *
 * @param path path to directory or file
 * @return 0 on success, -1 on error
 */

static int
remove_all(char *path)
{
    if (! path) {
	return -1;
    }
    return nftw(path, remove_cb, 12, FTW_DEPTH | FTW_PHYS);
}

static unsigned long du_cnt = 0;
static unsigned long du_size = 0;
static unsigned long du_blocks = 0;

static int
du_cb(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf)
{
    if (tflag == FTW_F) {
	du_size += sb->st_size;
	du_blocks += sb->st_blocks;
	du_cnt++;
    }
    return 0;           /* To tell nftw() to continue */
}

static int
du(char *path, uint64_t *storage)
{
    du_cnt = 0;
    du_size = 0;
    du_blocks = 0;
    if (nftw(path, du_cb, 6, 0) == -1) {
	return -1;
    }
    *storage = du_blocks * 512;
    return 0;
}


/**
 * @brief Clean the complete workspace (aka queue) directory
 *
 * Function to clean the queue directory by removing everything in the
 * queue directory.
 *
 * @param lmapd pointer to the struct lmapd
 * @return 0 on success, -1 on error
 */

int
lmapd_workspace_clean(struct lmapd *lmapd)
{
    int ret = 0;
    char filepath[PATH_MAX];
    struct dirent *dp;
    DIR *dfd;

    assert(lmapd);

    (void) remove_all(NULL);

    if (!lmapd->queue_path) {
	return 0;
    }

    dfd = opendir(lmapd->queue_path);
    if (!dfd) {
	lmap_err("failed to open queue directory '%s'", lmapd->queue_path);
	return -1;
    }
    
    while ((dp = readdir(dfd)) != NULL) {
	if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) {
	    continue;
	}
	(void) snprintf(filepath, sizeof(filepath), "%s/%s",
			lmapd->queue_path, dp->d_name);
	if (remove_all(filepath) != 0) {
	    lmap_err("failed to remove '%s'", filepath);
	    ret = -1;
	}
    }
    (void) closedir(dfd);

    return ret;
}

int
lmapd_workspace_update(struct lmapd *lmapd)
{
    int ret = 0;
    struct schedule *sched;
    struct action *act;

    if (!lmapd || !lmapd->lmap) {
	return 0;
    }

    for (sched = lmapd->lmap->schedules; sched; sched = sched->next) {
	if (du(sched->workspace, &sched->storage) == -1) {
	    ret = -1;
	}
	for (act = sched->actions; act; act = act->next) {
	    if (du(act->workspace, &act->storage) == -1) {
		ret = -1;
	    }
	}
    }

    return ret;
}

/**
 * @brief Clean the workspace of an action
 *
 * Function to clean the workspace of an action by removing everything
 * in the workspace directory.
 *
 * @param lmapd pointer to the struct lmapd
 * @param action pointer to the struct action
 * @return 0 on success, -1 on error
 */

int
lmapd_workspace_action_clean(struct lmapd *lmapd, struct action *action)
{
    int ret = 0;
    char filepath[PATH_MAX];
    struct dirent *dp;
    DIR *dfd;

    assert(lmapd);
    (void) lmapd;

    if (!action || !action->workspace) {
	return 0;
    }

    dfd = opendir(action->workspace);
    if (!dfd) {
	lmap_err("failed to open '%s'", action->workspace);
	return -1;
    }

    while ((dp = readdir(dfd)) != NULL) {
	if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) {
	    continue;
	}
	snprintf(filepath, sizeof(filepath), "%s/%s",
		 action->workspace, dp->d_name);
	if (remove_all(filepath) != 0) {
	    lmap_err("failed to remove '%s'", filepath);
	    ret = -1;
	}
    }
    (void) closedir(dfd);

    return ret;
}

/**
 * @brief Move the workspace of an action
 *
 * Function to move the workspace of an action to a destination
 * schedule.
 *
 * @param lmapd pointer to the struct lmapd
 * @param schedule pointer to the struct schedule
 * @param action pointer to the struct action
 * @param destination pointer to the struct schedule
 * @return 0 on success, -1 on error
 */

int
lmapd_workspace_action_move(struct lmapd *lmapd, struct schedule *schedule,
		            struct action *action, struct schedule *destination)
{
    int ret = 0;
    char oldfilepath[PATH_MAX];
    char newfilepath[PATH_MAX];
    struct dirent *dp;
    DIR *dfd;

    assert(lmapd);
    (void) lmapd;

    if (!schedule || !schedule->name
	|| !action || !action->workspace || !action->name
	|| !destination || !destination->workspace) {
	return 0;
    }

    dfd = opendir(action->workspace);
    if (!dfd) {
	lmap_err("failed to open '%s'", action->workspace);
	return -1;
    }

    while ((dp = readdir(dfd)) != NULL) {
	if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) {
	    continue;
	}
	snprintf(oldfilepath, sizeof(oldfilepath), "%s/%s",
		 action->workspace, dp->d_name);
	snprintf(newfilepath, sizeof(newfilepath), "%s/%s",
		 destination->workspace, dp->d_name);
	if (link(oldfilepath, newfilepath) < 0) {
	    lmap_err("failed to move '%s' to '%s'", oldfilepath, newfilepath);
	    ret = -1;
	}
    }
    (void) closedir(dfd);

    return ret;
}

/**
 * @brief Create workspace folders for schedules and their actions
 *
 * Function to create the workspace folders for schedules and their
 * actions. Actions store results before in their workspace sending
 * them to the destination schedule.
 *
 * @param lmapd pointer to struct lmapd
 * @return 0 on success, -1 on error
 */

int
lmapd_workspace_init(struct lmapd *lmapd)
{
    int ret = 0;
    struct schedule *sched;
    struct action *act;
    char filepath[PATH_MAX];
    
    assert(lmapd);

    if (!lmapd->lmap || !lmapd->queue_path) {
	return 0;
    }

    for (sched = lmapd->lmap->schedules; sched; sched = sched->next) {
	if (! sched->name) {
	    continue;
	}
	snprintf(filepath, sizeof(filepath), "%s/%s",
		 lmapd->queue_path, mksafe(sched->name));
	if (mkdir(filepath, 0700) < 0 && errno != EEXIST) {
	    lmap_err("failed to mkdir '%s'", filepath);
	    ret = -1;
	}
	lmap_schedule_set_workspace(sched, filepath);

	for (act = sched->actions; act; act = act->next) {
	    if (! act->name) {
		continue;
	    }
	    snprintf(filepath, sizeof(filepath), "%s/%s",
		     sched->workspace, mksafe(act->name));
	    if (mkdir(filepath, 0700) < 0 && errno != EEXIST) {
		lmap_err("failed to mkdir '%s'", filepath);
		ret = -1;
		continue;
	    }
	    lmap_action_set_workspace(act, filepath);
	}
    }

    return ret;
}

int
lmapd_workspace_action_meta_add_start(struct schedule *schedule, struct action *action, struct task *task)
{
    int fd;
    FILE *f;
    char buf[128];
    struct option *option;
    const char delimiter = ';';
    
    assert(action && action->name && action->workspace);

    fd = lmapd_workspace_action_open_meta(schedule, action,
					  O_WRONLY | O_CREAT | O_TRUNC);
    if (fd == -1) {
	return -1;
    }
    f = fdopen(fd, "w");
    if (! f) {
	lmap_err("failed to create meta file stream for action '%s'",
		 action->name);
	(void) close(fd);
	return -1;
    }
    
    snprintf(buf, sizeof(buf), "%s.%d.%d",
	     "LMAPD", LMAP_VERSION_MAJOR, LMAP_VERSION_MINOR);
    csv_append(f, 0, buf);
    snprintf(buf, sizeof(buf), "%lu", action->last_invocation);
    csv_append(f, delimiter, buf);
    csv_append(f, delimiter, "OK");
    csv_append(f, delimiter, action->name ? schedule->name : "");
    csv_append(f, delimiter, action->name ? action->name : "");
    csv_append(f, delimiter, action->task ? action->task : "");
    for (option = task->options; option; option = option->next) {
	csv_append(f, delimiter, option->id ? option->id : "");
	csv_append(f, delimiter, option->name ? option->name : "");
	csv_append(f, delimiter, option->value ? option->value : "");
    }
    for (option = action->options; option; option = option->next) {
	csv_append(f, delimiter, option->id ? option->id : "");
	csv_append(f, delimiter, option->name ? option->name : "");
	csv_append(f, delimiter, option->value ? option->value : "");
    }
    csv_end(f);
    (void) fclose(f);

#ifdef JSON
    json_object * jobj = json_object_new_object();
    json_object_object_add(jobj, "schedule",
	   json_object_new_string(schedule->name ? schedule->name : ""));
    json_object_object_add(jobj, "action",
	   json_object_new_string(action->name ? action->name : ""));
    json_object_object_add(jobj, "task",
	   json_object_new_string(action->task ? action->task : ""));
    fprintf(stderr, "** >>> %s <<<\n", json_object_to_json_string(jobj));
    json_object_put(jobj);
#endif
    
    return 0;
}

int
lmapd_workspace_action_meta_add_end(struct schedule *schedule, struct action *action)
{
    int fd;
    FILE *f;
    char buf[128];
    const char delimiter = ';';

    assert(action && action->name && action->workspace);
    
    fd = lmapd_workspace_action_open_meta(schedule, action,
					  O_WRONLY | O_APPEND);
    if (fd == -1) {
	return -1;
    }
    f = fdopen(fd, "a");
    if (! f) {
	lmap_err("failed to append meta file stream for action '%s'", action->name);
	(void) close(fd);
	return -1;
    }
    
    snprintf(buf, sizeof(buf), "%s.%d.%d",
	     "LMAPD", LMAP_VERSION_MAJOR, LMAP_VERSION_MINOR);
    csv_append(f, 0, buf);
    snprintf(buf, sizeof(buf), "%lu", action->last_completion);
    csv_append(f, delimiter, buf);
    csv_append(f, delimiter, action->last_status == 0 ? "OK" : "FAIL");
    snprintf(buf, sizeof(buf), "%d", action->last_status);
    csv_append(f, delimiter, buf);
    csv_end(f);
    (void) fclose(f);
    return 0;
}

int
lmapd_workspace_action_open_data(struct schedule *schedule,
				 struct action *action, int flags)
{
    int fd;
    int len;
    char filepath[PATH_MAX];

    snprintf(filepath, sizeof(filepath), "%s/%lu-%s",
	     action->workspace, action->last_invocation,
	     mksafe(schedule->name));
    len = strlen(filepath);
    snprintf(filepath+len, sizeof(filepath)-len, "-%s.data",
	     mksafe(action->name));
    fd = open(filepath, flags, 0600);
    if (fd == -1) {
	lmap_err("failed to open '%s'", filepath);
    }
    return fd;
}

int
lmapd_workspace_action_open_meta(struct schedule *schedule,
				 struct action *action, int flags)
{
    int fd;
    int len;
    char filepath[PATH_MAX];

    snprintf(filepath, sizeof(filepath), "%s/%lu-%s",
	     action->workspace, action->last_invocation,
	     mksafe(schedule->name));
    len = strlen(filepath);
    snprintf(filepath+len, sizeof(filepath)-len, "-%s.meta",
	     mksafe(action->name));
    fd = open(filepath, flags, 0600);
    if (fd == -1) {
	lmap_err("failed to open '%s'", filepath);
    }
    return fd;
}

static struct table *
read_table(int fd)
{
    int inrow = 0;
    FILE *file;
    struct table *tab;
    struct row *row = NULL;
    struct value *val;

    file = fdopen(fd, "r");
    if (! file) {
	lmap_err("failed to create file stream");
	return NULL;
    }

    tab = lmap_table_new();
    if (! tab) {
	(void) fclose(file);
	return NULL;
    }

    while (!feof(file)) {
	char *s = csv_next(file, ';');
	if (! s) {
	    if (feof(file)) {
		break;
	    }
	    inrow = 0;
	    continue;
	}
	if (!inrow) {
	    row = lmap_row_new();
	    if (! row) {
		lmap_table_free(tab);
		(void) fclose(file);
		return NULL;
	    }
	    lmap_table_add_row(tab, row);
	    inrow++;
	}
	val = lmap_value_new();
	if (! val) {
	    lmap_table_free(tab);
	    (void) fclose(file);
	    return NULL;
	}
	lmap_value_set_value(val, s);
	lmap_row_add_value(row, val);
    }

    (void) fclose(file);
    return tab;
}

static int
check_tag(struct result *res, const char *value)
{
    if (strcmp(value, "LMAPD-0.3")) {
	lmap_wrn("unexpected tag '%s' in report file", value);
	return -1;
    }
    return 0;
}

static int
check_status(struct result *res, const char *value)
{
    if (strcmp(value, "OK") && strcmp(value, "FAIL")) {
	lmap_wrn("unexpected status '%s' in report file", value);
	return -1;
    }
    return 0;
}

static struct result *
read_result(int fd)
{
    struct result *res;
    struct table *tab;
    struct row *row;
    struct value *val;
    int r, c;

    int (*r0[])(struct result *res, const char *c) = {
	check_tag,
	lmap_result_set_start,
	check_status,
	lmap_result_set_schedule,
	lmap_result_set_action,
	lmap_result_set_task,
	NULL
    };
		
    int (*r1[])(struct result *res, const char *c) = {
	check_tag,
	lmap_result_set_end,
	check_status,
	lmap_result_set_status,
	NULL
    };

    tab = read_table(fd);
    if (! tab) {
	return NULL;
    }

    res = lmap_result_new();
    if (! res) {
	lmap_table_free(tab);
	return NULL;
    }

    for (r = 0, row = tab->rows; row; row = row->next, r++) {
	for (c = 0, val = row->values; val; val = val->next, c++) {
	    lmap_dbg("** tab[%d,%d] = >%s<", r, c, val->value);
	    if (r == 0 && c < sizeof(r0)/sizeof(r0[0])-1) {
		if (r0[c](res, val->value) == -1) {
		    lmap_result_free(res);
		    lmap_table_free(tab);
		    return NULL;
		}
	    }
	    if (r == 1 && c < sizeof(r1)/sizeof(r1[0])-1) {
		if (r1[c](res, val->value) == -1) {
		    lmap_result_free(res);
		    lmap_table_free(tab);
		    return NULL;
		}
	    }
	}
    }

    return res;
}

int
lmapd_workspace_read_results(struct lmapd *lmapd)
{
    char *p;
    struct dirent *dp;
    DIR *dfd;
    struct table *tab;
    struct result *res;

    dfd = opendir(".");
    if (!dfd) {
	lmap_err("failed to open workspace directory '%s'", ".");
	return -1;
    }

    while ((dp = readdir(dfd)) != NULL) {
	if (strlen(dp->d_name) < 5) {
	    continue;
	}
	p = strrchr(dp->d_name, '.');
	if (! p) {
	    continue;
	}
	if (!strcmp(p, ".meta")) {
	    int mfd, dfd;
	    mfd = open(dp->d_name, O_RDONLY);
	    if (! mfd) {
		lmap_err("failed to open meta file '%s'", dp->d_name);
		continue;
	    }
	    strcpy(p, ".data");
	    dfd = open(dp->d_name, O_RDONLY);
	    if (! dfd) {
		lmap_err("failed to open data file '%s'", dp->d_name);
		(void) close(mfd);
		continue;
	    }
	    res = read_result(mfd);
	    if (res) {
		lmap_add_result(lmapd->lmap, res);
		tab = read_table(dfd);
		if (tab) {
		    lmap_result_add_table(res, tab);
		}
	    }
	}
    }
    (void) closedir(dfd);
    return 0;
}
