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

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <inttypes.h>
#include <fnmatch.h>
#include <errno.h>

#include "lmap.h"
#include "lmapd.h"
#include "utils.h"
#include "workspace.h"
#include "runner.h"
#include "signals.h"

#if 1
static void
event_gaga(struct event *event, struct event **ev,
	   short what, event_callback_fn func, struct timeval *tv)
{
    assert(event && event->lmapd && ev && *ev == NULL);

    *ev = event_new(event->lmapd->base, -1, what, func, event);
    if (!*ev || event_add(*ev, tv) < 0)  {
	lmap_err("failed to create/add event for '%s'", event->name);
    }
}
#endif

/**
 * @brief Generate a uniformly distributed random number.
 *
 * Generate a uniformly distributed random number in the interval
 * [min, max] by deviding the numbers space into ranges of equal
 * size. Seee <http://stackoverflow.com/a/17554531> for details.
 *
 * @param min the lower bound of the interval
 * @param max the upper bound of the interval
 * @return a random number in the interval [min, max]
 */

static unsigned int
rand_interval(unsigned int min, unsigned int max)
{
    int r;
    const unsigned int range = 1 + max - min;
    const unsigned int buckets = RAND_MAX / range;
    const unsigned int limit = buckets * range;

    /* Create equal size buckets all in a row, then fire randomly towards
     * the buckets until you land in one of them. All buckets are equally
     * likely. If you land off the end of the line of buckets, try again. */
    do {
	r = rand();
    } while (r >= limit);
    
    return min + (r / buckets);
}

static void
add_random_spread(struct event *event, struct timeval *tv)
{
    if (event->flags & LMAP_EVENT_FLAG_RANDOM_SPREAD_SET) {
	uint32_t spread = rand_interval(0, event->random_spread);
	// lmap_dbg("adding %u seconds random spread to %s",
	// 	 spread, event->name);
	tv->tv_sec += spread;
    }
}

static struct action *
find_action_by_pid(struct lmap *lmap, pid_t pid)
{
    struct schedule *sched;
    struct action *act;
    
    assert(lmap);

    for (sched = lmap->schedules; sched; sched = sched->next) {
	for (act = sched->actions; act; act = act->next) {
	    if (act->pid == pid) {
		return act;
	    }
	}
    }

    return NULL;
}

static struct schedule *
find_schedule_by_pid(struct lmap *lmap, pid_t pid)
{
    struct schedule *sched;
    struct action *act;
    
    assert(lmap);

    for (sched = lmap->schedules; sched; sched = sched->next) {
	for (act = sched->actions; act; act = act->next) {
	    if (act->pid == pid) {
		return sched;
	    }
	}
    }

    return NULL;
}

static int
big_tag_match(struct tag *match, struct tag *tags)
{
    struct tag *m, *t;

    for (m = match; m; m = m->next) {
	for (t = tags; t; t = t->next) {
	    if (fnmatch(m->tag, t->tag, 0) == 0) {
		return 1;
	    }
	}
    }

    return 0;
}

