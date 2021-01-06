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

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>
#include <ftw.h>
#include <inttypes.h>
#include <time.h>

#include "lmap.h"
#include "lmapd.h"
#include "utils.h"
#include "pidfile.h"
#include "xml-io.h"
#include "json-io.h"
#include "runner.h"
#include "workspace.h"

static int clean_cmd(int argc, char *argv[]);
static int config_cmd(int argc, char *argv[]);
static int help_cmd(int argc, char *argv[]);
static int reload_cmd(int argc, char *argv[]);
static int report_cmd(int argc, char *argv[]);
static int running_cmd(int argc, char *argv[]);
static int shutdown_cmd(int argc, char *argv[]);
static int status_cmd(int argc, char *argv[]);
static int validate_cmd(int argc, char *argv[]);
static int version_cmd(int argc, char *argv[]);

static struct
{
    char *command;
    char *description;
    int (*func) (int argc, char *argv[]);
} cmds[] = {
    { "clean",    "clean the workspace (be careful!)",      clean_cmd },
    { "config",   "validate and render lmap configuration", config_cmd },
    { "help",     "show brief list of commands",            help_cmd },
    { "reload",   "reload the lmap configuration",          reload_cmd },
    { "report",   "report data",			    report_cmd },
    { "running",  "test if the lmap daemon is running",	    running_cmd },
    { "shutdown", "shutdown the lmap daemon",		    shutdown_cmd },
    { "status",   "show status information",                status_cmd },
    { "validate", "validate lmap configuration",            validate_cmd },
    { "version",  "show version information",	            version_cmd },
    { NULL, NULL, NULL }
};

static struct lmapd *lmapd = NULL;

#define LMAP_FORMAT_XML		0x01
#define LMAP_FORMAT_JSON	0x02
static int format = LMAP_FORMAT_XML;

static void
atexit_cb(void)
{
    if (lmapd) {
	lmapd_free(lmapd);
    }
}

static void
vlog(int level, const char *func, const char *format, va_list args)
{
    (void) level;
    (void) func;
    fprintf(stderr, "lmapctl: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
}

static void
usage(FILE *f)
{
    fprintf(f, "usage: %s [-h] [-q queue] [-c config] [-C dir] [-s status]\n"
	    "\t-q path to queue directory\n"
	    "\t-c path to config directory or file\n"
	    "\t-r path to run directory (pid file and status file)\n"
	    "\t-C path in which the program is executed\n"
	    "\t-h show brief usage information and exit\n"
	    "\t-j use json format when generating output\n"
	    "\t-x use xml format when generating output (default)\n",
	    LMAPD_LMAPCTL);
}

static void
help(FILE *f)
{
    int i;

    for (i = 0; cmds[i].command; i++) {
	fprintf(f, "  %-10s  %s\n", cmds[i].command,
		cmds[i].description ? cmds[i].description : "");
    }
}

static char*
render_datetime_short(time_t *tp)
{
    static char buf[32];
    struct tm *tmp;
    time_t now;

    if (! *tp) {
	return "";
    }

    now = time(NULL);

    tmp = localtime(tp);
    if (now-*tp < 24*60*60) {
	strftime(buf, sizeof(buf), "%H:%M:%S", tmp);
    } else {
	strftime(buf, sizeof(buf), "%Y-%m-%d", tmp);
    }
    return buf;
}

static char*
render_storage(uint64_t storage)
{
    static char buf[127];

    if (storage/1024/1024 > 9999) {
	snprintf(buf, sizeof(buf), "%" PRIu64 "G",
		 ((storage/1024/1024)+512)/1014);
    } else if (storage/1024 > 9999) {
	snprintf(buf, sizeof(buf), "%" PRIu64 "M",
		 ((storage/1024)+512)/1024);
    } else if (storage > 9999) {
	snprintf(buf, sizeof(buf), "%" PRIu64 "K",
		 (storage+512)/1024);
    } else {
	snprintf(buf, sizeof(buf), "%" PRIu64, storage);
    }
    return buf;
}

static char*
render_datetime_long(time_t *tp)
{
    static char buf[32];
    struct tm *tmp;

    if (! *tp) {
	return "";
    }

    tmp = localtime(tp);

    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", tmp);

    /*
     * Hack to insert the ':' in the timezone offset since strftime()
     * implementations do not generate this separator.
     */

    if (strlen(buf) == 24) {
	buf[25] = buf[24];
	buf[24] = buf[23];
	buf[23] = buf[22];
	buf[22] = ':';
    }

    return buf;
}

#if 0
static char*
render_uint32(uint32_t num)
{
    static char buf[32];

    if (num > 1000*1000*1000) {
	snprintf(buf, sizeof(buf), "%uG", ((num/1000/1000)+500)/1000);
    } else if (num > 1000*1000) {
	snprintf(buf, sizeof(buf), "%uM", ((num/1000)+500)/1000);
    } else if (num > 1000) {
	snprintf(buf, sizeof(buf), "%uk", (num+500)/1000);
    } else {
	snprintf(buf, sizeof(buf), "%u", num);
    }

    return buf;
}
#endif

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
    lmapd->lmap = lmap_new();
    if (! lmapd->lmap) {
	return -1;
    }

    if (lmap_xml_parse_config_path(lmapd->lmap, lmapd->config_path)) {
	lmap_free(lmapd->lmap);
	lmapd->lmap = NULL;
	return -1;
    }

    return 0;
}

