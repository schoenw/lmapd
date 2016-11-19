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

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdio.h>
#include <inttypes.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lmap.h"
#include "lmapd.h"
#include "utils.h"

#define UNUSED(x) (void)(x)

static void *
xcalloc(size_t count, size_t size, const char *func)
{
    char *p = calloc(count, size);
    if (!p) {
        lmap_log(LOG_ERR, func, "failed to allocate memory");
    }
    return p;
}

static void
xfree(void *ptr)
{
    if (ptr) {
        free(ptr);
    }
}

static int
set_string(char **dp, const char *s, const char *func)
{
    xfree(*dp);
    *dp = NULL;
    if (s) {
        *dp = strdup(s);
	if (! *dp) {
	    lmap_log(LOG_ERR, func, "failed to allocate memory");
	    return -1;
	}
    }
    return 0;
}

#if 0
static int
set_yang_identifier(char **dp, const char *s, const char *func)
{
    int i;

    if (s) {
	if (s[0] != '_' && ! isalpha(s[0])) {
	error:
	    lmap_err("illegal yang-identifier value '%s'", s);
	    return -1;
	}
	
	for (i = 0; s[i]; i++) {
	    if (!isalpha(s[i]) && !isdigit(s[i])
		&& s[i] != '_' && s[i] != '-' && s[i] != '.') {
		goto error;
	    }
	}
	
	if (strncasecmp("xml", s, 3) == 0) {
	    goto error;
	}
    }
    
    return set_string(dp, s, func);
}
#endif

static int
set_lmap_identifier(char **dp, const char *s, const char *func)
{
    int i;
    const char safe[] = "-.,_";

    if (s) {
	if (! s[0]) {
	    lmap_err("illegal lmap-identifier value '%s'", s);
	    return -1;
	}

	for (i = 0; s[i]; i++) {
	    if (!isalnum(s[i]) && !strchr(safe, s[i])) {
		lmap_wrn("potentially unsafe character '%c' in '%s'", s[i], s);
	    }
	}
    }
    
    return set_string(dp, s, func);
}

static int
set_boolean(int *dst, const char *value, const char *func)
{
    if (strcmp("true", value) == 0) {
        *dst = 1;
    } else if (strcmp("false", value) == 0) {
        *dst = 0;
    } else {
	lmap_log(LOG_ERR, func, "illegal boolean value '%s'", value);
        return -1;
    }
    return 0;
}

static int
set_tag(char **dp, const char *s, const char *func)
{
    if (s && strlen(s) == 0) {
	lmap_log(LOG_ERR, func, "illegal zero-length tag '%s'", s);
	return -1;
    }
    return set_string(dp, s, func);
}

static int
set_int32(int32_t *ip, const char *s, const char *func)
{
    intmax_t i;
    char *end;

    i = strtoimax(s, &end, 10);
    if (*end || i > INT32_MAX) {
	lmap_err("illegal int32 value '%s'", s);
	return -1;
    }
    *ip = i;
    return 0;
}

static int
set_uint32(uint32_t *up, const char *s, const char *func)
{
    uintmax_t u;
    char *end;

    u = strtoumax(s, &end, 10);
    if (*end || u > UINT32_MAX) {
	lmap_err("illegal uint32 value '%s'", s);
	return -1;
    }
    *up = u;
    return 0;
}

static int
set_uint64(uint64_t *up, const char *s, const char *func)
{
    uintmax_t u;
    char *end;

    u = strtoumax(s, &end, 10);
    if (*end || u > UINT64_MAX) {
	lmap_err("illegal uint64 value '%s'", s);
	return -1;
    }
    *up = u;
    return 0;
}

static int
set_timezoneoffset(int16_t *ip, const char *s)
{
    size_t len;
    int hours, minutes;

    len = strlen(s);
    
    if (len == 1 && s[0] == 'Z') {
	*ip = 0;
	return 0;
    }

    if ((len != 6)
	|| (s[0] != '-' && s[0] != '+') || !isdigit(s[1]) || !isdigit(s[2])
	|| s[3] != ':' || !isdigit(s[4]) || !isdigit(s[5])) {
	return -1;
    }

    hours = (s[1] - '0') * 10 + (s[2] - '0');
    minutes = (s[4] - '0') * 10 + (s[5] - '0');
    if (hours < -23 || hours > 23 || minutes < 0 || minutes > 59) {
	return -1;
    }

    *ip = hours * 60 + minutes;
    if (s[0] == '-') {
	*ip *= -1;
    }
    return 0;
}

static int
set_dateandtime(time_t *tp, const char *s, const char *func)
{
    struct tm tm;
    time_t t;
    char *end;
    int16_t offset;
    extern long timezone;

    /*
     * Do not use %z in strptime() since this is not portable.  It
     * does what you expect on BSD like systems but not on Linux
     * (glibc) like systems. The only reliable and portable way to
     * process time zone offsets seems to be to do roll our own code.
     */

    memset(&tm, 0, sizeof(struct tm));
    end = strptime(s, "%Y-%m-%dT%T", &tm);
    if (end == NULL) {
    error:
	lmap_log(LOG_ERR, func, "illegal date and time value '%s'", s);
	return -1;
    }

    /*
     * Parse the timezone offset, which must be either 'Z' or in the
     * format [+-]HH:MM. Note that strptime() also fails to parse this
     * correctly - strptime() does not like the colon, which is
     * however required by RFC 3339 and YANG's date-and-time.
     */

    if (set_timezoneoffset(&offset, end) != 0) {
	goto error;
    }

    t = mktime(&tm);
    if (t == -1) {
	lmap_log(LOG_ERR, func, "time conversion failed");
	return -1;
    }
    *tp = (t - (offset * 60) + timezone);
    return 0;

}

static int
add_tag(struct tag **tagp, const char *value, const char *func)
{
    struct tag *tail;
    struct tag *tag;

    tag = lmap_tag_new();
    if (! tag) {
	return -1;
    }
    lmap_tag_set_tag(tag, value);

    if (!*tagp) {
	*tagp = tag;
	return 0;
    }

    for (tail = *tagp; tail; ) {
	if (strcmp(tail->tag, value) == 0) {
	    lmap_tag_free(tag);
	    lmap_log(LOG_WARNING, func, "ignoring duplicate tag '%s'", value);
	    return -1;
	}
	if (tail->next) {
	    tail = tail->next;
	} else {
	    tail->next = tag;
	    break;
	}
    }
    return 0;
}

static void
free_all_tags(struct tag *tags)
{
    while (tags) {
	struct tag *old = tags;
	tags = tags->next;
	lmap_tag_free(old);
    }
}

static int
add_option(struct option **optionp, struct option *option, const char *func)
{
    struct option **tail = optionp;
    struct option *cur;

    if (! option->id) {
	lmap_log(LOG_ERR, func, "unnamed option");
	return -1;
    }

    while (*tail != NULL) {
	cur = *tail;
	if (cur) {
	    if (! strcmp(cur->id, option->id)) {
		lmap_log(LOG_ERR, func, "duplicate option '%s'", option->name);
		return -1;
	    }
	}
	tail = &((*tail)->next);
    }
    *tail = option;

    return 0;
}