static int
action_exec(struct lmapd *lmapd, struct schedule *schedule, struct action *action)
{
    pid_t pid;
    char *argv[256];
    struct timeval t;
    struct task *task;
    struct option *option;
    int i, fd;
    const int argv_limit = sizeof(argv)/sizeof(argv[0]) - 4;

    assert(lmapd);

    if (!action || !action->name || !action->task || !action->workspace) {
	return 0;
    }

    if (action->state == LMAP_ACTION_STATE_SUPPRESSED) {
	action->cnt_suppressions++;
    }

    if (action->state == LMAP_ACTION_STATE_DISABLED
	|| action->state == LMAP_ACTION_STATE_SUPPRESSED) {
	return 0;
    }

    task = lmap_find_task(lmapd->lmap, action->task);
    if (! task) {
	lmap_err("task '%s' for action '%s' does not exist",
		 action->task, action->name);
	return -1;
    }

    if (!task->program) {
	lmap_err("task '%s' has no program", task->name);
	return -1;
    }

    /*
     * Check that the program of the task is listed as a valid
     * capability; we do not want to run arbitrary commands.
     */

    {
	struct task *tp = NULL;
	
	if (lmapd->lmap->capabilities) {
	    for (tp = lmapd->lmap->capabilities->tasks; tp; tp = tp->next) {
		if (tp->program && strcmp(task->program, tp->program) == 0) {
		    break;
		}
	    }
	}
	if (! tp) {
	    lmap_err("task '%s' does not match capabilities", task->name);
	    return -1;
	}
    }
    
    if (action->pid) {
	lmap_wrn("action '%s' still running (pid %d) - skipping",
		 action->name, action->pid);
	action->cnt_overlaps++;
	return -1;
    }

    event_base_gettimeofday_cached(lmapd->base, &t);

    /*
     * Create the argument vector. Note that the max. number of
     * elements in argv is on some systems a runtime parameter.
     * Perhaps we should dynamically allocate an argv?
     */
    
    i = 0;
    argv[i] = task->program;
    for (option = task->options; option; option = option->next) {
	if (i >= argv_limit) {
	    lmap_err("action '%s' has too many arguments", action->name);
	    return -1;
	}
	if (option->name) {
	    argv[++i] = option->name;
	}
	if (option->value) {
	    argv[++i] = option->value;
	}
    }
    for (option = action->options; option; option = option->next) {
	if (i >= argv_limit) {
	    lmap_err("action '%s' has too many arguments", action->name);
	    return -1;
	}
	if (option->name) {
	    argv[++i] = option->name;
	}
	if (option->value) {
	    argv[++i] = option->value;
	}
    }
    argv[++i] = NULL;

    pid = fork();
    if (pid < 0) {
	lmap_err("failed to fork");
	return -1;
    }

    if (pid) {
	action->pid = pid;
	action->last_invocation = t.tv_sec;
	action->state = LMAP_ACTION_STATE_RUNNING;
	action->cnt_invocations++;
	return 1;
    }

    /*
     * Pass some information to the task via the environment; this is
     * necessary so that a reporter can create a proper report.
     */
#if 0
    if (lmapd->lmap && lmapd->lmap->agent) {
	struct agent *agent = lmapd->lmap->agent;
	if (agent->agent_id && agent->report_agent_id) {
	    setenv("LMAP_AGENT_ID", lmapd->lmap->agent->agent_id, 1);
	}
	if (agent->measurement_point && agent->report_measurement_point) {
	    setenv("LMAP_MEASUREMENT_POINT", agent->measurement_point, 1);
	}
	if (agent->group_id) {
	    setenv("LMAP_GROUP_ID", agent->group_id, 1);
	}
    }
#endif
    /*
     * Save some meta information about the invocation of this action
     * in the action workspace.
     */

    action->last_invocation = t.tv_sec;
    if (lmapd_workspace_action_meta_add_start(schedule, action, task)) {
	exit(EXIT_FAILURE);
    }
    
    /*
     * Setup redirection. Data goes into .data files.
     */

    fd = lmapd_workspace_action_open_data(schedule, action,
					  O_WRONLY | O_CREAT | O_TRUNC);
    if (fd == -1) {
	exit(EXIT_FAILURE);
    }
    if (dup2(fd, STDOUT_FILENO) == -1) {
	lmap_err("failed to redirect stdout");
	exit(EXIT_FAILURE);
    }
    (void) close(fd);
    if (chdir(action->workspace) == -1) {
	lmap_err("failed to change directory");
	exit(EXIT_FAILURE);
    }
    execvp(task->program, argv);
    lmap_err("failed to execute action '%s'", action->name);
    exit(EXIT_FAILURE);
}

