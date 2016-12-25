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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <fcntl.h>

#include "lmap.h"
#include "lmapd.h"
#include "utils.h"
#include "pidfile.h"
#include "xml-io.h"
#include "runner.h"
#include "workspace.h"

static struct lmapd *lmapd = NULL;

static void
atexit_cb()
{
    if (lmapd_pid_check(lmapd)) {
	lmapd_pid_remove(lmapd);
    }
    
    if (lmapd) {
	lmapd_free(lmapd);
    }
}

static void
usage(FILE *f)
{
    fprintf(f, "usage: %s [-f] [-n] [-s] [-z] [-v] [-h] [-q queue] [-c config] [-s status]\n"
	    "\t-f fork (daemonize)\n"
	    "\t-n parse config and dump config and exit\n"
	    "\t-s parse config and dump state and exit\n"
	    "\t-z clean the workspace before starting\n"
	    "\t-q path to queue directory\n" 
	    "\t-c path to config directory or file\n"
	    "\t-r path to run directory (pid file and status file)\n"
	    "\t-v show version information and exit\n"
	    "\t-h show brief usage information and exit\n",
	    LMAPD_LMAPD);
}

/**
 * @brief Daemonizes the process
 *
 * Deamonize the process by detaching from the parent, starting a new
 * session, changing to the root directory, and attaching stdin,
 * stdout and stderr to /dev/null.
 */

static void
daemonize()
{
    int fd;
    pid_t pid;

    pid = fork();
    if (pid < 0) {
	lmap_err("fork() failed");
	exit(EXIT_FAILURE);
    }

    if (pid > 0) {
	exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
	lmap_err("setsid() failed");
	exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid < 0) {
	lmap_err("fork() failed");
	exit(EXIT_FAILURE);
    }

    if (pid > 0) {
	exit(EXIT_SUCCESS);
    }

    if (chdir("/") < 0) {
	lmap_err("chdir() failed");
	exit(EXIT_FAILURE);
    }

    closelog();
    fd = open("/dev/null", O_RDWR, 0);
    if (fd != -1) {
	dup2(fd, STDIN_FILENO);
	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);
    }
    for (fd = sysconf(_SC_OPEN_MAX); fd > 2; fd--) {
	(void) close(fd);
    }
    openlog("lmapd", LOG_PID | LOG_NDELAY, LOG_DAEMON);
}

/**
 * @brief Reads the XML config file
 *
 * Function to read the XML config file and initialize the
 * coresponding data structures with data.
 *
 * @param lmapd pointer to the lmapd struct
 * @return 0 on success -1 or error
 */

static int
read_config(struct lmapd *lmapd)
{
    int ret = 0;

    lmapd->lmap = lmap_new();
    if (! lmapd->lmap) {
	return -1;
    }
    
    ret = lmap_xml_parse_config_path(lmapd->lmap, lmapd->config_path);
    if (ret != 0) {
	lmap_free(lmapd->lmap);
	lmapd->lmap = NULL;
	return -1;
    }
    
    if (lmapd->lmap->agent) {
	lmapd->lmap->agent->last_started = time(NULL);
    }

    if (!lmapd->lmap->capabilities) {
	lmapd->lmap->capabilities = lmap_capability_new();
    }
    if (lmapd->lmap->capabilities) {
	char buf[256];
	snprintf(buf, sizeof(buf), "%s version %d.%d.%d", LMAPD_LMAPD,
		 LMAP_VERSION_MAJOR, LMAP_VERSION_MINOR, LMAP_VERSION_PATCH);
	lmap_capability_set_version(lmapd->lmap->capabilities, buf);
	lmap_capability_add_system_tags(lmapd->lmap->capabilities);
    }

    return 0;
}