static void
free_all_options(struct option *options)
{
    while (options) {
	struct option *old = options;
	options = options->next;
	lmap_option_free(old);
    }
}

struct event *
lmap_find_event(struct lmap *lmap, const char *name)
{
    struct event *event;

    if (!lmap || !name) {
	return NULL;
    }
    
    for (event = lmap->events; event; event = event->next) {
	if (event->name && strcmp(event->name, name) == 0) {
	    return event;
	}
    }

    return NULL;
}

struct task *
lmap_find_task(struct lmap *lmap, const char *name)
{
    struct task *task;

    if (!lmap || !name) {
	return NULL;
    }
    
    for (task = lmap->tasks; task; task = task->next) {
	if (task->name && strcmp(task->name, name) == 0) {
	    return task;
	}
    }

    return NULL;
}

struct schedule *
lmap_find_schedule(struct lmap *lmap, const char *name)
{
    struct schedule *schedule;

    if (! lmap || !name) {
	return NULL;
    }
    
    for (schedule = lmap->schedules; schedule; schedule = schedule->next) {
	if (schedule->name && strcmp(schedule->name, name) == 0) {
	    return schedule;
	}
    }

    return NULL;
}

/*
 * struct lmap functions...
 */

/**
 * @brief Allocates and initializes a struct lmap
 *
 * Allocates and initializes a struct lmap
 *
 * @return pointer to a struct lmap on success, NULL on error
 */

struct lmap*
lmap_new()
{
    struct lmap *lmap;

    lmap = (struct lmap*) xcalloc(1, sizeof(struct lmap), __FUNCTION__);
    return lmap;
}

/**
 * @brief Deallocates a struct lmap
 * @details Deallocates a struct lmap and all structures contained in it
 * @param lmap The struct lmap to deallocate
 * @return void
 */

void
lmap_free(struct lmap *lmap)
{
    if (lmap) {
        if (lmap->agent) {
	    lmap_agent_free(lmap->agent);
	}

	while (lmap->schedules) {
	    struct schedule *next = lmap->schedules->next;
	    lmap_schedule_free(lmap->schedules);
	    lmap->schedules = next;
	}
	
	while (lmap->supps) {
	    struct supp *next = lmap->supps->next;
	    lmap_supp_free(lmap->supps);
	    lmap->supps = next;
	}
	
	while (lmap->tasks) {
	    struct task *next = lmap->tasks->next;
	    lmap_task_free(lmap->tasks);
	    lmap->tasks = next;
	}
	
	while (lmap->events) {
	    struct event *next = lmap->events->next;
	    lmap_event_free(lmap->events);
	    lmap->events = next;
	}
	
	while (lmap->results) {
	    struct result *next = lmap->results->next;
	    lmap_result_free(lmap->results);
	    lmap->results = next;
	}
	
	xfree(lmap);
    }
}

int
lmap_valid(struct lmap *lmap)
{
    int valid = 1;
    struct supp *supp;
    struct task *task;
    struct event *event;
    struct schedule *schedule;

    if (lmap->agent) {
	valid &= lmap_agent_valid(lmap, lmap->agent);
    }

    for (supp = lmap->supps; supp; supp = supp->next) {
	valid &= lmap_supp_valid(lmap, supp);
    }

    for (task = lmap->tasks; task; task = task->next) {
	valid &= lmap_task_valid(lmap, task);
    }

    for (event = lmap->events; event; event = event->next) {
	valid &= lmap_event_valid(lmap, event);
    }

    for (schedule = lmap->schedules; schedule; schedule = schedule->next) {
	valid &= lmap_schedule_valid(lmap, schedule);
    }

    return valid;
}

int
lmap_add_schedule(struct lmap *lmap, struct schedule *schedule)
{
    struct schedule **tail = &lmap->schedules;
    struct schedule *cur = NULL;

    if (! schedule->name) {
	lmap_err("unnamed schedule");
	return -1;
    }

    while (*tail != NULL) {
	cur = *tail;
	if (cur) {
	    if (! strcmp(cur->name, schedule->name)) {
		lmap_err("duplicate schedule '%s'", schedule->name);
		return -1;
	    }
	}
	tail = &((*tail)->next);
    }
    *tail = schedule;

    return 0;
}

int
lmap_add_supp(struct lmap *lmap, struct supp *supp)
{
    struct supp **tail = &lmap->supps;
    struct supp *cur = NULL;

    if (! supp->name) {
	lmap_err("unnamed suppression");
	return -1;
    }

    while (*tail != NULL) {
	cur = *tail;
	if (cur) {
	    if (! strcmp(cur->name, supp->name)) {
		lmap_err("duplicate suppression '%s'", supp->name);
		return -1;
	    }
	}
	tail = &((*tail)->next);
    }
    *tail = supp;

    return 0;
}

int
lmap_add_task(struct lmap *lmap, struct task *task)
{
    struct task **tail = &lmap->tasks;
    struct task *cur = NULL;

    if (! task->name) {
	lmap_err("unnamed task");
	return -1;
    }

    while (*tail != NULL) {
	cur = *tail;
	if (cur) {
	    if (! strcmp(cur->name, task->name)) {
		lmap_err("duplicate task '%s'", task->name);
		return -1;
	    }
	}
	tail = &((*tail)->next);
    }
    *tail = task;

    return 0;
}

int
lmap_add_event(struct lmap *lmap, struct event *event)
{
    struct event **tail = &lmap->events;
    struct event *cur = NULL;

    if (! event->name) {
	lmap_err("unnamed event");
	return -1;
    }

    while (*tail != NULL) {
	cur = *tail;
	if (cur) {
	    if (! strcmp(cur->name, event->name)) {
		lmap_err("duplicate event '%s'", event->name);
		return -1;
	    }
	}
	tail = &((*tail)->next);
    }
    *tail = event;

    return 0;
}

int
lmap_add_result(struct lmap *lmap, struct result *res)
{
    struct result **tail = &lmap->results;

    while (*tail != NULL) {
	tail = &((*tail)->next);
    }
    *tail = res;

    return 0;
}

/*
 * struct agent functions...
 */

struct agent*
lmap_agent_new()
{
    struct agent *agent;

    agent = (struct agent*) xcalloc(1, sizeof(struct agent), __FUNCTION__);
    if (agent) {
        agent->controller_timeout = 604800;	/* one week in seconds */
    }
    return agent;
}

void
lmap_agent_free(struct agent *agent)
{
    if (agent) {
        xfree(agent->agent_id);
	xfree(agent->group_id);
	xfree(agent->measurement_point);
	xfree(agent->version);
	xfree(agent);
    }
}