static void
schedule_exec(struct lmapd *lmapd, struct schedule *schedule)
{
    struct timeval t;
    struct action *act;
    int rc;

    assert(lmapd);

    if (!schedule || !schedule->name) {
	return;
    }

    // lmap_dbg("executing schedule '%s'", schedule->name);
    
    /* avoid leftover data (possibly due to a crash) from
     * previous runs of an action. */
    for (act = schedule->actions; act; act = act->next) {
	(void) lmapd_workspace_action_clean(lmapd, act);
    }

    event_base_gettimeofday_cached(lmapd->base, &t);
    
    switch (schedule->mode) {
    case LMAP_SCHEDULE_EXEC_MODE_SEQUENTIAL:
	schedule->last_invocation = t.tv_sec;
	schedule->cnt_invocations++;
	if (schedule->actions) {
	    act = schedule->actions;
	    rc = action_exec(lmapd, schedule, act);
	    if (rc == 1) {
		schedule->state = LMAP_SCHEDULE_STATE_RUNNING;
	    }
	}
	break;
    case LMAP_SCHEDULE_EXEC_MODE_PARALLEL:
	schedule->last_invocation = t.tv_sec;
	schedule->cnt_invocations++;
	if (schedule->actions) {
	    for (act = schedule->actions; act; act = act->next) {
		rc = action_exec(lmapd, schedule, act);
		if (rc == 1) {
		    schedule->state = LMAP_SCHEDULE_STATE_RUNNING;
		}
	    }
	}
	break;
    case LMAP_SCHEDULE_EXEC_MODE_PIPELINED:
	lmap_dbg("disabling schedule '%s' (pipelined not yet implemented)",
		 schedule->name);
	schedule->state = LMAP_SCHEDULE_STATE_DISABLED;
	break;
    }
}

static void
action_kill(struct lmapd *lmapd, struct action *action)
{
    assert(lmapd);

    if (!action || !action->name) {
	return;
    }

    if (action->state == LMAP_ACTION_STATE_RUNNING) {
	if (action->pid) {
	    (void) kill(action->pid, SIGTERM);
	}
    }
}

static void
schedule_kill(struct lmapd *lmapd, struct schedule *schedule)
{
    struct action *act;

    assert(lmapd);

    if (!schedule || !schedule->name) {
	return;
    }

    for (act = schedule->actions; act; act = act->next) {
	(void) action_kill(lmapd, act);
    }
}

static int
suppression_start(struct lmapd *lmapd, struct supp *supp)
{
    struct lmap *lmap;
    struct schedule *schedule;
    struct action *action;

    assert(lmapd);

    lmap = lmapd->lmap;
    if (!lmap || !supp || !supp->match || !supp->name) {
	return 0;
    }

    // lmap_dbg("starting suppression %s", supp->name);
    supp->state = LMAP_SUPP_STATE_ACTIVE;

    for (schedule = lmap->schedules; schedule; schedule = schedule->next)
    {
	if (schedule->state == LMAP_SCHEDULE_STATE_DISABLED) {
	    continue;
	}

	if (big_tag_match(supp->match, schedule->suppression_tags)) {
	    // lmap_dbg("suppressing %s", schedule->name);
	    if (schedule->state == LMAP_SCHEDULE_STATE_ENABLED) {
		schedule->state = LMAP_SCHEDULE_STATE_SUPPRESSED;
	    }
	    if (supp->flags & LMAP_SUPP_FLAG_STOP_RUNNING_SET) {
		schedule->flags |= LMAP_SCHEDULE_FLAG_STOP_RUNNING;
	    }
	    schedule->cnt_active_suppressions++;
	}

	for (action = schedule->actions; action; action = action->next) {
	    if (action->state == LMAP_ACTION_STATE_DISABLED) {
		continue;
	    }

	    if (schedule->flags & LMAP_SCHEDULE_FLAG_STOP_RUNNING) {
		action_kill(lmapd, action);
	    }
	
	    if (big_tag_match(supp->match, action->suppression_tags)) {
		// lmap_dbg("suppressing %s", action->name);
		if (action->state == LMAP_ACTION_STATE_ENABLED) {
		    action->state = LMAP_ACTION_STATE_SUPPRESSED;
		}
		if (action->state == LMAP_ACTION_STATE_RUNNING
		    && ! (schedule->flags & LMAP_SCHEDULE_FLAG_STOP_RUNNING)
		    && supp->flags & LMAP_SUPP_FLAG_STOP_RUNNING_SET) {
		    action_kill(lmapd, action);
		    action->state = LMAP_ACTION_STATE_SUPPRESSED;
		}
		action->cnt_active_suppressions++;
	    }
	}
    }

    return 0;
}