/**
 * @brief Reads the XML state file
 *
 * Function to read the XML state file and initialize the
 * coresponding data structures with data.
 *
 * @param lmapd pointer to the lmapd struct
 * @return 0 on success -1 or error
 */

static int
read_state(struct lmapd *lmapd)
{
    char statefile[PATH_MAX];

    snprintf(statefile, sizeof(statefile), "%s/%s",
	     lmapd->run_path, LMAPD_STATUS_FILE);

    lmapd->lmap = lmap_new();
    if (! lmapd->lmap) {
	return -1;
    }

    if (lmap_xml_parse_state_file(lmapd->lmap, statefile)) {
	lmap_free(lmapd->lmap);
	lmapd->lmap = NULL;
	return -1;
    }

    return 0;
}

static int
clean_cmd(int argc, char *argv[])
{
    pid_t pid;

    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    pid = lmapd_pid_read(lmapd);
    if (! pid) {
	lmap_err("failed to obtain PID of lmapd");
	return 1;
    }

    if (kill(pid, SIGUSR2) == -1) {
	lmap_err("failed to send SIGUSR2 to process %d", pid);
	return 1;
    }

    return 0;
}

static int
config_cmd(int argc, char *argv[])
{
    char *xml;

    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    if (read_config(lmapd) != 0) {
	return 1;
    }
    if (! lmap_valid(lmapd->lmap)) {
	return 1;
    }

    xml = lmap_xml_render_config(lmapd->lmap);
    if (! xml) {
	return 1;
    }
    fputs(xml, stdout);
    free(xml);
    return 0;
}

static int
help_cmd(int argc, char *argv[])
{
    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    help(stdout);
    return 0;
}

static int
reload_cmd(int argc, char *argv[])
{
    pid_t pid;

    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    pid = lmapd_pid_read(lmapd);
    if (! pid) {
	lmap_err("failed to obtain PID of lmapd");
	return 1;
    }

    if (kill(pid, SIGHUP) == -1) {
	lmap_err("failed to send SIGHUP to process %d", pid);
	return 1;
    }

    return 0;
}

static int
report_cmd(int argc, char *argv[])
{
    char *report = NULL;

    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    if (read_config(lmapd) != 0) {
	return 1;
    }
    if (! lmap_valid(lmapd->lmap)) {
	return 1;
    }

    if (lmapd->lmap->agent && ! lmapd->lmap->agent->report_date) {
	lmapd->lmap->agent->report_date = time(NULL);
    }

    /*
     * Setup the paths into the workspaces and then load the results
     * found in the current directory.
     */

    lmapd_workspace_init(lmapd);
    if (lmapd_workspace_read_results(lmapd) == -1) {
	return 1;
    }

    switch (format) {
    case LMAP_FORMAT_XML:
	report = lmap_xml_render_report(lmapd->lmap);
	break;
    case LMAP_FORMAT_JSON:
	report = lmap_json_render_report(lmapd->lmap);
	break;
    }
    if (! report) {
	return 1;
    }
    fputs(report, stdout);
    free(report);
    return 0;
}

static int
running_cmd(int argc, char *argv[])
{
    pid_t pid;

    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    pid = lmapd_pid_read(lmapd);
    if (! pid) {
	return 1;
    }

    return 0;
}