int lmap_agent_valid(struct lmap *lmap, struct agent *agent)
{
    int valid = 1;

    UNUSED(lmap);

    if (agent->report_agent_id && ! agent->agent_id) {
	lmap_err("report-agent-id requires an agent-id");
	valid = 0;
    }

    if (agent->report_group_id && ! agent->group_id) {
	lmap_err("report-group-id requires a group-id");
	valid = 0;
    }

    if (agent->report_measurement_point && ! agent->measurement_point) {
	lmap_err("report-measurement-point requires a measurement-point");
	valid = 0;
    }

    return valid;
}

int
lmap_agent_set_agent_id(struct agent *agent, const char *value)
{
    int i;
    const char *p;

    if (value) {
	/* check that the value is a valid uuid */
	for (i = 0, p = value; i <= 36; i++, p++) {
	    if ((i == 8) || (i == 13) || (i == 18) || (i == 23)) {
		if (*p == '-')
		    continue;
		else
		    goto error;
	    }
	    if (i== 36 && *p == 0) {
		continue;
	    }
	    if (!isxdigit(*p)) {
	    error:
		lmap_err("illegal uuid value '%s'", value);
		return -1;
	    }
	}
    }

    return set_string(&agent->agent_id, value, __FUNCTION__);
}

int
lmap_agent_set_group_id(struct agent *agent, const char *value)
{
    return set_string(&agent->group_id, value, __FUNCTION__);
}

int
lmap_agent_set_measurement_point(struct agent *agent, const char *value)
{
    return set_string(&agent->measurement_point, value, __FUNCTION__);
}

int
lmap_agent_set_report_agent_id(struct agent *agent, const char *value)
{
    int ret;

    ret = set_boolean(&agent->report_agent_id, value, __FUNCTION__);
    if (ret == 0) {
	agent->flags |= LMAP_AGENT_FLAG_REPORT_AGENT_ID_SET;
    }
    return ret;
}

int
lmap_agent_set_report_group_id(struct agent *agent, const char *value)
{
    int ret;

    ret = set_boolean(&agent->report_group_id, value, __FUNCTION__);
    if (ret == 0) {
	agent->flags |= LMAP_AGENT_FLAG_REPORT_GROUP_ID_SET;
    }
    return ret;
}

int lmap_agent_set_report_measurement_point(struct agent *agent, const char *value)
{
    int ret;
    
    ret = set_boolean(&agent->report_measurement_point, value, __FUNCTION__);
    if (ret == 0) {
	agent->flags |= LMAP_AGENT_FLAG_REPORT_MEASUREMENT_POINT_SET;
    }
    return ret;
}

int lmap_agent_set_controller_timeout(struct agent *agent, const char *value)
{
    int ret;
    
    ret = set_uint32(&agent->controller_timeout, value, __FUNCTION__);
    if (ret == 0) {
	agent->flags |= LMAP_AGENT_FLAG_CONTROLLER_TIMEOUT_SET;
    }
    return ret;
}

int lmap_agent_set_version(struct agent *agent, const char *value)
{
    return set_string(&agent->version, value, __FUNCTION__);
}

int lmap_agent_set_last_started(struct agent *agent, const char *value)
{
    return set_dateandtime(&agent->last_started, value, __FUNCTION__);
}

/*
 * struct registry functions...
 */

struct registry *
lmap_registry_new()
{
    struct registry *registry;

    registry = (struct registry*) xcalloc(1, sizeof(struct registry), __FUNCTION__);
    return registry;
}

void
lmap_registry_free(struct registry *registry)
{
    if (registry) {
	xfree(registry->uri);
	free_all_tags(registry->roles);
	xfree(registry);
    }
}

int
lmap_registry_valid(struct lmap *lmap, struct registry *registry)
{
    int valid = 1;

    UNUSED(lmap);

    if (! registry->uri) {
	lmap_err("registry requires a uri");
	valid = 0;
    }

    return valid;
}

int
lmap_registry_set_uri(struct registry *registry, const char *value)
{
    return set_string(&registry->uri, value, __FUNCTION__);
}

int
lmap_registry_add_role(struct registry *registry, const char *value)
{

    return add_tag(&registry->roles, value, __FUNCTION__);
}

/*
 * struct option functions...
 */

struct option *
lmap_option_new()
{
    struct option *option;

    option = (struct option*) xcalloc(1, sizeof(struct option), __FUNCTION__);
    return option;
}

void
lmap_option_free(struct option *option)
{
    if (option) {
	xfree(option->id);
	xfree(option->name);
	xfree(option->value);
	xfree(option);
    }
}

int
lmap_option_valid(struct lmap *lmap, struct option *option)
{
    int valid = 1;

    UNUSED(lmap);

    if (! option->id) {
	lmap_err("option requires an id");
	valid = 0;
    }

    return valid;
}

int
lmap_option_set_id(struct option *option, const char *value)
{
    return set_lmap_identifier(&option->id, value, __FUNCTION__);
}

int
lmap_option_set_name(struct option *option, const char *value)
{
    return set_string(&option->name, value, __FUNCTION__);
}

int
lmap_option_set_value(struct option *option, const char *value)
{
    return set_string(&option->value, value, __FUNCTION__);
}

/*
 * struct tag functions...
 */

struct tag *
lmap_tag_new()
{
    struct tag *tag;

    tag = (struct tag*) xcalloc(1, sizeof(struct tag), __FUNCTION__);
    return tag;
}

void
lmap_tag_free(struct tag *tag)
{
    if (tag) {
	xfree(tag->tag);
	xfree(tag);
    }
}

int
lmap_tag_valid(struct lmap *lmap, struct tag *tag)
{
    int valid = 1;

    UNUSED(lmap);

    if (! tag->tag) {
	lmap_err("tag requires a value");
	valid = 0;
    }

    return valid;
}

int
lmap_tag_set_tag(struct tag *tag, const char *value)
{
    return set_tag(&tag->tag, value, __FUNCTION__);
}

/*
 * struct supp functions...
 */

struct supp *
lmap_supp_new()
{
    struct supp *supp;

    supp = (struct supp*) xcalloc(1, sizeof(struct supp), __FUNCTION__);
    supp->state = LMAP_SUPP_STATE_ENABLED;
    return supp;
}

void
lmap_supp_free(struct supp *supp)
{
    if (supp) {
	xfree(supp->name);
	xfree(supp->start);
	xfree(supp->end);
	free_all_tags(supp->match);
	xfree(supp);
    }
}

int
lmap_supp_valid(struct lmap *lmap, struct supp *supp)
{
    int valid = 1;

    if (! supp->name) {
	lmap_err("suppression requires a name");
	valid = 0;
    }

    if (supp->start && ! lmap_find_event(lmap, supp->start)) {
	lmap_err("suppression %s%s%srefers to undefined start event '%s'",
		 supp->name ? "'" : "",
		 supp->name ? supp->name : "",
		 supp->name ? "' " : "",
		 supp->start);
	valid = 0;
    }
    if (supp->end && ! lmap_find_event(lmap, supp->end)) {
	lmap_err("suppression %s%s%srefers to undefined end event '%s'",
		 supp->name ? "'" : "",
		 supp->name ? supp->name : "",
		 supp->name ? "' " : "",
		 supp->end);
	valid = 0;
    }

    return valid;
}