static int
suppression_end(struct lmapd *lmapd, struct supp *supp)
{
    struct lmap *lmap;
    struct schedule *schedule;
    struct action *action;

    assert(lmapd);

    lmap = lmapd->lmap;
    if (!lmap || !supp || !supp->match || !supp->name) {
	return 0;
    }

    // lmap_dbg("ending suppression %s", supp->name);
    supp->state = LMAP_SUPP_STATE_ENABLED;

    for (schedule = lmap->schedules; schedule; schedule = schedule->next)
    {
	if (schedule->state == LMAP_SCHEDULE_STATE_DISABLED) {
	    continue;
	}
	
	if (big_tag_match(supp->match, schedule->suppression_tags)) {
	    // lmap_dbg("unsuppressing %s", schedule->name);
	    if (schedule->cnt_active_suppressions) {
		schedule->cnt_active_suppressions--;
	    }
	    if (schedule->cnt_active_suppressions == 0) {
		if (schedule->state == LMAP_SCHEDULE_STATE_SUPPRESSED) {
		    schedule->state = LMAP_SCHEDULE_STATE_ENABLED;
		}
	    }
	}
	
	for (action = schedule->actions; action; action = action->next) {
	    if (action->state == LMAP_ACTION_STATE_DISABLED) {
		continue;
	    }
	    if (big_tag_match(supp->match, action->suppression_tags)) {
		// lmap_dbg("unsuppressing %s", action->name);
		if (action->cnt_active_suppressions) {
		    action->cnt_active_suppressions--;
		}
		if (action->cnt_active_suppressions == 0) {
		    if (action->state == LMAP_ACTION_STATE_SUPPRESSED) {
			action->state = LMAP_ACTION_STATE_ENABLED;
		    }
		}
	    }
	}
    }

    return 0;
}

/**
 * @brief Callback called from the event loop
 *
 * Function which is executed by the event loop periodically. It calls
 * waitpid() to see if any children changed their status. Does all the
 * moving of files from action to destination. Start any subsequent
 * actions in sequential schedules.
 *
 * @param lmapd pointer to a struct lmapd
 */

