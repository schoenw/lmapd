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

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>

#include "lmap.h"
#include "lmapd.h"
#include "utils.h"
#include "pidfile.h"

/**
 * @brief Reads the PID stored in the pidfile
 *
 * Function to read the process identifier (PID) stored in the
 * pidfile.  We do a simple check to see if the process is alive,
 * and return 0 if it isn't.  But it could be any process.
 *
 * @param lmapd pointer to struct lmapd
 * @return valid PID on success, 0 on error
 */

pid_t
lmapd_pid_read(struct lmapd *lmapd)
{
    FILE *f;
    pid_t pid;
    char pidfile[PATH_MAX];

    snprintf(pidfile, sizeof(pidfile),
	     "%s/%s", lmapd->run_path, LMAPD_PID_FILE);

    if ((f = fopen(pidfile, "r")) == NULL) {
	return 0;
    }

    if (fscanf(f, "%d", &pid) != 1) {
	fclose(f);
	return 0;
    }

    fclose(f);
    return (pid > 0 && !(kill(pid, 0) == -1 && errno == ESRCH)) ? pid : 0;
}

/**
 * @brief Checks if the current PID is written to the pidfile
 *
 * Function to check if the current process identifier (pid) of the
 * current process is stored in the pid file
 *
 * @param lmapd pointer to struct lmapd
 * @return valid pid on success, 0 on error
 */

pid_t
lmapd_pid_check(struct lmapd *lmapd)
{
    pid_t pid = lmapd_pid_read(lmapd);

    if ((!pid) || (pid != getpid())) {
	return 0;
    }

    if (kill(pid, 0) && errno == ESRCH) {
	return 0;
    }

    return pid;
}

/**
 * @brief Writes current pid into the pidfile
 *
 * Function to write the current process identifier (pid) into a
 * pidfile.
 *
 * @param lmapd pointer to struct lmapd
 * @return 0 on success, -1 on erorr
 */

int
lmapd_pid_write(struct lmapd *lmapd)
{
    FILE *f;
    pid_t pid;
    char pidfile[PATH_MAX];

    snprintf(pidfile, sizeof(pidfile),
	     "%s/%s", lmapd->run_path, LMAPD_PID_FILE);

    if ((f = fopen(pidfile, "w+")) == NULL) {
	lmap_err("failed to create pid file '%s'", pidfile);
	return -1;
    }

    pid = getpid();
    if (! fprintf(f, "%d\n", pid)) {
	lmap_err("failed to write pid into '%s'", pidfile);
	fclose(f);
	return -1;
    }

    fflush(f);
    fclose(f);
    return 0;
}

/**
 * @brief Removes the pid file
 *
 * Function to remove the specified file in which we store the process
 * id (pid) of the running lmapd.
 *
 * @param lmapd pointer to struct lmapd
 * @return 0 on success, -1 on error
 */

int
lmapd_pid_remove(struct lmapd *lmapd)
{
    char pidfile[PATH_MAX];

    snprintf(pidfile, sizeof(pidfile),
	     "%s/%s", lmapd->run_path, LMAPD_PID_FILE);

    return unlink(pidfile);
}