int
lmap_supp_set_name(struct supp *supp, const char *value)
{
    return set_lmap_identifier(&supp->name, value, __FUNCTION__);
}

int
lmap_supp_set_start(struct supp *supp, const char *value)
{
    return set_lmap_identifier(&supp->start, value, __FUNCTION__);
}

int
lmap_supp_set_end(struct supp *supp, const char *value)
{
    return set_lmap_identifier(&supp->end, value, __FUNCTION__);
}

int
lmap_supp_add_match(struct supp *supp, const char *value)
{
    return add_tag(&supp->match, value, __FUNCTION__);
}

int
lmap_supp_set_stop_running(struct supp *supp, const char *value)
{
    int ret;
    
    ret = set_boolean(&supp->stop_running, value, __FUNCTION__);
    if (ret == 0) {
	supp->flags |= LMAP_SUPP_FLAG_STOP_RUNNING_SET;
    }
    return ret;
}

int
lmap_supp_set_state(struct supp *supp, const char *value)
{
    if (strcmp("enabled", value) == 0) {
	supp->state = LMAP_SUPP_STATE_ENABLED;
    } else if (strcmp("disabled", value) == 0) {
	supp->state = LMAP_SUPP_STATE_DISABLED;
    } else if (strcmp("active", value) == 0) {
	supp->state = LMAP_SUPP_STATE_ACTIVE;
    } else {
	lmap_err("illegal state '%s'", value);
	return -1;
    }
    return 0;
}

/*
 * struct event functions...
 */

struct event *
lmap_event_new()
{
    struct event *event;

    event = (struct event*) xcalloc(1, sizeof(struct event), __FUNCTION__);
    return event;
}

void
lmap_event_free(struct event *event)
{
    if (event) {
	xfree(event->name);
	xfree(event);
    }
}

int
lmap_event_valid(struct lmap *lmap, struct event *event)
{
    int valid = 1;

    UNUSED(lmap);

    if (! event->name) {
	lmap_err("event requires a name");
	valid = 0;
    }

    if (! event->type) {
	lmap_err("event %s%s%srequires a type",
		 event->name ? "'" : "",
		 event->name ? event->name : "",
		 event->name ? "' " : "");
	valid = 0;
    }

    if (event->type == LMAP_EVENT_TYPE_PERIODIC
	&& ! (event->flags & LMAP_EVENT_FLAG_INTERVAL_SET)) {
	lmap_err("event %s%s%srequires an interval",
		 event->name ? "'" : "",
		 event->name ? event->name : "",
		 event->name ? "' " : "");
	valid = 0;
    }
    if (event->type == LMAP_EVENT_TYPE_CALENDAR) {
	if (event->months == 0) {
	    lmap_err("event %s%s%srequires a month",
		     event->name ? "'" : "",
		     event->name ? event->name : "",
		     event->name ? "' " : "");
	    valid = 0;
	}
	if (event->days_of_month == 0) {
	    lmap_err("event %s%s%srequires a day of month",
		     event->name ? "'" : "",
		     event->name ? event->name : "",
		     event->name ? "' " : "");
	    valid = 0;
	}
	if (event->days_of_week == 0) {
	    lmap_err("event %s%s%srequires a day of week",
		     event->name ? "'" : "",
		     event->name ? event->name : "",
		     event->name ? "' " : "");
	    valid = 0;
	}
	if (event->hours == 0) {
	    lmap_err("event %s%s%srequires an hour",
		     event->name ? "'" : "",
		     event->name ? event->name : "",
		     event->name ? "' " : "");
	    valid = 0;
	}
	if (event->minutes == 0) {
	    lmap_err("event %s%s%srequires a minute",
		     event->name ? "'" : "",
		     event->name ? event->name : "",
		     event->name ? "' " : "");
	    valid = 0;
	}
	if (event->seconds == 0) {
	    lmap_err("event %s%s%srequires a second",
		     event->name ? "'" : "",
		     event->name ? event->name : "",
		     event->name ? "' " : "");
	    valid = 0;
	}
    }
    if (event->type == LMAP_EVENT_TYPE_ONE_OFF
	&& ! (event->flags & LMAP_EVENT_FLAG_START_SET)) {
	lmap_err("event %s%s%srequires a time",
		 event->name ? "'" : "",
		 event->name ? event->name : "",
		 event->name ? "' " : "");
	valid = 0;
    }

    if (event->flags & LMAP_EVENT_FLAG_START_SET
	&& event->flags & LMAP_EVENT_FLAG_END_SET
	&& event->end < event->start) {
	lmap_err("event %s%s%sends before it starts",
		 event->name ? "'" : "",
		 event->name ? event->name : "",
		 event->name ? "' " : "");
	valid = 0;
    }

    return valid;
}

int
lmap_event_set_name(struct event *event, const char *value)
{
    return set_lmap_identifier(&event->name, value, __FUNCTION__);
}

int
lmap_event_set_type(struct event *event, const char *value)
{
    int i;

    struct {
	char *name;
	int type;
    }  tab[] = {
	{ "periodic",			LMAP_EVENT_TYPE_PERIODIC },
	{ "calendar",			LMAP_EVENT_TYPE_CALENDAR },
	{ "one-off",			LMAP_EVENT_TYPE_ONE_OFF },
	{ "immediate",			LMAP_EVENT_TYPE_IMMEDIATE },
	{ "startup",			LMAP_EVENT_TYPE_STARTUP },
	{ "controller-lost",		LMAP_EVENT_TYPE_CONTROLLER_LOST },
	{ "controller-connected",	LMAP_EVENT_TYPE_CONTROLLER_CONNECTED },
	{ NULL, 0 }
    };

    for (i = 0; tab[i].name; i++) {
	if (!strcmp(tab[i].name, value)) {
	    event->type = tab[i].type;
	    break;
	}
    }

    if (! tab[i].name) {
	lmap_err("unknown event type '%s'", value);
	return -1;
    }

    return 0;
}

int
lmap_event_set_interval(struct event *event, const char *value)
{
    int ret;
    
    ret = set_uint32(&event->interval, value, __FUNCTION__);
    if (ret == 0) {
	if (event->interval < 1) {
	    lmap_err("illegal interval value '%s'", value);
	    return -1;
	}
	event->flags |= LMAP_EVENT_FLAG_INTERVAL_SET;
    }
    return ret;
}

int
lmap_event_set_start(struct event *event, const char *value)
{
    int ret;
    
    ret = set_dateandtime(&event->start, value, __FUNCTION__);
    if (ret == 0) {
	event->flags |= LMAP_EVENT_FLAG_START_SET;
    }
    return ret;
}

int
lmap_event_set_end(struct event *event, const char *value)
{
    int ret;
    
    ret = set_dateandtime(&event->end, value, __FUNCTION__);
    if (ret == 0) {
	event->flags |= LMAP_EVENT_FLAG_END_SET;
    }
    return ret;
}