void
lmapd_cleanup(struct lmapd *lmapd)
{
    pid_t pid;
    int status, failed, succeeded;
    struct lmap *lmap;
    struct timeval t;
    struct action *action;
    struct schedule *schedule;
    struct tag *tag;

    assert(lmapd);
    lmap = lmapd->lmap;
    if (! lmap) {
	return;
    }
    
    event_base_gettimeofday_cached(lmapd->base, &t);

    while (1) {
	pid = waitpid(0, &status, WNOHANG);
	if (pid == 0 || pid == -1) {
	    return;
	}

	if (!WIFEXITED(status) && !WIFSIGNALED(status)) {
	    continue;
	}

	action = find_action_by_pid(lmap, pid);
	if (! action) {
	    lmap_dbg("ignoring pid '%d'", pid);
	    continue;
	}
	schedule = find_schedule_by_pid(lmap, pid);
	if (! schedule) {
	    lmap_dbg("ignoring pid '%d'", pid);
	    continue;
	}

	action->pid = 0;
	action->state = LMAP_ACTION_STATE_ENABLED;
	action->last_completion = t.tv_sec;
	if (WIFEXITED(status)) {
	    action->last_status = WEXITSTATUS(status);
	}
	if (WIFSIGNALED(status)) {
	    action->last_status = -WTERMSIG(status);
	}

	if (action->last_status != 0) {
	    action->last_failed_completion = action->last_completion;
	    action->last_failed_status = action->last_status;
	    action->cnt_failures++;
	}

	/*
	 * Save some meta information about the completion of this
	 * action in the action workspace.
	 */

	(void) lmapd_workspace_action_meta_add_end(schedule, action);

	/*
	 * Move the results to the destinations and afterwards cleanup
	 * the workspace.
	 */

	if (action->last_status == 0 && action->destinations) {
	    for (tag = action->destinations; tag; tag = tag->next) {
		struct schedule *dst = lmap_find_schedule(lmap, tag->tag);
		if (dst) {
		    (void) lmapd_workspace_action_move(lmapd, schedule, action, dst);
		}
	    }
	}
	(void) lmapd_workspace_action_clean(lmapd, action);

	/*
	 * Is there any subsequent action in a sequential schedule?
	 * If so, execute the next action in sequence except when the
	 * schedule got meanwhile suppressed and the stop all running
	 * flag is set.
	 */

	if (action->next && schedule
	    && schedule->mode == LMAP_SCHEDULE_EXEC_MODE_SEQUENTIAL) {
	    if (schedule->state != LMAP_SCHEDULE_STATE_SUPPRESSED
		&& ! (schedule->flags & LMAP_SCHEDULE_FLAG_STOP_RUNNING)) {
		(void) action_exec(lmapd, schedule, action->next);
	    }
	}

	/*
	 * Change schedule state back to enabled if all actions have
	 * left the running state.  If at least one action was executed
	 * and every action returned success, clean up the schedule
	 * input processing queue.
	 */

	if (schedule->state == LMAP_SCHEDULE_STATE_RUNNING) {
	    schedule->state = LMAP_SCHEDULE_STATE_ENABLED;
	    if (schedule->cnt_active_suppressions) {
		schedule->state = LMAP_SCHEDULE_STATE_SUPPRESSED;
	    }
	    succeeded = failed = 0;
	    for (action = schedule->actions; action; action = action->next) {
		if (action->state == LMAP_ACTION_STATE_RUNNING) {
		    schedule->state = LMAP_SCHEDULE_STATE_RUNNING;
		}
		if (action->last_status) {
		    failed = 1;
		} else {
		    succeeded = 1;
		}
	    }
	    if (schedule->state != LMAP_SCHEDULE_STATE_RUNNING) {
		if (failed) {
		    schedule->cnt_failures++;
		} else if (succeeded) {
		    /* there was at least one action, and none failed */
		    lmapd_workspace_schedule_clean(lmapd, schedule);
		}
	    }
	}
    }
}

/**
 * @brief Callback called from the event loop
 *
 * Function which is executed by the event loop when an event
 * fires. It loops through the existing schedules and checks which one
 * needs to be executed.
 *
 * @param lmapd pointer to a struct lmapd
 */

static void
execute_cb(struct lmapd *lmapd, struct event *event)
{
    struct schedule *sched;
    
    assert(lmapd && event);

    if (! lmapd->lmap) {
	return;
    }

    for (sched = lmapd->lmap->schedules; sched; sched = sched->next) {
	
	if (sched->state == LMAP_SCHEDULE_STATE_DISABLED) {
	    goto next;
	}
	
	if (! sched->name) {
	    lmap_err("disabling unnamed schedule");
	    sched->state = LMAP_SCHEDULE_STATE_DISABLED;
	    goto next;
	}

	if (sched->start && !strcmp(sched->start, event->name)) {
	    if (sched->state == LMAP_SCHEDULE_STATE_SUPPRESSED) {
		sched->cnt_suppressions++;
		goto next;
	    }
	    if (sched->state == LMAP_SCHEDULE_STATE_RUNNING) {
		lmap_wrn("schedule '%s' still running - skipping", sched->name);
		sched->cnt_overlaps++;
		goto next;
	    }

	    sched->cycle_number = 0;
	    if (event->flags & LMAP_EVENT_FLAG_CYCLE_INTERVAL_SET
		&& event->cycle_interval) {
		struct timeval t;
		event_base_gettimeofday_cached(lmapd->base, &t);
		sched->cycle_number = (t.tv_sec / event->cycle_interval) * event->cycle_interval;
	    }

	    lmapd_workspace_schedule_move(lmapd, sched);
	    schedule_exec(lmapd, sched);
	    if (event->type == LMAP_EVENT_TYPE_ONE_OFF
		|| event->type == LMAP_EVENT_TYPE_IMMEDIATE
		|| event->type == LMAP_EVENT_TYPE_STARTUP) {
		sched->state = LMAP_SCHEDULE_STATE_DISABLED;
	    }
	}

    next:

	if (sched->end && !strcmp(sched->end, event->name)) {
	    schedule_kill(lmapd, sched);
	}
    }
}