static int
shutdown_cmd(int argc, char *argv[])
{
    pid_t pid;

    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    pid = lmapd_pid_read(lmapd);
    if (! pid) {
	lmap_err("failed to obtain PID of lmapd");
	return 1;
    }

    if (kill(pid, SIGTERM) == -1) {
	lmap_err("failed to send SIGTERM to process %d", pid);
	return 1;
    }

    return 0;
}

static int
status_cmd(int argc, char *argv[])
{
    struct lmap *lmap = NULL;
    pid_t pid;
    struct timespec tp = { .tv_sec = 0, .tv_nsec = 87654321 };

    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    pid = lmapd_pid_read(lmapd);
    if (! pid) {
	lmap_err("failed to obtain PID of lmapd");
	return 1;
    }

    if (kill(pid, SIGUSR1) == -1) {
	lmap_err("failed to send SIGUSR1 to process %d", pid);
	return 1;
    }
    /*
     * I should do something more intelligent here, e.g., wait unti
     * the state file is available with a matching touch date and
     * nobody is writing it (i.e., obtain an exclusing open).
     */
    (void) nanosleep(&tp, NULL);

    if (read_state(lmapd) != 0) {
	return 1;
    }

    if (lmapd->lmap) {
	lmap = lmapd->lmap;
    }

    if (lmap && lmap->agent) {
	struct agent *agent = lmap->agent;
	struct capability *cap = lmap->capabilities;
	printf("agent-id:     %s\n", agent->agent_id);
	printf("version:      %s\n", cap ? cap->version : "<?>");
	if (cap && cap->tags) {
	    struct tag *tag;
	    printf("tags:         ");
	    for (tag = cap->tags; tag; tag = tag->next) {
		printf("%s%s", tag == cap->tags ? "" : ", ", tag->tag);
	    }
	    printf("\n");
	}
	printf("last-started: %s\n",
	       render_datetime_long(&agent->last_started));
	printf("\n");
    }

    printf("%-15.15s %-1s %3.3s %3.3s %3.3s %3.3s %5.5s %3s %3s %-10s %-10s %s\n",
	   "SCHEDULE/ACTION", "S", "IN%", "SU%", "OV%", "ER%", " STOR",
	   "LST", "LFS", "L-INVOKE", "L-COMPLETE", "L-FAILURE");

    if (lmap && lmap->schedules) {
	char *state;
	struct schedule *schedule;
	struct action *action;
	uint32_t total_attempts;

	for (schedule = lmap->schedules; schedule; schedule = schedule->next) {
	    switch (schedule->state) {
	    case LMAP_SCHEDULE_STATE_ENABLED:
		state = "E";
		break;
	    case LMAP_SCHEDULE_STATE_DISABLED:
		state = "D";
		break;
	    case LMAP_SCHEDULE_STATE_RUNNING:
		state = "R";
		break;
	    case LMAP_SCHEDULE_STATE_SUPPRESSED:
		state = "S";
		break;
	    default:
		state = "?";
	    }

	    total_attempts = schedule->cnt_invocations
		+ schedule->cnt_suppressions + schedule->cnt_overlaps;
	    printf("%-15.15s ", schedule->name ? schedule->name : "???");
	    printf("%-1s ", state);
	    printf("%3d %3d %3d %3d ",
		   total_attempts ? schedule->cnt_invocations*100/total_attempts : 0,
		   total_attempts ? schedule->cnt_suppressions*100/total_attempts : 0,
		   total_attempts ? schedule->cnt_overlaps*100/total_attempts : 0,
		   schedule->cnt_invocations ? schedule->cnt_failures*100/schedule->cnt_invocations : 0);

	    printf("%5.5s ", render_storage(schedule->storage));

	    if (schedule->last_invocation) {
		printf("%8.8s%s", "",
		       render_datetime_short(&schedule->last_invocation));
	    }

	    printf("\n");

	    for (action = schedule->actions; action; action = action->next) {
		switch(action->state) {
		case LMAP_ACTION_STATE_ENABLED:
		    state = "E";
		    break;
		case LMAP_ACTION_STATE_DISABLED:
		    state = "D";
		    break;
		case LMAP_ACTION_STATE_RUNNING:
		    state = "R";
		    break;
		case LMAP_ACTION_STATE_SUPPRESSED:
		    state = "S";
		    break;
		default:
		    state = "?";
		}

		total_attempts = action->cnt_invocations
		    + action->cnt_suppressions + action->cnt_overlaps;
		printf(" %-14.14s ", action->name ? action->name : "???");
		printf("%-1s ", state);
		printf("%3d %3d %3d %3d ",
		       total_attempts ? action->cnt_invocations*100/total_attempts : 0,
		       total_attempts ? action->cnt_suppressions*100/total_attempts : 0,
		       total_attempts ? action->cnt_overlaps*100/total_attempts : 0,
		       action->cnt_invocations ? action->cnt_failures*100/action->cnt_invocations : 0);

		printf("%5.5s ", render_storage(action->storage));

		printf("%3d %3d ", action->last_status,
		       action->last_failed_status);

		printf("%-10s ", action->last_invocation
		       ? render_datetime_short(&action->last_invocation) : "");

		printf("%-10s ", action->last_completion
		       ? render_datetime_short(&action->last_completion) : "");

		if (action->last_failed_completion) {
		    printf("%s", render_datetime_short(&action->last_failed_completion));
		}
		printf("\n");
	    }
	}
    }

    printf("\n");
    printf("%-15.15s %-1s\n",
	   "SUPPRESSION", "S");

    if (lmap && lmap->supps) {
	char *state;
	struct supp *supp;

	for (supp = lmap->supps; supp; supp = supp->next) {
	    switch (supp->state) {
	    case LMAP_SUPP_STATE_ENABLED:
		state = "E";
		break;
	    case LMAP_SUPP_STATE_DISABLED:
		state = "D";
		break;
	    case LMAP_SUPP_STATE_ACTIVE:
		state = "A";
		break;
	    default:
		state = "?";
		break;
	    }

	    printf("%-15.15s ", supp->name ? supp->name : "???");
	    printf("%-1s ", state);

	    printf("\n");
	}
    }
    return 0;
}

