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

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include "lmap.h"
#include "lmapd.h"
#include "utils.h"
#include "xml-io.h"
#include "runner.h"
#include "signals.h"
#include "workspace.h"

/**
 * @brief Callback executed when SIGINT is received
 *
 * Function which is executed when SIGINT is received by the daemon.
 * We simply signal to quit the eventloop and then let the lmapd main
 * loop cleanup and exit.
 *
 * @param sig unused
 * @param events unused
 * @param context pointer to the lmapd structure
 */

void
lmapd_sigint_cb(evutil_socket_t sig, short events, void *context)
{
    struct lmapd *lmapd = (struct lmapd *) context;

    (void) sig;
    (void) events;

    assert(lmapd);
    lmapd_stop(lmapd);
}

/**
 * @brief Callback executed when SIGTERM is received
 *
 * Function which is executed when SIGTERM is received by the daemon.
 * We simply signal to quit the eventloop and then let the lmapd main
 * loop cleanup and exit.
 *
 * @param sig unused
 * @param events unused
 * @param context pointer to the lmapd structure
 */

void
lmapd_sigterm_cb(evutil_socket_t sig, short events, void *context)
{
    struct lmapd *lmapd = (struct lmapd *) context;

    (void) sig;
    (void) events;

    assert(lmapd);
    lmapd_stop(lmapd);
}

/**
 * @brief Callback executed when SIGHUP is received
 *
 * Function which is executed when SIGHUP is received by the daemon.
 * We simply signal to quit the eventloop and then let the lmapd main
 * loop reload and restart the system.
 *
 * @param sig unused
 * @param events unused
 * @param context pointer to the lmapd structure
 */

void
lmapd_sighub_cb(evutil_socket_t sig, short events, void *context)
{
    struct lmapd *lmapd = (struct lmapd *) context;

    (void) sig;
    (void) events;

    assert(lmapd);
    lmapd_restart(lmapd); 
}

/**
 * @brief Callback executed when SIGCHLD is received
 *
 * Function which is executed when SIGCHLD is received by the daemon.
 * Call the runner function that takes care of finished child
 * processes (actions).
 *
 * @param sig unused
 * @param events unused
 * @param context pointer to the lmapd structure
 */

void
lmapd_sigchld_cb(evutil_socket_t sig, short events, void *context)
{
    struct lmapd *lmapd = (struct lmapd *) context;

    (void) sig;
    (void) events;

    assert(lmapd);
    lmapd_cleanup(lmapd);
}

/**
 * @brief Callback executed when SIGUSR1 is received
 *
 * Function which is executed when SIGUSR1 is received by the
 * daemon. It obtains the lmap state information rendered in XML and
 * writes it into the lmap state file in the run directory.
 *
 * @param sig unused
 * @param events unused
 * @param context pointer to the lmapd structure
 */

void
lmapd_sigusr1_cb(evutil_socket_t sig, short events, void *context)
{
    FILE *f = NULL;
    char *xml = NULL;
    char filename[PATH_MAX];
    struct lmapd *lmapd = (struct lmapd *) context;
    
    (void) sig;
    (void) events;

    assert(lmapd);
    assert(lmapd->run_path);

    lmapd_workspace_update(lmapd);
    xml = lmap_xml_render_state(lmapd->lmap);
    if (! xml) {
	lmap_err("failed to render lmap state");
	return;
    }

    snprintf(filename, sizeof(filename),
	     "%s/%s", lmapd->run_path, LMAPD_STATUS_FILE);
    f = fopen(filename, "w");
    if (! f) {
	lmap_err("failed to open '%s': %s", filename, strerror(errno));
	goto done;
    }

    if (fputs(xml, f) == EOF || fflush(f) == EOF) {
	lmap_err("failed to write to '%s'", filename);
	goto done;
    }

done:
    if (f) {
	(void) fclose(f);
    }
    if (xml) {
	free(xml);
    }
}

/**
 * @brief Callback executed when SIGUSR2 is received
 *
 * Function which is executed when SIGUSR2 is received by the
 * daemon. It cleans and reinitializes the workspace.
 *
 * @param sig unused
 * @param events unused
 * @param context pointer to the lmapd structure
 */

void
lmapd_sigusr2_cb(evutil_socket_t sig, short events, void *context)
{
    struct lmapd *lmapd = (struct lmapd *) context;
    
    (void) sig;
    (void) events;

    assert(lmapd);
    assert(lmapd->run_path);

    if (lmapd_workspace_clean(lmapd) == 0) {
	(void) lmapd_workspace_init(lmapd);
    }
}