/**
 * @brief Callback called from the event loop
 *
 * Function which is executed by the event loop when an event
 * fires. It loops through the existing suppressions and checks which
 * suppressions need to be actived or deactived.
 *
 * @param lmapd pointer to a struct lmapd
 */

static void
suppress_cb(struct lmapd *lmapd, struct event *event)
{
    struct supp *supp;
    
    assert(lmapd && event);
    
    if (! lmapd->lmap) {
	return;
    }
    
    for (supp = lmapd->lmap->supps; supp; supp = supp->next) {
	
	if (supp->state == LMAP_SUPP_STATE_DISABLED) {
	    continue;
	}
	
	if (! supp->name) {
	    lmap_err("disabling unnamed suppression");
	    supp->state = LMAP_SUPP_STATE_DISABLED;
 	    continue;
	}
	
 	if (supp->start && !strcmp(supp->start, event->name)) {
	    if (supp->state == LMAP_SUPP_STATE_ENABLED) {
		suppression_start(lmapd, supp);
	    } else {
		lmap_wrn("suppression '%s' not enabled - skipping",
			 supp->name);
	    }
	}
	
	if (supp->end && !strcmp(supp->end, event->name)) {
	    if (supp->state == LMAP_SUPP_STATE_ACTIVE) {
		suppression_end(lmapd, supp);
	    } else 
		lmap_wrn("suppression '%s' not active - skipping",
			 supp->name);
	}
    }
}

/**
 * @brief Callback called from the event loop
 *
 * This function is executed when the event passed in the context
 * fires.
 *
 * @param fd unused
 * @param events unused
 * @param context pointer to a struct event
 */

static void
fire_cb(evutil_socket_t fd, short events, void *context)
{
    struct event *event = (struct event *) context;

    (void) fd;
    (void) events;

    assert(event && event->lmapd);

    suppress_cb(event->lmapd, event);
    execute_cb(event->lmapd, event);
    
    event_free(event->fire_event);
    event->fire_event = NULL;
}

static void
trigger_periodic_cb(evutil_socket_t fd, short events, void *context)
{
    struct event *event = (struct event *) context;
    struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };
    struct timeval t;

    (void) fd;
    (void) events;

    assert(event && event->lmapd);

    event_base_gettimeofday_cached(event->lmapd->base, &t);
    if (event->flags & LMAP_EVENT_FLAG_END_SET) {
	if (t.tv_sec > event->end) {
	    /* XXX disable related schedules / suppressions */
	    lmap_wrn("event '%s' ending", event->name);
	    event_free(event->trigger_event);
	    event->trigger_event = NULL;
	    return;
	}
    }

    add_random_spread(event, &tv);
    event_gaga(event, &event->fire_event, EV_TIMEOUT, fire_cb, &tv);
}

static void
trigger_calendar_cb(evutil_socket_t fd, short events, void *context)
{
    struct event *event = (struct event *) context;
    struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };
    struct timeval t;
    int match;
    
    (void) fd;
    (void) events;

    assert(event && event->lmapd);

    event_base_gettimeofday_cached(event->lmapd->base, &t);
    if (event->flags & LMAP_EVENT_FLAG_END_SET) {
	if (t.tv_sec > event->end) {
	    /* XXX disable related schedules / suppressions */
	    lmap_wrn("event '%s' ending", event->name);
	    event_free(event->trigger_event);
	    event->trigger_event = NULL;
	    return;
	}
    }

    match = lmap_event_calendar_match(event, &t.tv_sec);
    if (match < 0) {
	lmap_err("shutting down '%s'", event->name);
	event_free(event->trigger_event);
	event->trigger_event = NULL;
	return;
    }
    
    if (match == 0) {
	tv.tv_sec = 1;
	event_add(event->trigger_event, &tv);
	return;
    }
    
    add_random_spread(event, &tv);
    event_gaga(event, &event->fire_event, EV_TIMEOUT, fire_cb, &tv);
    
    tv.tv_sec = match;
    event_add(event->trigger_event, &tv);
}