int
lmap_event_set_random_spread(struct event *event, const char *value)
{
    int ret;
    
    ret = set_uint32(&event->random_spread, value, __FUNCTION__);
    if (ret == 0) {
	if (event->random_spread >= RAND_MAX) {
	    lmap_err("random_spread must be smaller than %u", RAND_MAX);
	    return -1;
	} else {
	    event->flags |= LMAP_EVENT_FLAG_RANDOM_SPREAD_SET;
	}
    }
    return ret;
}

int
lmap_event_add_month(struct event *event, const char *value)
{
    int i;
    
    struct {
	char *name;
	uint16_t value;
    } tab[] = {
	{ "*",		UINT16_MAX },
	{ "january",	(1 << 0) },
	{ "february",	(1 << 1) },
	{ "march",	(1 << 2) },
	{ "april",	(1 << 3) },
	{ "may",	(1 << 4) },
	{ "june",	(1 << 5) },
	{ "july",	(1 << 6) },
	{ "august",	(1 << 7) },
	{ "september",	(1 << 8) },
	{ "october",	(1 << 9) },
	{ "november",	(1 << 10) },
	{ "december",	(1 << 11) },
	{ NULL, 0 }
    };

    for (i = 0; tab[i].name; i++) {
	if (! strcmp(tab[i].name, value)) {
	    break;
	}
    }

    if (!tab[i].name) {
	lmap_err("illegal month value '%s'", value);
	return -1;
    }

    event->months |= tab[i].value;
    return 0;
}

int
lmap_event_add_day_of_month(struct event *event, const char *value)
{
    int32_t val;

    if (!strcmp(value, "*")) {
	event->days_of_month = UINT32_MAX;
	return 0;
    }

    if (set_int32(&val, value, __FUNCTION__) == -1 || val < 1 || val > 31) {
	lmap_err("illegal day of month value '%s'", value);
	return -1;
    }

    event->days_of_month |= (1ul << val);
    return 0;
}

int
lmap_event_add_day_of_week(struct event *event, const char *value)
{
    int i;
    
    struct {
	char *name;
	uint8_t value;
    } tab[] = {
	{ "*",		UINT8_MAX },
	{ "monday",	(1 << 0) },
	{ "tuesday",	(1 << 1) },
	{ "wednesday",	(1 << 2) },
	{ "thursday",	(1 << 3) },
	{ "friday",	(1 << 4) },
	{ "saturday",	(1 << 5) },
	{ "sunday",	(1 << 6) },
	{ NULL, 0 }
    };

    for (i = 0; tab[i].name; i++) {
	if (! strcmp(tab[i].name, value)) {
	    break;
	}
    }

    if (!tab[i].name) {
	lmap_err("illegal day of week value '%s'", value);
	return -1;
    }

    event->days_of_week |= tab[i].value;
    return 0;
}

int
lmap_event_add_hour(struct event *event, const char *value)
{
    int32_t val;

    if (!strcmp(value, "*")) {
	event->hours = UINT32_MAX;
	return 0;
    }

    if (set_int32(&val, value, __FUNCTION__) == -1 || val < 0 || val > 23) {
	lmap_err("illegal hour value '%s'", value);
	return -1;
    }

    event->hours |= (1 << val);
    return 0;
}

int
lmap_event_add_minute(struct event *event, const char *value)
{
    int32_t val;

    if (!strcmp(value, "*")) {
	event->minutes = UINT64_MAX;
	return 0;
    }

    if (set_int32(&val, value, __FUNCTION__) == -1 || val < 0 || val > 59) {
	lmap_err("illegal minute value '%s'", value);
	return -1;
    }

    event->minutes |= (1ull << val);
    return 0;
}

int
lmap_event_add_second(struct event *event, const char *value)
{
    int32_t val;

    if (!strcmp(value, "*")) {
	event->seconds = UINT64_MAX;
	return 0;
    }

    if (set_int32(&val, value, __FUNCTION__) == -1 || val < 0 || val > 59) {
	lmap_err("illegal second value '%s'", value);
	return -1;
    }

    event->seconds |= (1ull << val);
    return 0;
}

int
lmap_event_set_timezone_offset(struct event *event, const char *value)
{
    if (set_timezoneoffset(&event->timezone_offset, value) != 0) {
	lmap_err("illegal timezone offset value '%s'", value);
	return -1;
    }
    
    event->flags |= LMAP_EVENT_FLAG_TIMEZONE_OFFSET_SET;
    return 0;
}

int
lmap_event_calendar_match(struct event *event, time_t *now)
{
    struct tm *tm;
    int wday;
    
    if (event->type != LMAP_EVENT_TYPE_CALENDAR) {
	return -1;
    }

    tm = localtime(now);
    if (! tm) {
	lmap_err("failed to obtain localtime");
	return -1;
    }

    if (event->months != UINT16_MAX
	&& !((1 << tm->tm_mon) & event->months)) {
	// lmap_dbg("%s: month does not match", event->name);
	return 0;
    }
    
    if (event->days_of_month != UINT32_MAX
	&&  !(1 << (tm->tm_mday) & event->days_of_month)) {
	// lmap_dbg("%s: day of month does not match", event->name);
	return 0;
    }
    
    /*
     * Weekdays are counted differently, struct tm has the week
     * starting with sunday while our lmap week starts with
     * monday.
     */
    
    wday = (tm->tm_wday == 0) ? 6 : (tm->tm_wday -1);
    if (event->days_of_week != UINT8_MAX
	&&  !(1 << (wday) & event->days_of_week)) {
	// lmap_dbg("%s: day of week does not match", event->name);
	return 0;
    }
    
    if (event->hours != UINT32_MAX
	&&  !(1 << (tm->tm_hour) & event->hours)) {
	// lmap_dbg("%s: hour does not match", event->name);
	return 0;
    }
	
    if (event->minutes != UINT64_MAX
	&&  !(1ull << (tm->tm_min) & event->minutes)) {
	// lmap_dbg("%s: minute does not match", event->name);
	return 0;
    }
    
    if (event->seconds != UINT64_MAX
	&&  !(1ull << (tm->tm_sec) & event->seconds)) {
	// lmap_dbg("%s: second does not match", event->name);
	return 0;
    }

    /* XXX calculate the number of seconds until it is reasonable
       to check again */
    return 1;
}

/*
 * struct task functions...
 */

struct task *
lmap_task_new()
{
    struct task *task;

    task = (struct task*) xcalloc(1, sizeof(struct task), __FUNCTION__);
    return task;
}

void
lmap_task_free(struct task *task)
{
    if (task) {
	xfree(task->name);
	while (task->registries) {
	    struct registry *old = task->registries;
	    task->registries = task->registries->next;
	    lmap_registry_free(old);
	}
	xfree(task->program);
	free_all_options(task->options);
	free_all_tags(task->tags);
	xfree(task);
    }
}

int
lmap_task_valid(struct lmap *lmap, struct task *task)
{
    int valid = 1;

    UNUSED(lmap);

    if (! task->name) {
	lmap_err("task requires a name");
	valid = 0;
    }

    if (! task->program) {
	lmap_err("task %s%s%srequires a program",
		 task->name ? "'" : "",
		 task->name ? task->name : "",
		 task->name ? "' " : "");
	valid = 0;
    }

    return valid;
}