int
main(int argc, char *argv[])
{
    int opt, daemon = 0, noop = 0, state = 0, zap = 0, valid = 0, ret = 0;
    char *config_path = NULL;
    char *queue_path = NULL;
    char *run_path = NULL;
    pid_t pid;
    
    while ((opt = getopt(argc, argv, "fnszq:c:r:vh")) != -1) {
	switch (opt) {
	case 'f':
	    daemon = 1;
	    break;
	case 'n':
	    noop = 1;
	    break;
	case 's':
	    state = 1;
	    break;
	case 'z':
	    zap = 1;
	    break;
	case 'q':
	    queue_path = optarg;
	    break;
	case 'c':
	    config_path = optarg;
	    break;
	case 'r':
	    run_path = optarg;
	    break;
	case 'v':
	    printf("%s version %d.%d.%d\n", LMAPD_LMAPD,
		   LMAP_VERSION_MAJOR, LMAP_VERSION_MINOR, LMAP_VERSION_PATCH);
	    exit(EXIT_SUCCESS);
	case 'h':
	    usage(stdout);
	    exit(EXIT_SUCCESS);
	default:
	    usage(stderr);
	    exit(EXIT_FAILURE);
	}
    }

    lmapd = lmapd_new();
    if (! lmapd) {
	exit(EXIT_FAILURE);
    }
    
    atexit(atexit_cb);
    
    openlog("lmapd", LOG_PID | LOG_NDELAY, LOG_DAEMON);
    
    (void) lmapd_set_config_path(lmapd,
				 config_path ? config_path : LMAPD_CONFIG_DIR);
    if (!lmapd->config_path) {
	exit(EXIT_FAILURE);
    }
    
    if (noop || state) {
	if (read_config(lmapd) != 0) {
	    exit(EXIT_FAILURE);
	}
	valid = lmap_valid(lmapd->lmap);
	if (valid && noop) {
	    char *xml = lmap_xml_render_config(lmapd->lmap);
	    if (! xml) {
		exit(EXIT_FAILURE);
	    }
	    fputs(xml, stdout);
	    free(xml);
	}
	if (valid && state) {
	    char *xml = lmap_xml_render_state(lmapd->lmap);
	    if (! xml) {
		exit(EXIT_FAILURE);
	    }
	    fputs(xml, stdout);
	    free(xml);
	}
	if (fflush(stdout) == EOF) {
	    lmap_err("flushing stdout failed");
	    exit(EXIT_FAILURE);
	}
	exit(valid ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    (void) lmapd_set_queue_path(lmapd,
				queue_path ? queue_path : LMAPD_QUEUE_DIR);
    (void) lmapd_set_run_path(lmapd,
			      run_path ? run_path : LMAPD_RUN_DIR);
    if (!lmapd->queue_path || !lmapd->run_path) {
	exit(EXIT_FAILURE);
    }

    if (zap) {
	(void) lmapd_workspace_clean(lmapd);
    }

    if (daemon) {
	daemonize();
    }

    /*
     * Initialize the random number generator. Since random numbers
     * are only used to calculate random spreads, using time() might
     * only cause real issues if a large number of MAs boot up at the
     * same time. Well, perhaps that is even possible after the power
     * outage? I will fix it later when the power is back. ;-)
     */
    
    srand(time(NULL));

    pid = lmapd_pid_read(lmapd);
    if (pid) {
	lmap_err("%s already running (pid %d)?", LMAPD_LMAPD, pid);
	exit(EXIT_FAILURE);
    }
    lmapd_pid_write(lmapd);

    do {
	if (read_config(lmapd) != 0) {
	    exit(EXIT_FAILURE);
	}
	valid = lmap_valid(lmapd->lmap);
	if (! valid) {
	    lmap_err("configuration is invalid - exiting...");
	    exit(EXIT_FAILURE);
	}

	(void) lmapd_workspace_init(lmapd);
	ret = lmapd_run(lmapd);
	
	/*
	 * Sleep one second just in case we get into a failure loop so
	 * as to avoid getting into a crazy tight loop.
	 */

	if (lmapd->flags & LMAPD_FLAG_RESTART) {
	    (void) sleep(1);
	    lmap_free(lmapd->lmap);
	    lmapd->lmap = NULL;
	}
    } while (lmapd->flags & LMAPD_FLAG_RESTART);

    closelog();
    exit(ret == -1 ? EXIT_FAILURE : EXIT_SUCCESS);
}