static void
startup_cb(evutil_socket_t fd, short events, void *context)
{
    struct event *event = (struct event *) context;
    struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };

    (void) fd;
    (void) events;

    switch (event->type) {
    case LMAP_EVENT_TYPE_PERIODIC:
	tv.tv_sec = event->interval;
	event_gaga(event, &event->trigger_event, EV_PERSIST, trigger_periodic_cb, &tv);
	trigger_periodic_cb(-1, 0, event);
	break;
    case LMAP_EVENT_TYPE_CALENDAR:
	event_gaga(event, &event->trigger_event, EV_TIMEOUT, trigger_calendar_cb, &tv);
	break;
    default:
	break;
    }

    event_free(event->start_event);
    event->start_event = NULL;
}

/**
 * @brief Create and event loop and execute the schedules
 *
 * This function creates an event loop and registeres handlers to take
 * care of signals and to periodically check whether any schedules
 * need to be executed. The event loop usually runs until a SIGHUP or
 * SIGINT signal has been received. Upon receiving SIGUSR1 and
 * SIGUSR2, state and config information is written to files located
 * in the $prefix/var/run directory.
 *
 * @param lmapd pointer to the struct lmapd
 * @return 0 on success, -1 on error
 */

int
lmapd_run(struct lmapd *lmapd)
{
    int i, ret;
    struct timeval one_sec = { .tv_sec = 1, .tv_usec = 0 };

    struct {
	char *name;
	int signum;
	void (*func)(evutil_socket_t sig, short events, void *context);
	struct event *event;
    } tab[] = {
	{ "SIGINT",	SIGINT,		lmapd_sigint_cb,	NULL },
	{ "SIGTERM",	SIGTERM,	lmapd_sigterm_cb,	NULL },
	{ "SIGCHLD",	SIGCHLD,	lmapd_sigchld_cb,	NULL },
	{ "SIGHUP",	SIGHUP,		lmapd_sighub_cb,	NULL },
	{ "SIGUSR1",	SIGUSR1,	lmapd_sigusr1_cb,	NULL },
	{ "SIGUSR2",	SIGUSR2,	lmapd_sigusr2_cb,	NULL },
	{ NULL,		0,		NULL,			NULL }
    };

    lmapd->base = event_base_new();
    if (! lmapd->base) {
	lmap_err("failed to initialize event base - exiting...");
	return -1;
    }
    
    /*
     * Register all event callbacks...
     */
    
    for (i = 0; tab[i].name; i++) {
	if (tab[i].signum) {
	    tab[i].event = evsignal_new(lmapd->base,
					tab[i].signum, tab[i].func, lmapd);
	    if (!tab[i].event || evsignal_add(tab[i].event, NULL) < 0) {
		lmap_err("failed to create/add %s event", tab[i].name);
	    }
	} else {
	    tab[i].event = event_new(lmapd->base,
				     -1, EV_PERSIST, tab[i].func, lmapd);
	    if (!tab[i].event || event_add(tab[i].event, &one_sec) < 0) {
		lmap_err("failed to create/add %s event", tab[i].name);
	    }
	}
    }

    if (lmapd->lmap) {
	struct event *event;
	time_t now = time(NULL);
	for (event = lmapd->lmap->events; event; event = event->next) {
	    struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };
	    if (! event->name) {
		continue;
	    }

	    {
		/* skip events that are not used by anyone */
		struct schedule *sched;
		struct supp *supp;
		int used = 0;
		
		for (sched = lmapd->lmap->schedules; !used && sched; sched = sched->next) {
		    if (sched->start && ! strcmp(sched->start, event->name)) {
			used = 1;
		    }
		    if (sched->end && ! strcmp(sched->end, event->name)) {
			used = 1;
		    }
		}
		
		for (supp = lmapd->lmap->supps; !used && supp; supp = supp->next) {
		    if (supp->start && ! strcmp(supp->start, event->name)) {
			used = 1;
		    }
		    if (supp->end && ! strcmp(supp->end, event->name)) {
			used = 1;
		    }
		}
		if (! used) {
		    lmap_wrn("event '%s' is not used - skipping", event->name);
		    continue;
		}
	    }
	    
	    event->lmapd = lmapd;	/* this avoids a new data structure */
	    switch (event->type) {
	    case LMAP_EVENT_TYPE_PERIODIC:
		if (event->flags & LMAP_EVENT_FLAG_END_SET) {
		    if (now > event->end) {
			lmap_wrn("event '%s' ended in the past", event->name);
			break;
		    }
		}
		if (event->flags & LMAP_EVENT_FLAG_START_SET) {
		    if (now > event->start) {
			uint32_t delta = (now - event->start) / event->interval;
			tv.tv_sec = (event->start + (delta + 1) * event->interval) - now;
		    } else {
			tv.tv_sec = event->start - now;
		    }
		}
		event_gaga(event, &event->start_event, EV_TIMEOUT, startup_cb, &tv);
		break;

	    case LMAP_EVENT_TYPE_CALENDAR:
		if (event->flags & LMAP_EVENT_FLAG_END_SET) {
		    if (now > event->end) {
			lmap_wrn("event '%s' ended in the past", event->name);
			break;
		    }
		}
		event_gaga(event, &event->start_event, EV_TIMEOUT, startup_cb, &tv);
		break;

	    case LMAP_EVENT_TYPE_ONE_OFF:
		if (now < event->start) {
		    lmap_wrn("event '%s' is in the past", event->name);
		    break;
		}
		tv.tv_sec = event->start-now;
		add_random_spread(event, &tv);
		event_gaga(event, &event->fire_event, EV_TIMEOUT, fire_cb, &tv);
		break;
		
	    case LMAP_EVENT_TYPE_STARTUP:
	    case LMAP_EVENT_TYPE_IMMEDIATE:
		add_random_spread(event, &tv);
		event_gaga(event, &event->fire_event, EV_TIMEOUT, fire_cb, &tv);
		break;
		
	    default:
		lmap_wrn("ignoring event '%s' (not implemented)", event->name);
		break;
	    }
	}
    }
    
    lmap_dbg("event loop starting");
    ret = event_base_dispatch(lmapd->base);
    if (ret != 0) {
	lmap_err("event loop failed");
    }
    lmap_dbg("event loop finished");

    /*
     * Cleanup all events and the event loop base.
     */

    if (lmapd->lmap) {
	struct event *event;
	for (event = lmapd->lmap->events; event; event = event->next) {
	    if (event->start_event) {
		event_free(event->start_event);
		event->start_event = NULL;
	    }
	    if (event->trigger_event) {
		event_free(event->trigger_event);
		event->trigger_event = NULL;
	    }
	    if (event->fire_event) {
		event_free(event->fire_event);
		event->fire_event = NULL;
	    }
	}
    }
    
    for (i = 0; tab[i].name; i++) {
	if (tab[i].event) {
	    event_free(tab[i].event);
	}
    }
    event_base_free(lmapd->base);

    /*
     * Cleanup the lmap data model but keep the lmapd daemon state (in
     * case we do a restart).
     */
    
    if (lmapd->lmap) {
	lmap_free(lmapd->lmap);
	lmapd->lmap = NULL;
    }
    
    return (ret == 0) ? 0 : -1;
}

void
lmapd_killall(struct lmapd *lmapd)
{
    struct schedule *sched;

    assert(lmapd);

    if (! lmapd->lmap) {
	return;
    }
    
    for (sched = lmapd->lmap->schedules; sched; sched = sched->next) {
	schedule_kill(lmapd, sched);
    }
}

void
lmapd_stop(struct lmapd *lmapd)
{
    lmapd->flags &= ~LMAPD_FLAG_RESTART;
    lmapd_killall(lmapd);
    if (event_base_loopbreak(lmapd->base) < 0) {
	lmap_err("failed to break the event loop");
    }
}

void
lmapd_restart(struct lmapd *lmapd)
{
    lmapd->flags |= LMAPD_FLAG_RESTART;
    lmapd_killall(lmapd);
    if (event_base_loopbreak(lmapd->base) < 0) {
	lmap_err("failed to break the event loop");
    }
}