int
lmap_task_set_name(struct task *task, const char *value)
{
    return set_lmap_identifier(&task->name, value, __FUNCTION__);
}

int
lmap_task_set_program(struct task *task, const char *value)
{
    return set_string(&task->program, value, __FUNCTION__);
}

int
lmap_task_add_registry(struct task *task, struct registry *registry)
{
    struct registry **tail = &task->registries;
    struct registry *cur;

    if (! registry->uri) {
	lmap_err("unnamed registry");
	return -1;
    }

    while (*tail != NULL) {
	cur = *tail;
	if (cur) {
	    if (! strcmp(cur->uri, registry->uri)) {
		lmap_err("duplicate registry '%s'", registry->uri);
		return -1;
	    }
	}
	tail = &((*tail)->next);
    }
    *tail = registry;

    return 0;
}

int
lmap_task_add_option(struct task *task, struct option *option)
{
    return add_option(&task->options, option, __FUNCTION__);
}

int
lmap_task_add_tag(struct task *task, const char *value)
{
    return add_tag(&task->tags, value, __FUNCTION__);
}

int
lmap_task_set_suppress_by_default(struct task *task, const char *value)
{
    int ret;
    
    ret = set_boolean(&task->suppress_by_default, value, __FUNCTION__);
    if (ret == 0) {
	task->flags |= LMAP_TASK_FLAG_SUPPRESS_BY_DEFAULT_SET;
    }
    return ret;
}

/*
 * struct schedule functions...
 */

struct schedule *
lmap_schedule_new()
{
    struct schedule *schedule;

    schedule = (struct schedule*) xcalloc(1, sizeof(struct schedule), __FUNCTION__);
    schedule->mode = LMAP_SCHEDULE_EXEC_MODE_PIPELINED;
    schedule->state = LMAP_SCHEDULE_STATE_ENABLED;
    return schedule;
}

void
lmap_schedule_free(struct schedule *schedule)
{
    if (schedule) {
	xfree(schedule->name);
	xfree(schedule->start);
	xfree(schedule->end);
	while (schedule->actions) {
	    struct action *old = schedule->actions;
	    schedule->actions = schedule->actions->next;
	    lmap_action_free(old);
	}
	free_all_tags(schedule->tags);
	free_all_tags(schedule->suppression_tags);
	xfree(schedule->workspace);
	xfree(schedule);
    }
}

int
lmap_schedule_valid(struct lmap *lmap, struct schedule *schedule)
{
    struct action *action;
    int valid = 1;

    UNUSED(lmap);

    if (! schedule->name) {
	lmap_err("schedule requires a name");
	valid = 0;
    }

    if (! schedule->start) {
	lmap_err("schedule %s%s%srequires a start event",
		 schedule->name ? "'" : "",
		 schedule->name ? schedule->name : "",
		 schedule->name ? "' " : "");
	valid = 0;
    }

    if (schedule->start && ! lmap_find_event(lmap, schedule->start)) {
	lmap_err("schedule %s%s%srefers to undefined start event '%s'",
		 schedule->name ? "'" : "",
		 schedule->name ? schedule->name : "",
		 schedule->name ? "' " : "",
		 schedule->start);
	valid = 0;
    }
    if (schedule->end && ! lmap_find_event(lmap, schedule->end)) {
	lmap_err("schedule %s%s%srefers to undefined end event '%s'",
		 schedule->name ? "'" : "",
		 schedule->name ? schedule->name : "",
		 schedule->name ? "' " : "",
		 schedule->start);
	valid = 0;
    }

    for (action = schedule->actions; action; action = action->next) {
	if (! lmap_action_valid(lmap, action)) {
	    valid = 0;
	}
    }

    return valid;
}

int
lmap_schedule_set_name(struct schedule *schedule, const char *value)
{
    return set_lmap_identifier(&schedule->name, value, __FUNCTION__);
}

int
lmap_schedule_set_start(struct schedule *schedule, const char *value)
{
    return set_lmap_identifier(&schedule->start, value, __FUNCTION__);
}

int
lmap_schedule_set_end(struct schedule *schedule, const char *value)
{
    int ret;

    if (schedule->flags & LMAP_SCHEDULE_FLAG_DURATION_SET) {
	schedule->duration = 0;
	schedule->flags &= ~LMAP_SCHEDULE_FLAG_DURATION_SET;
    }

    ret = set_lmap_identifier(&schedule->end, value, __FUNCTION__);
    if (ret == 0) {
	schedule->flags |= LMAP_SCHEDULE_FLAG_END_SET;
    }
    return ret;
}

int
lmap_schedule_set_duration(struct schedule *schedule, const char *value)
{
    int ret;

    if (schedule->flags & LMAP_SCHEDULE_FLAG_END_SET) {
	xfree(schedule->end);
	schedule->end = NULL;
	schedule->flags &= ~LMAP_SCHEDULE_FLAG_END_SET;
    }
    
    ret = set_uint64(&schedule->duration, value, __FUNCTION__);
    if (ret == 0) {
	schedule->flags |= LMAP_SCHEDULE_FLAG_DURATION_SET;
    }
    return ret;
}

int
lmap_schedule_set_exec_mode(struct schedule *schedule, const char *value)
{
    if (strcmp("sequential", value) == 0) {
	schedule->mode = LMAP_SCHEDULE_EXEC_MODE_SEQUENTIAL;
    } else if (strcmp("parallel", value) == 0) {
	schedule->mode = LMAP_SCHEDULE_EXEC_MODE_PARALLEL;
    } else if (strcmp("pipelined", value) == 0) {
	schedule->mode = LMAP_SCHEDULE_EXEC_MODE_PIPELINED;
    } else {
	lmap_err("illegal execution mode '%s'", value);
	return -1;
    }
    schedule->flags |= LMAP_SCHEDULE_FLAG_EXEC_MODE_SET;
    return 0;
}

int
lmap_schedule_set_state(struct schedule *schedule, const char *value)
{
    if (strcmp("enabled", value) == 0) {
	schedule->state = LMAP_SCHEDULE_STATE_ENABLED;
    } else if (strcmp("disabled", value) == 0) {
	schedule->state = LMAP_SCHEDULE_STATE_DISABLED;
    } else if (strcmp("running", value) == 0) {
	schedule->state = LMAP_SCHEDULE_STATE_RUNNING;
    } else if (strcmp("suppressed", value) == 0) {
	schedule->state = LMAP_SCHEDULE_STATE_SUPPRESSED;
    } else {
	lmap_err("illegal state '%s'", value);
	return -1;
    }
    return 0;
}

int
lmap_schedule_set_storage(struct schedule *schedule, const char *value)
{
    return set_uint64(&schedule->storage, value, __FUNCTION__);
}

int
lmap_schedule_set_invocations(struct schedule *schedule, const char *value)
{
    return set_uint32(&schedule->cnt_invocations, value, __FUNCTION__);
}