static int
validate_cmd(int argc, char *argv[])
{
    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    if (read_config(lmapd) != 0) {
	return 1;
    }
    if (! lmap_valid(lmapd->lmap)) {
	return 1;
    }
    return 0;
}

static int
version_cmd(int argc, char *argv[])
{
    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    printf("%s version %d.%d.%d\n", LMAPD_LMAPCTL,
	   LMAP_VERSION_MAJOR, LMAP_VERSION_MINOR, LMAP_VERSION_PATCH);
    return 0;
}

int
main(int argc, char *argv[])
{
    int i, opt;
    char *config_path = NULL;
    char *queue_path = NULL;
    char *run_path = NULL;

    while ((opt = getopt(argc, argv, "q:c:r:C:hjx")) != -1) {
	switch (opt) {
	case 'q':
	    queue_path = optarg;
	    break;
	case 'c':
	    config_path = optarg;
	    break;
	case 'r':
	    run_path = optarg;
	    break;
	case 'C':
	    if (chdir(optarg) == -1) {
		lmap_err("failed to change directory to '%s'", optarg);
		exit(EXIT_FAILURE);
	    }
	    break;
	case 'h':
	    usage(stdout);
	    exit(EXIT_SUCCESS);
	case 'j':
	    format = LMAP_FORMAT_JSON;
	    break;
	case 'x':
	    format = LMAP_FORMAT_XML;
	    break;
	default:
	    usage(stderr);
	    exit(EXIT_FAILURE);
	}
    }

    lmap_set_log_handler(vlog);

    lmapd = lmapd_new();
    if (! lmapd) {
	exit(EXIT_FAILURE);
    }

    atexit(atexit_cb);

    if (optind >= argc) {
	lmap_err("expected command argument after options");
	exit(EXIT_FAILURE);
    }

    (void) lmapd_set_config_path(lmapd,
				config_path ? config_path : LMAPD_CONFIG_DIR);
    (void) lmapd_set_queue_path(lmapd,
				queue_path ? queue_path : LMAPD_QUEUE_DIR);
    (void) lmapd_set_run_path(lmapd,
			      run_path ? run_path : LMAPD_RUN_DIR);
    if (!lmapd->config_path || !lmapd->queue_path || !lmapd->run_path) {
	exit(EXIT_FAILURE);
    }

    for (i = 0; cmds[i].command; i++) {
	if (! strcmp(cmds[i].command, argv[optind])) {
	    if (cmds[i].func(argc - optind, argv + optind) == 0) {
		exit(EXIT_SUCCESS);
	    } else {
		exit(EXIT_FAILURE);
	    }
	}
    }

    if (! cmds[i].command) {
	lmap_err("unknown command '%s' - valid commands are:", argv[optind]);
	help(stderr);
	exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}