int
lmap_schedule_set_suppressions(struct schedule *schedule, const char *value)
{
    return set_uint32(&schedule->cnt_suppressions, value, __FUNCTION__);
}

int
lmap_schedule_set_overlaps(struct schedule *schedule, const char *value)
{
    return set_uint32(&schedule->cnt_overlaps, value, __FUNCTION__);
}

int
lmap_schedule_set_failures(struct schedule *schedule, const char *value)
{
    return set_uint32(&schedule->cnt_failures, value, __FUNCTION__);
}

int
lmap_schedule_set_last_invocation(struct schedule *schedule, const char *value)
{
    return set_dateandtime(&schedule->last_invocation, value, __FUNCTION__);
}

int
lmap_schedule_add_tag(struct schedule *schedule, const char *value)
{
    return add_tag(&schedule->tags, value, __FUNCTION__);
}

int
lmap_schedule_add_suppression_tag(struct schedule *schedule, const char *value)
{
    return add_tag(&schedule->suppression_tags, value, __FUNCTION__);
}

int
lmap_schedule_add_action(struct schedule *schedule, struct action *action)
{
    struct action **tail = &schedule->actions;
    struct action *cur;

    if (! action->name) {
	lmap_err("unnamed action");
	return -1;
    }

    while (*tail != NULL) {
	cur = *tail;
	if (cur) {
	    if (! strcmp(cur->name, action->name)) {
		lmap_err("duplicate action '%s'", action->name);
		return -1;
	    }
	}
	tail = &((*tail)->next);
    }
    *tail = action;

    return 0;
}

int
lmap_schedule_set_workspace(struct schedule *schedule, const char *value)
{
    return set_string(&schedule->workspace, value, __FUNCTION__);
}

/*
 * struct action functions...
 */

struct action *
lmap_action_new()
{
    struct action *action;

    action = (struct action*) xcalloc(1, sizeof(struct action), __FUNCTION__);
    action->state = LMAP_ACTION_STATE_ENABLED;
    return action;
}

void
lmap_action_free(struct action *action)
{
    if (action) {
	xfree(action->name);
	xfree(action->task);
	free_all_tags(action->destinations);
	free_all_options(action->options);
	free_all_tags(action->tags);
	free_all_tags(action->suppression_tags);
	xfree(action->last_message);
	xfree(action->last_failed_message);
	xfree(action->workspace);
	xfree(action);
    }
}

int
lmap_action_valid(struct lmap *lmap, struct action *action)
{
    int valid = 1;
    struct tag *tag;

    if (! action->name) {
	lmap_err("action requires a name");
	valid = 0;
    }

    if (! action->task) {
	lmap_err("action %s%s%srequires a task",
		 action->name ? "'" : "",
		 action->name ? action->name : "",
		 action->name ? "' " : "");
	valid = 0;
    }

    if (action->task && ! lmap_find_task(lmap, action->task)) {
	lmap_err("action %s%s%srefers to undefined task '%s'",
		 action->name ? "'" : "",
		 action->name ? action->name : "",
		 action->name ? "' " : "",
		 action->task);
	valid = 0;
    }
    for (tag = action->destinations; tag; tag = tag->next) {
	if (! lmap_find_schedule(lmap, tag->tag)) {
	    lmap_err("action %s%s%srefers to undefined destination '%s'",
		     action->name ? "'" : "",
		     action->name ? action->name : "",
		     action->name ? "' " : "",
		     tag->tag);
	    valid = 0;
	}
    }

    return valid;
}

int
lmap_action_set_name(struct action *action, const char *value)
{
    return set_lmap_identifier(&action->name, value, __FUNCTION__);
}

int
lmap_action_set_task(struct action *action, const char *value)
{
    return set_string(&action->task, value, __FUNCTION__);
}

int
lmap_action_add_option(struct action *action, struct option *option)
{
    return add_option(&action->options, option, __FUNCTION__);
}

int
lmap_action_add_destination(struct action *action, const char *value)
{
    return add_tag(&action->destinations, value, __FUNCTION__);
}

int
lmap_action_add_tag(struct action *action, const char *value)
{
    return add_tag(&action->tags, value, __FUNCTION__);
}

int
lmap_action_add_suppression_tag(struct action *action, const char *value)
{
    return add_tag(&action->suppression_tags, value, __FUNCTION__);
}

int
lmap_action_set_state(struct action *action, const char *value)
{
    if (strcmp("enabled", value) == 0) {
	action->state = LMAP_ACTION_STATE_ENABLED;
    } else if (strcmp("disabled", value) == 0) {
	action->state = LMAP_ACTION_STATE_DISABLED;
    } else if (strcmp("running", value) == 0) {
	action->state = LMAP_ACTION_STATE_RUNNING;
    } else if (strcmp("suppressed", value) == 0) {
	action->state = LMAP_ACTION_STATE_SUPPRESSED;
    } else {
	lmap_err("illegal state '%s'", value);
	return -1;
    }
    return 0;
}

int
lmap_action_set_storage(struct action *action, const char *value)
{
    return set_uint64(&action->storage, value, __FUNCTION__);
}

int
lmap_action_set_invocations(struct action *action, const char *value)
{
    return set_uint32(&action->cnt_invocations, value, __FUNCTION__);
}

int
lmap_action_set_suppressions(struct action *action, const char *value)
{
    return set_uint32(&action->cnt_suppressions, value, __FUNCTION__);
}

int
lmap_action_set_overlaps(struct action *action, const char *value)
{
    return set_uint32(&action->cnt_overlaps, value, __FUNCTION__);
}

int
lmap_action_set_failures(struct action *action, const char *value)
{
    return set_uint32(&action->cnt_failures, value, __FUNCTION__);
}

int
lmap_action_set_last_invocation(struct action *action, const char *value)
{
    return set_dateandtime(&action->last_invocation, value, __FUNCTION__);
}

int
lmap_action_set_last_completion(struct action *action, const char *value)
{
    return set_dateandtime(&action->last_completion, value, __FUNCTION__);
}

int
lmap_action_set_last_status(struct action *action, const char *value)
{
    return set_int32(&action->last_status, value, __FUNCTION__);
}

int
lmap_action_set_last_message(struct action *action, const char *value)
{
    return set_string(&action->last_message, value, __FUNCTION__);
}

int
lmap_action_set_last_failed_completion(struct action *action, const char *value)
{
    return set_dateandtime(&action->last_failed_completion, value, __FUNCTION__);
}

int
lmap_action_set_last_failed_status(struct action *action, const char *value)
{
    return set_int32(&action->last_failed_status, value, __FUNCTION__);
}

int
lmap_action_set_last_failed_message(struct action *action, const char *value)
{
    return set_string(&action->last_failed_message, value, __FUNCTION__);
}

int
lmap_action_set_workspace(struct action *action, const char *value)
{
    return set_string(&action->workspace, value, __FUNCTION__);
}

/*
 * struct lmapd functions...
 */

struct lmapd *
lmapd_new()
{
    struct lmapd *lmapd;

    lmapd = (struct lmapd*) xcalloc(1, sizeof(struct lmapd), __FUNCTION__);
    return lmapd;
}

void
lmapd_free(struct lmapd *lmapd)
{
    if (lmapd) {
	lmap_free(lmapd->lmap);
	xfree(lmapd->config_path);
	xfree(lmapd->queue_path);
	xfree(lmapd->run_path);
	xfree(lmapd);
    }
}

int
lmapd_set_config_path(struct lmapd *lmapd, const char *value)
{
    int ret;
    size_t len;
    struct stat sb;
    char *name = NULL;

    if (stat(value, &sb) == -1) {
    invalid:
	lmap_err("invalid config path or file '%s'", value);
	return -1;
    }

    if (S_ISREG(sb.st_mode) || S_ISDIR(sb.st_mode)) {
	return set_string(&lmapd->config_path, value, __FUNCTION__);
    } else {
	goto invalid;
    }

    /*
     * Try to find the configuration file by appending a config file
     * name to the config path.
     */
    
    len = strlen(value) + strlen(LMAPD_CONFIG_FILE) + 2;
    name = xcalloc(len, 1, __FUNCTION__);
    snprintf(name, len, "%s/%s", value, LMAPD_CONFIG_FILE);
    if (! name || stat(name, &sb) == -1 || ! S_ISREG(sb.st_mode)) {
	lmap_err("invalid config file '%s'", name);
	xfree(name);
	return -1;
    }

    ret = set_string(&lmapd->config_path, name ? name : value, __FUNCTION__);
    xfree(name);
    return ret;
}

int
lmapd_set_queue_path(struct lmapd *lmapd, const char *value)
{
    struct stat sb;

    if (stat(value, &sb) == -1 || ! S_ISDIR(sb.st_mode)) {
	lmap_err("invalid queue path '%s'", value);
	return -1;
    }
    
    return set_string(&lmapd->queue_path, value, __FUNCTION__);
}

int
lmapd_set_run_path(struct lmapd *lmapd, const char *value)
{
    struct stat sb;

    if (stat(value, &sb) == -1 || ! S_ISDIR(sb.st_mode)) {
	lmap_err("invalid run path '%s'", value);
	return -1;
    }
    
    return set_string(&lmapd->run_path, value, __FUNCTION__);
}

/*
 * struct val functions...
 */

struct value *
lmap_value_new()
{
    struct value *val;

    val = (struct value*) xcalloc(1, sizeof(struct value), __FUNCTION__);
    return val;
}

void
lmap_value_free(struct value *val)
{
    if (val) {
	xfree(val->value);
	xfree(val);
    }
}

int
lmap_value_valid(struct lmap *lmap, struct value *val)
{
    int valid = 1;

    UNUSED(lmap);

    if (! val->value) {
	lmap_err("val requires a value");
	valid = 0;
    }
    
    return valid;
}

int
lmap_value_set_value(struct value *val, const char *value)
{
    return set_string(&val->value, value, __FUNCTION__);
}

/*
 * struct row functions...
 */

struct row *
lmap_row_new()
{
    struct row *row;

    row = (struct row*) xcalloc(1, sizeof(struct row), __FUNCTION__);
    return row;
}

void
lmap_row_free(struct row *row)
{
    if (row) {
	while (row->values) {
	    struct value *val = row->values;
	    row->values = val->next;
	    lmap_value_free(val);
	}
	xfree(row);
    }
}

int
lmap_row_valid(struct lmap *lmap, struct row *row)
{
    struct value *val;
    int valid = 1;

    UNUSED(lmap);

    for (val = row->values; val; val = val->next) {
	valid &= lmap_value_valid(lmap, val);
    }
    
    return valid;
}

int
lmap_row_add_value(struct row *row, struct value *val)
{
    struct value *tail;

    if (! row->values) {
	row->values = val;
	return 0;
    }

    for (tail = row->values; tail; ) {
	if (tail->next) {
	    tail = tail->next;
	} else {
	    tail->next = val;
	    break;
	}
    }
    return 0;
}

/*
 * struct table functions...
 */

struct table *
lmap_table_new()
{
    struct table *tab;

    tab = (struct table*) xcalloc(1, sizeof(struct table), __FUNCTION__);
    return tab;
}

void
lmap_table_free(struct table *tab)
{
    if (tab) {
	while (tab->rows) {
	    struct row *row = tab->rows;
	    tab->rows = row->next;
	    lmap_row_free(row);
	}
	xfree(tab);
    }
}

int
lmap_table_valid(struct lmap *lmap, struct table *tab)
{
    struct row *row;
    int valid = 1;

    UNUSED(lmap);

    for (row = tab->rows; row; row = row->next) {
	valid &= lmap_row_valid(lmap, row);
    }
    
    return valid;
}

int
lmap_table_add_row(struct table *tab, struct row *row)
{
    struct row **tail = &tab->rows;

    while (*tail != NULL) {
	tail = &((*tail)->next);
    }
    *tail = row;

    return 0;
}

/*
 * struct result functions...
 */

struct result *
lmap_result_new()
{
    struct result *res;

    res = (struct result*) xcalloc(1, sizeof(struct result), __FUNCTION__);
    return res;
}

void
lmap_result_free(struct result *res)
{
    if (res) {
	while (res->tables) {
	    struct table *tab = res->tables;
	    res->tables = tab->next;
	    lmap_table_free(tab);
	}
	xfree(res);
    }
}

int
lmap_result_valid(struct lmap *lmap, struct result *res)
{
    struct table *tab;
    int valid = 1;

    UNUSED(lmap);

    for (tab = res->tables; tab; tab = tab->next) {
	valid &= lmap_table_valid(lmap, tab);
    }
    
    return valid;
}

int
lmap_result_add_table(struct result *res, struct table *tab)
{
    struct table **tail = &res->tables;

    while (*tail != NULL) {
	tail = &((*tail)->next);
    }
    *tail = tab;

    return 0;
}

int
lmap_result_set_schedule(struct result *res, const char *value)
{
    return set_lmap_identifier(&res->schedule, value, __FUNCTION__);
}

int
lmap_result_set_action(struct result *res, const char *value)
{
    return set_lmap_identifier(&res->action, value, __FUNCTION__);
}

int
lmap_result_set_task(struct result *res, const char *value)
{
    return set_lmap_identifier(&res->task, value, __FUNCTION__);
}

int
lmap_result_set_start(struct result *res, const char *value)
{
    uint32_t u;
    int ret;
    
    ret = set_uint32(&u, value, __FUNCTION__);
    if (ret == 0) {
	res->start = u;
    }
    
    return ret;
}

int
lmap_result_set_end(struct result *res, const char *value)
{
    uint32_t u;
    int ret;
    
    ret = set_uint32(&u, value, __FUNCTION__);
    if (ret == 0) {
	res->end = u;
    }

    return ret;
}

int
lmap_result_set_status(struct result *res, const char *value)
{
    int32_t i;
    int ret;
    
    ret = set_int32(&i, value, __FUNCTION__);
    if (ret == 0) {
	res->status = i;
    }

    return ret;
}
