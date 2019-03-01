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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

#include <libxml/debugXML.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "lmap.h"
#include "utils.h"
#include "xml-io.h"

#define RENDER_CONFIG_TRUE	0x01
#define RENDER_CONFIG_FALSE	0x02

#define PARSE_CONFIG_TRUE	0x01
#define PARSE_CONFIG_FALSE	0x02

#define YANG_CONFIG_TRUE	0x01
#define YANG_CONFIG_FALSE	0x02
#define YANG_KEY		0x04

/**
 * @brief Parses the agent information
 * @details Function to parse the agent object information from the XML config
 * file
 * @return 0 on success, -1 on error
 */
static int
parse_agent(struct lmap *lmap, xmlXPathContextPtr ctx, int what)
{
    int i, j;
    xmlXPathObjectPtr result;

    const char *xpath = "//lmapc:lmap/lmapc:agent/lmapc:*";

    struct {
	char *name;
	int flags;
	int (*func)(struct agent *a, const char *c);
    } tab[] = {
	{ .name = "agent-id",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_agent_set_agent_id },
	{ .name = "group-id",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_agent_set_group_id },
	{ .name = "measurement-point",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_agent_set_measurement_point },
	{ .name = "report-agent-id",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_agent_set_report_agent_id },
	{ .name = "report-group-id",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_agent_set_report_group_id },
	{ .name = "report-measurement-point",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_agent_set_report_measurement_point },
	{ .name = "controller-timeout",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_agent_set_controller_timeout },
	{ .name = "last-started",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_agent_set_last_started },
	{ .name = NULL, .flags = 0, .func = NULL }
    };

    assert(lmap);

    result = xmlXPathEvalExpression(BAD_CAST xpath, ctx);
    if (! result) {
	lmap_err("error in xpath expression '%s'", xpath);
	return -1;
    }

    if (!result->nodesetval || !result->nodesetval->nodeNr) {
	xmlXPathFreeObject(result);
	return 0;
    }

    if (! lmap->agent) {
	lmap->agent = lmap_agent_new();
	if (! lmap->agent) {
	    xmlXPathFreeObject(result);
	    return -1;
	}
    }

    for (i = 0; result->nodesetval && i < result->nodesetval->nodeNr; i++) {
	xmlNodePtr node = result->nodesetval->nodeTab[i];

	for (j = 0; tab[j].name; j++) {
	    if ((tab[j].flags & YANG_KEY)
		|| (what & PARSE_CONFIG_TRUE && tab[j].flags & YANG_CONFIG_TRUE)
		|| (what & PARSE_CONFIG_FALSE && tab[j].flags & YANG_CONFIG_FALSE)) {
		if (!xmlStrcmp(node->name, BAD_CAST tab[j].name)) {
		    xmlChar *content = xmlNodeGetContent(node);
		    tab[j].func(lmap->agent, (char *) content);
		    if (content) {
			xmlFree(content);
		    }
		    break;
		}
	    }
	}
	if (! tab[j].name) {
	    lmap_wrn("unexpected element '%s'", node->name);
	}
    }
    xmlXPathFreeObject(result);

    return 0;
}

static struct supp *
parse_suppression(xmlNodePtr supp_node, int what)
{
    int j;
    xmlNodePtr node;
    struct supp *supp;

    struct {
	char *name;
	int flags;
	int (*func)(struct supp *s, const char *c);
    } tab[] = {
	{ .name = "name",
	  .flags = YANG_CONFIG_TRUE | YANG_KEY,
	  .func = lmap_supp_set_name },
	{ .name = "start",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_supp_set_start },
	{ .name = "end",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_supp_set_end },
	{ .name = "match",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_supp_add_match },
	{ .name = "stop-running",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_supp_set_stop_running },
	{ .name = "state",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_supp_set_state },
	{ .name = NULL, .flags = 0, .func = NULL }
    };

    supp = lmap_supp_new();
    if (! supp) {
	return NULL;
    }

    for (node = xmlFirstElementChild(supp_node);
	 node; node = xmlNextElementSibling(node)) {

	if (node->ns != supp_node->ns) continue;

	for (j = 0; tab[j].name; j++) {
	    if ((tab[j].flags & YANG_KEY)
		|| (what & PARSE_CONFIG_TRUE && tab[j].flags & YANG_CONFIG_TRUE)
		|| (what & PARSE_CONFIG_FALSE && tab[j].flags & YANG_CONFIG_FALSE)) {
		if (!xmlStrcmp(node->name, BAD_CAST tab[j].name)) {
		    xmlChar *content = xmlNodeGetContent(node);
		    tab[j].func(supp, (char *) content);
		    if (content) {
			xmlFree(content);
		    }
		    break;
		}
	    }
	}
	if (! tab[j].name) {
	    lmap_wrn("unexpected element '%s'", node->name);
	}
    }

    return supp;
}

/**
 * @brief Parses the suppression information
 * @details Function to parse the suppression information from the XML config
 * file
 * @return 0 on success, -1 on error
 */
static int
parse_suppressions(struct lmap *lmap, xmlXPathContextPtr ctx, int what)
{
    int i;
    xmlXPathObjectPtr result;
    struct supp *supp;

    const char *xpath = "//lmapc:lmap/lmapc:suppressions/lmapc:suppression";

    assert(lmap);

    result = xmlXPathEvalExpression(BAD_CAST xpath, ctx);
    if (! result) {
	lmap_err("error in xpath expression '%s'", xpath);
	return -1;
    }

    for (i = 0; result->nodesetval && i < result->nodesetval->nodeNr; i++) {
	supp = parse_suppression(result->nodesetval->nodeTab[i], what);
	if (supp) {
	    lmap_add_supp(lmap, supp);
	}
    }

    xmlXPathFreeObject(result);
    return 0;
}

static struct option *
parse_option(xmlNodePtr option_node, int what)
{
    int j;
    xmlNodePtr node;
    struct option *option;

    struct {
	char *name;
	int flags;
	int (*func)(struct option *s, const char *c);
    } tab[] = {
	{ .name = "id",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_option_set_id },
	{ .name = "name",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_option_set_name },
	{ .name = "value",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_option_set_value },
	{ .name = NULL, .flags = 0, .func = NULL }
    };

    option = lmap_option_new();
    if (! option) {
	return NULL;
    }

    for (node = xmlFirstElementChild(option_node);
	 node; node = xmlNextElementSibling(node)) {

	if (node->ns != option_node->ns) continue;

	for (j = 0; tab[j].name; j++) {
	    if ((tab[j].flags & YANG_KEY)
		|| (what & PARSE_CONFIG_TRUE && tab[j].flags & YANG_CONFIG_TRUE)
		|| (what & PARSE_CONFIG_FALSE && tab[j].flags & YANG_CONFIG_FALSE)) {
		if (!xmlStrcmp(node->name, BAD_CAST tab[j].name)) {
		    xmlChar *content = xmlNodeGetContent(node);
		    tab[j].func(option, (char *) content);
		    if (content) {
			xmlFree(content);
		    }
		    break;
		}
	    }
	}
	if (! tab[j].name) {
	    lmap_wrn("unexpected element '%s'", node->name);
	}
    }

    return option;
}

static struct registry *
parse_registry(xmlNodePtr registry_node, int what)
{
    int j;
    xmlNodePtr node;
    struct registry *registry;

    struct {
	char *name;
	int flags;
	int (*func)(struct registry *s, const char *c);
    } tab[] = {
	{ .name = "uri",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_registry_set_uri },
	{ .name = "role",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_registry_add_role },
	{ .name = NULL, .flags = 0, .func = NULL }
    };

    registry = lmap_registry_new();
    if (! registry) {
	return NULL;
    }

    for (node = xmlFirstElementChild(registry_node);
	 node; node = xmlNextElementSibling(node)) {

	if (node->ns != registry_node->ns) continue;

	for (j = 0; tab[j].name; j++) {
	    if ((tab[j].flags & YANG_KEY)
		|| (what & PARSE_CONFIG_TRUE && tab[j].flags & YANG_CONFIG_TRUE)
		|| (what & PARSE_CONFIG_FALSE && tab[j].flags & YANG_CONFIG_FALSE)) {
		if (!xmlStrcmp(node->name, BAD_CAST tab[j].name)) {
		    xmlChar *content = xmlNodeGetContent(node);
		    tab[j].func(registry, (char *) content);
		    if (content) {
			xmlFree(content);
		    }
		    break;
		}
	    }
	}
	if (! tab[j].name) {
	    lmap_wrn("unexpected element '%s'", node->name);
	}
    }

    return registry;
}

static struct task *
parse_task(xmlNodePtr task_node, int what)
{
    int j;
    xmlNodePtr node;
    struct task *task;

    struct {
	char *name;
	int flags;
	int (*func)(struct task *s, const char *c);
    } tab[] = {
	{ .name = "name",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_task_set_name },
	{ .name = "program",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_task_set_program },
	{ .name = "tag",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_task_add_tag },
	{ .name = NULL, .flags = 0, .func = NULL }
    };

    task = lmap_task_new();
    if (! task) {
	return NULL;
    }

    for (node = xmlFirstElementChild(task_node);
	 node; node = xmlNextElementSibling(node)) {

	if (node->ns != task_node->ns) continue;

	if (!xmlStrcmp(node->name, BAD_CAST "option")) {
	    struct option *option = parse_option(node, what);
	    lmap_task_add_option(task, option);
	    continue;
	}

	if (!xmlStrcmp(node->name, BAD_CAST "function")) {
	    struct registry *registry = parse_registry(node, what);
	    lmap_task_add_registry(task, registry);
	    continue;
	}

	for (j = 0; tab[j].name; j++) {
	    if ((tab[j].flags & YANG_KEY)
		|| (what & PARSE_CONFIG_TRUE && tab[j].flags & YANG_CONFIG_TRUE)
		|| (what & PARSE_CONFIG_FALSE && tab[j].flags & YANG_CONFIG_FALSE)) {
		if (!xmlStrcmp(node->name, BAD_CAST tab[j].name)) {
		    xmlChar *content = xmlNodeGetContent(node);
		    tab[j].func(task, (char *) content);
		    if (content) {
			xmlFree(content);
		    }
		    break;
		}
	    }
	}
	if (! tab[j].name) {
	    lmap_wrn("unexpected element '%s'", node->name);
	}
    }

    return task;
}

/**
 * @brief Parses the task information
 * @details Function to parse the task information from the XML config
 * file
 * @return 0 on success, -1 on error
 */
static int
parse_tasks(struct lmap *lmap, xmlXPathContextPtr ctx, int what)
{
    int i;
    xmlXPathObjectPtr result;
    struct task *task;

    const char *xpath = "//lmapc:lmap/lmapc:tasks/lmapc:task";

    assert(lmap);

    result = xmlXPathEvalExpression(BAD_CAST xpath, ctx);
    if (! result) {
	lmap_err("error in xpath expression '%s'", xpath);
	return -1;
    }

    for (i = 0; result->nodesetval && i < result->nodesetval->nodeNr; i++) {
	task = parse_task(result->nodesetval->nodeTab[i], what);
	if (task) {
	    lmap_add_task(lmap, task);
	}
    }

    xmlXPathFreeObject(result);
    return 0;
}

static struct task *
parse_capability_task(xmlNodePtr task_node, int what)
{
    int j;
    xmlNodePtr node;
    struct task *task;

    struct {
	char *name;
	int flags;
	int (*func)(struct task *s, const char *c);
    } tab[] = {
	{ .name = "name",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_task_set_name },
	{ .name = "version",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_task_set_version },
	{ .name = "program",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_task_set_program },
	{ .name = NULL, .flags = 0, .func = NULL }
    };

    task = lmap_task_new();
    if (! task) {
	return NULL;
    }

    for (node = xmlFirstElementChild(task_node);
	 node; node = xmlNextElementSibling(node)) {

	if (node->ns != task_node->ns) continue;

	if (!xmlStrcmp(node->name, BAD_CAST "function")) {
	    struct registry *registry = parse_registry(node, what);
	    lmap_task_add_registry(task, registry);
	    continue;
	}

	for (j = 0; tab[j].name; j++) {
	    if ((tab[j].flags & YANG_KEY)
		|| (what & PARSE_CONFIG_TRUE && tab[j].flags & YANG_CONFIG_TRUE)
		|| (what & PARSE_CONFIG_FALSE && tab[j].flags & YANG_CONFIG_FALSE)) {
		if (!xmlStrcmp(node->name, BAD_CAST tab[j].name)) {
		    xmlChar *content = xmlNodeGetContent(node);
		    tab[j].func(task, (char *) content);
		    if (content) {
			xmlFree(content);
		    }
		    break;
		}
	    }
	}
	if (! tab[j].name) {
	    lmap_wrn("unexpected element '%s'", node->name);
	}
    }

    return task;
}

/**
 * @brief Parses the capability tasks information
 * @details Function to parse the capability task information from the
 * XML config file
 * @return 0 on success, -1 on error
 */
static int
parse_capability_tasks(struct lmap *lmap, xmlXPathContextPtr ctx, int what)
{
    int i;
    xmlXPathObjectPtr result;
    struct task *task;

    const char *xpath = "//lmapc:lmap/lmapc:capabilities/lmapc:tasks/lmapc:task";

    assert(lmap);

    result = xmlXPathEvalExpression(BAD_CAST xpath, ctx);
    if (! result) {
	lmap_err("error in xpath expression '%s'", xpath);
	return -1;
    }

    if (! lmap->capabilities) {
	lmap->capabilities = lmap_capability_new();
	if (! lmap->capabilities) {
	    xmlXPathFreeObject(result);
	    return -1;
	}
    }

    for (i = 0; result->nodesetval && i < result->nodesetval->nodeNr; i++) {
	task = parse_capability_task(result->nodesetval->nodeTab[i], what);
	if (task) {
	    lmap_capability_add_task(lmap->capabilities, task);
	}
    }

    xmlXPathFreeObject(result);
    return 0;
}

/**
 * @brief Parses the capabilities information
 * @details Function to parse the capability object information from the
 * XML config file
 * @return 0 on success, -1 on error
 */
static int
parse_capabilities(struct lmap *lmap, xmlXPathContextPtr ctx, int what)
{
    int i, j;
    xmlXPathObjectPtr result;

    const char *xpath = "//lmapc:lmap/lmapc:capabilities/lmapc:*";

    struct {
	char *name;
	int flags;
	int (*func)(struct capability *a, const char *c);
    } tab[] = {
	{ .name = "version",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_capability_set_version },
	{ .name = "tag",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_capability_add_tag },
	{ .name = NULL, .flags = 0, .func = NULL }
    };

    assert(lmap);

    result = xmlXPathEvalExpression(BAD_CAST xpath, ctx);
    if (! result) {
	lmap_err("error in xpath expression '%s'", xpath);
	return -1;
    }

    if (!result->nodesetval || !result->nodesetval->nodeNr) {
	xmlXPathFreeObject(result);
	return 0;
    }

    if (! lmap->capabilities) {
	lmap->capabilities = lmap_capability_new();
	if (! lmap->capabilities) {
	    xmlXPathFreeObject(result);
	    return -1;
	}
    }

    for (i = 0; result->nodesetval && i < result->nodesetval->nodeNr; i++) {
	xmlNodePtr node = result->nodesetval->nodeTab[i];

	for (j = 0; tab[j].name; j++) {
	    if ((tab[j].flags & YANG_KEY)
		|| (what & PARSE_CONFIG_TRUE && tab[j].flags & YANG_CONFIG_TRUE)
		|| (what & PARSE_CONFIG_FALSE && tab[j].flags & YANG_CONFIG_FALSE)) {
		if (!xmlStrcmp(node->name, BAD_CAST "tasks")) {
		    continue;
		}
		if (!xmlStrcmp(node->name, BAD_CAST tab[j].name)) {
		    xmlChar *content = xmlNodeGetContent(node);
		    tab[j].func(lmap->capabilities, (char *) content);
		    if (content) {
			xmlFree(content);
		    }
		    break;
		}
	    }
	}
	if (! tab[j].name) {
	    lmap_wrn("unexpected element '%s'", node->name);
	}
    }
    xmlXPathFreeObject(result);

    return 0;
}

static void
parse_periodic(struct event *event, xmlNodePtr period_node, int what)
{
    int j;
    xmlNodePtr node;

    struct {
	char *name;
	int flags;
	int (*func)(struct event *e, const char *c);
    } tab[] = {
	{ .name = "interval",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_set_interval },
	{ .name = "start",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_set_start },
	{ .name = "end",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_set_end },
	{ .name = NULL, .flags = 0, .func = NULL }
    };

    for (node = xmlFirstElementChild(period_node);
	 node; node = xmlNextElementSibling(node)) {

	if (node->ns != period_node->ns) continue;

	for (j = 0; tab[j].name; j++) {
	    if ((tab[j].flags & YANG_KEY)
		|| (what & PARSE_CONFIG_TRUE && tab[j].flags & YANG_CONFIG_TRUE)
		|| (what & PARSE_CONFIG_FALSE && tab[j].flags & YANG_CONFIG_FALSE)) {
		if (!xmlStrcmp(node->name, BAD_CAST tab[j].name)) {
		    xmlChar *content = xmlNodeGetContent(node);
		    tab[j].func(event, (char *) content);
		    if (content) {
			xmlFree(content);
		    }
		    break;
		}
	    }
	}
	if (! tab[j].name) {
	    lmap_wrn("unexpected element '%s'", node->name);
	}
    }
}

static void
parse_calendar(struct event *event, xmlNodePtr calendar_node, int what)
{
    int j;
    xmlNodePtr node;

    struct {
	char *name;
	int flags;
	int (*func)(struct event *e, const char *c);
    } tab[] = {
	{ .name = "month",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_add_month },
	{ .name = "day-of-month",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_add_day_of_month },
	{ .name = "day-of-week",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_add_day_of_week },
	{ .name = "hour",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_add_hour },
	{ .name = "minute",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_add_minute },
	{ .name = "second",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_add_second },
	{ .name = "timezone-offset",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_set_timezone_offset },
	{ .name = "start",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_set_start },
	{ .name = "end",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_set_end },
	{ .name = NULL, .flags = 0, .func = NULL }
    };

    for (node = xmlFirstElementChild(calendar_node);
	 node; node = xmlNextElementSibling(node)) {

	if (node->ns != calendar_node->ns) continue;

	for (j = 0; tab[j].name; j++) {
	    if ((tab[j].flags & YANG_KEY)
		|| (what & PARSE_CONFIG_TRUE && tab[j].flags & YANG_CONFIG_TRUE)
		|| (what & PARSE_CONFIG_FALSE && tab[j].flags & YANG_CONFIG_FALSE)) {
		if (!xmlStrcmp(node->name, BAD_CAST tab[j].name)) {
		    xmlChar *content = xmlNodeGetContent(node);
		    tab[j].func(event, (char *) content);
		    if (content) {
			xmlFree(content);
		    }
		    break;
		}
	    }
	}
	if (! tab[j].name) {
	    lmap_wrn("unexpected element '%s'", node->name);
	}
    }
}

static void
parse_one_off(struct event *event, xmlNodePtr one_off_node, int what)
{
    int j;
    xmlNodePtr node;

    struct {
	char *name;
	int flags;
	int (*func)(struct event *e, const char *c);
    } tab[] = {
	{ .name = "time",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_set_start },
	{ .name = NULL, .flags = 0, .func = NULL }
    };

    for (node = xmlFirstElementChild(one_off_node);
	 node; node = xmlNextElementSibling(node)) {

	if (node->ns != one_off_node->ns) continue;

	for (j = 0; tab[j].name; j++) {
	    if (!xmlStrcmp(node->name, BAD_CAST tab[j].name)) {
		xmlChar *content = xmlNodeGetContent(node);
		tab[j].func(event, (char *) content);
		if (content) {
		    xmlFree(content);
		}
		break;
	    }
	}
	if (! tab[j].name) {
	    lmap_wrn("unexpected element '%s'", node->name);
	}
    }
}

static struct event *
parse_event(xmlNodePtr event_node, int what)
{
    int j;
    xmlNodePtr node;
    struct event *event;

    struct {
	char *name;
	int flags;
	int (*func)(struct event *e, const char *c);
	void (*parse)(struct event *e, xmlNodePtr node, int what);
    } tab[] = {
	{ .name = "name",
	  .flags = YANG_CONFIG_TRUE | YANG_KEY,
	  .func = lmap_event_set_name,
	  .parse = NULL },
	{ .name = "random-spread",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_set_random_spread,
	  .parse = NULL },
	{ .name = "cycle-interval",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_set_cycle_interval,
	  .parse = NULL },
	{ .name = "periodic",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_set_type,
	  .parse = parse_periodic },
	{ .name = "calendar",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_set_type,
	  .parse = parse_calendar },
	{ .name = "one-off",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_set_type,
	  .parse = parse_one_off },
	{ .name = "immediate",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_set_type,
	  .parse = NULL },
	{ .name = "startup",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_set_type,
	  .parse = NULL },
	{ .name = "controller-lost",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_set_type,
	  .parse = NULL },
	{ .name = "controller-connected",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_event_set_type,
	  .parse = NULL },
	{ .name = NULL, .flags = 0, .func = NULL, .parse = NULL }
    };

    event = lmap_event_new();
    if (! event) {
	return NULL;
    }

    for (node = xmlFirstElementChild(event_node);
	 node; node = xmlNextElementSibling(node)) {

	if (node->ns != event_node->ns) continue;

	for (j = 0; tab[j].name; j++) {
	    if ((tab[j].flags & YANG_KEY)
		|| (what & PARSE_CONFIG_TRUE && tab[j].flags & YANG_CONFIG_TRUE)
		|| (what & PARSE_CONFIG_FALSE && tab[j].flags & YANG_CONFIG_FALSE)) {
		if (!xmlStrcmp(node->name, BAD_CAST tab[j].name)) {
		    if (tab[j].func == lmap_event_set_type) {
			tab[j].func(event, (char *) node->name);
		    } else {
			xmlChar *content = xmlNodeGetContent(node);
			tab[j].func(event, (char *) content);
			if (content) {
			    xmlFree(content);
			}
		    }
		    if (tab[j].parse) {
			tab[j].parse(event, node, what);
		    }
		    break;
		}
	    }
	}
	if (! tab[j].name) {
	    lmap_wrn("unexpected element '%s'", node->name);
	}
    }

    return event;
}

/**
 * @brief Parses the event information
 * @details Function to parse the event information from the XML config
 * file
 * @return 0 on success, -1 on error
 */
static int
parse_events(struct lmap *lmap, xmlXPathContextPtr ctx, int what)
{
    int i;
    xmlXPathObjectPtr result;
    struct event *event;

    const char *xpath = "//lmapc:lmap/lmapc:events/lmapc:event";

    assert(lmap);

    result = xmlXPathEvalExpression(BAD_CAST xpath, ctx);
    if (! result) {
	lmap_err("error in xpath expression '%s'", xpath);
	return -1;
    }

    for (i = 0; result->nodesetval && i < result->nodesetval->nodeNr; i++) {
	event = parse_event(result->nodesetval->nodeTab[i], what);
	if (event) {
	    lmap_add_event(lmap, event);
	}
    }

    xmlXPathFreeObject(result);
    return 0;
}

static struct action *
parse_action(xmlNodePtr action_node, int what)
{
    int j;
    xmlNodePtr node;
    struct action *action;

    struct {
	char *name;
	int flags;
	int (*func)(struct action *s, const char *c);
    } tab[] = {
	{ .name = "name",
	  .flags = YANG_CONFIG_TRUE | YANG_KEY,
	  .func = lmap_action_set_name },
	{ .name = "task",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_action_set_task },
	{ .name = "destination",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_action_add_destination },
	{ .name = "tag",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_action_add_tag },
	{ .name = "suppression-tag",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_action_add_suppression_tag },
	{ .name = "state",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_action_set_state },
	{ .name = "storage",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_action_set_storage },
	{ .name = "invocations",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_action_set_invocations },
	{ .name = "suppressions",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_action_set_suppressions },
	{ .name = "overlaps",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_action_set_overlaps },
	{ .name = "failures",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_action_set_failures },
	{ .name = "last-invocation",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_action_set_last_invocation },
	{ .name = "last-completion",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_action_set_last_completion },
	{ .name = "last-status",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_action_set_last_status },
	{ .name = "last-message",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_action_set_last_message },
	{ .name = "last-failed-completion",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_action_set_last_failed_completion },
	{ .name = "last-failed-status",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_action_set_last_failed_status },
	{ .name = "last-failed-message",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_action_set_last_failed_message },
	{ .name = NULL, .flags = 0, .func = NULL }
    };

    action = lmap_action_new();
    if (! action) {
	return NULL;
    }

    for (node = xmlFirstElementChild(action_node);
	 node; node = xmlNextElementSibling(node)) {

	if (node->ns != action_node->ns) continue;

	if (!xmlStrcmp(node->name, BAD_CAST "option")) {
	    struct option *option = parse_option(node, what);
	    lmap_action_add_option(action, option);
	    continue;
	}

	for (j = 0; tab[j].name; j++) {
	    if ((tab[j].flags & YANG_KEY)
		|| (what & PARSE_CONFIG_TRUE && tab[j].flags & YANG_CONFIG_TRUE)
		|| (what & PARSE_CONFIG_FALSE && tab[j].flags & YANG_CONFIG_FALSE)) {
		if (!xmlStrcmp(node->name, BAD_CAST tab[j].name)) {
		    xmlChar *content = xmlNodeGetContent(node);
		    tab[j].func(action, (char *) content);
		    if (content) {
			xmlFree(content);
		    }
		    break;
		}
	    }
	}
	if (! tab[j].name) {
	    lmap_wrn("unexpected element '%s'", node->name);
	}
    }

    return action;
}

static struct schedule *
parse_schedule(xmlNodePtr schedule_node, int what)
{
    int j;
    xmlNodePtr node;
    struct schedule *schedule;

    struct {
	char *name;
	int flags;
	int (*func)(struct schedule *e, const char *c);
    } tab[] = {
	{ .name = "name",
	  .flags = YANG_CONFIG_TRUE | YANG_KEY,
	  .func = lmap_schedule_set_name },
	{ .name = "start",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_schedule_set_start },
	{ .name = "end",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_schedule_set_end },
	{ .name = "duration",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_schedule_set_duration },
	{ .name = "execution-mode",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_schedule_set_exec_mode },
	{ .name = "tag",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_schedule_add_tag },
	{ .name = "suppression-tag",
	  .flags = YANG_CONFIG_TRUE,
	  .func = lmap_schedule_add_suppression_tag },
	{ .name = "state",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_schedule_set_state },
	{ .name = "storage",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_schedule_set_storage },
	{ .name = "invocations",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_schedule_set_invocations },
	{ .name = "suppressions",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_schedule_set_suppressions },
	{ .name = "overlaps",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_schedule_set_overlaps },
	{ .name = "failures",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_schedule_set_failures },
	{ .name = "last-invocation",
	  .flags = YANG_CONFIG_FALSE,
	  .func = lmap_schedule_set_last_invocation },
	{ .name = NULL, .flags = 0, .func = NULL }
    };

    schedule = lmap_schedule_new();
    if (! schedule) {
	return NULL;
    }

    for (node = xmlFirstElementChild(schedule_node);
	 node; node = xmlNextElementSibling(node)) {

	if (node->ns != schedule_node->ns) continue;

	if (!xmlStrcmp(node->name, BAD_CAST "action")) {
	    struct action *action = parse_action(node, what);
	    lmap_schedule_add_action(schedule, action);
	    continue;
	}

	for (j = 0; tab[j].name; j++) {
	    if ((tab[j].flags & YANG_KEY)
		|| (what & PARSE_CONFIG_TRUE && tab[j].flags & YANG_CONFIG_TRUE)
		|| (what & PARSE_CONFIG_FALSE && tab[j].flags & YANG_CONFIG_FALSE)) {
		if (!xmlStrcmp(node->name, BAD_CAST tab[j].name)) {
		    xmlChar *content = xmlNodeGetContent(node);
		    tab[j].func(schedule, (char *) content);
		    if (content) {
			xmlFree(content);
		    }
		    break;
		}
	    }
	}
	if (! tab[j].name) {
	    lmap_wrn("unexpected element '%s'", node->name);
	}
    }

    return schedule;
}

/**
 * @brief Parses the schedule information
 * @details Function to parse the schedule information from the XML config
 * file
 * @return 0 on success, -1 on error
 */
static int
parse_schedules(struct lmap *lmap, xmlXPathContextPtr ctx, int what)
{
    int i;
    xmlXPathObjectPtr result;
    struct schedule *schedule;

    const char *xpath = "//lmapc:lmap/lmapc:schedules/lmapc:schedule";

    assert(lmap);

    result = xmlXPathEvalExpression(BAD_CAST xpath, ctx);
    if (! result) {
	lmap_err("error in xpath expression '%s'", xpath);
	return -1;
    }

    for (i = 0; result->nodesetval && i < result->nodesetval->nodeNr; i++) {
	schedule = parse_schedule(result->nodesetval->nodeTab[i], what);
	if (schedule) {
	    lmap_add_schedule(lmap, schedule);
	}
    }

    xmlXPathFreeObject(result);
    return 0;
}

static int
parse_control(struct lmap *lmap, xmlDocPtr doc, int what)
{
    int i, ret;
    xmlXPathContextPtr ctx = NULL;

    struct {
	int (*parse)(struct lmap *lmap, xmlXPathContextPtr ctx, int what);
    } tab[] = {
	{ parse_capabilities },
	{ parse_capability_tasks },
	{ parse_agent },
	{ parse_schedules },
	{ parse_suppressions },
	{ parse_tasks },
	{ parse_events },
	{ NULL }
    };

    assert(lmap && doc);

    ctx = xmlXPathNewContext(doc);
    if (! ctx) {
	lmap_err("cannot create xpath context");
	ret = -1;
	goto exit;
    }

    ret = xmlXPathRegisterNs(ctx, BAD_CAST LMAPC_XML_PREFIX,
			     BAD_CAST LMAPC_XML_NAMESPACE);
    if (ret != 0) {
	lmap_err("cannot register xpath namespace '%s'", LMAPC_XML_NAMESPACE);
	ret = -1;
	goto exit;
    }

    for (i = 0; tab[i].parse; i++) {
	ret = tab[i].parse(lmap, ctx, what);
	if (ret != 0) {
	    goto exit;
	}
    }

exit:
    if (ctx) {
	xmlXPathFreeContext(ctx);
    }
    return ret;
}

static int
parse_config_doc(struct lmap *lmap, xmlDocPtr doc)
{
    return parse_control(lmap, doc, PARSE_CONFIG_TRUE);
}

int
lmap_xml_parse_config_path(struct lmap *lmap, const char *path)
{
    int ret = 0;
    char filepath[PATH_MAX];
    struct dirent *dp;
    DIR *dfd;

    assert(path);

    dfd = opendir(path);
    if (!dfd) {
	if (errno == ENOTDIR) {
	    return lmap_xml_parse_config_file(lmap, path);
	} else {
	    lmap_err("cannot read config path '%s'", path);
	    return -1;
	}
    }

    while ((dp = readdir(dfd)) != NULL) {
	size_t len = strlen(dp->d_name);
	if (len < 5) {
	    continue;
	}
	if (strcmp(dp->d_name + len - 4, ".xml")) {
	    continue;
	}
	(void) snprintf(filepath, sizeof(filepath), "%s/%s", path, dp->d_name);
	if (lmap_xml_parse_config_file(lmap, filepath) < 0) {
	    ret = -1;
	    break;
	}
    }
    (void) closedir(dfd);

    return ret;
}

int
lmap_xml_parse_config_file(struct lmap *lmap, const char *file)
{
    int ret;
    xmlDocPtr doc = NULL;

    assert(file);

    doc = xmlParseFile(file);
    if (! doc) {
	lmap_err("cannot parse config file '%s'", file);
	ret = -1;
	goto exit;
    }

    ret = parse_config_doc(lmap, doc);

exit:
    if (doc) {
	xmlFreeDoc(doc);
	xmlCleanupParser();
    }
    return ret;
}

int
lmap_xml_parse_config_string(struct lmap *lmap, const char *string)
{
    int ret;
    xmlDocPtr doc = NULL;

    assert(string);

    doc = xmlParseMemory(string, strlen(string));
    if (! doc) {
	lmap_err("cannot parse from string");
	ret = -1;
	goto exit;
    }

    ret = parse_config_doc(lmap, doc);

exit:
    if (doc) {
	xmlFreeDoc(doc);
	xmlCleanupParser();
    }
    return ret;
}

static int
parse_state_doc(struct lmap *lmap, xmlDocPtr doc)
{
    return parse_control(lmap, doc, (PARSE_CONFIG_TRUE | PARSE_CONFIG_FALSE));
}

int
lmap_xml_parse_state_path(struct lmap *lmap, const char *path)
{
    int ret = 0;
    char filepath[PATH_MAX];
    struct dirent *dp;
    DIR *dfd;

    assert(path);

    dfd = opendir(path);
    if (!dfd) {
	if (errno == ENOTDIR) {
	    return lmap_xml_parse_state_file(lmap, path);
	} else {
	    lmap_err("cannot read capability path '%s'", path);
	    return -1;
	}
    }

    while ((dp = readdir(dfd)) != NULL) {
	size_t len = strlen(dp->d_name);
	if (len < 5) {
	    continue;
	}
	if (strcmp(dp->d_name + len - 4, ".xml")) {
	    continue;
	}
	(void) snprintf(filepath, sizeof(filepath), "%s/%s", path, dp->d_name);
	if (lmap_xml_parse_state_file(lmap, filepath) < 0) {
	    ret = -1;
	    break;
	}
    }
    (void) closedir(dfd);

    return ret;
}

int
lmap_xml_parse_state_file(struct lmap *lmap, const char *file)
{
    int ret;
    xmlDocPtr doc = NULL;

    assert(file);

    doc = xmlParseFile(file);
    if (! doc) {
	lmap_err("cannot parse state file '%s'", file);
	ret = -1;
	goto exit;
    }

    ret = parse_state_doc(lmap, doc);

exit:
    if (doc) {
	xmlFreeDoc(doc);
	xmlCleanupParser();
    }
    return ret;
}

int
lmap_xml_parse_state_string(struct lmap *lmap, const char *string)
{
    int ret;
    xmlDocPtr doc = NULL;

    assert(string);

    doc = xmlParseMemory(string, strlen(string));
    if (! doc) {
	lmap_err("cannot parse from string");
	ret = -1;
	goto exit;
    }

    ret = parse_state_doc(lmap, doc);

exit:
    if (doc) {
	xmlFreeDoc(doc);
	xmlCleanupParser();
    }
    return ret;
}

static int
parse_report(struct lmap *lmap, xmlXPathContextPtr ctx)
{
    int i, j;
    xmlXPathObjectPtr result;

    const char *xpath = "//lmapr:report/lmapr:*";

    struct {
	char *name;
	int (*func)(struct agent *a, const char *c);
    } tab[] = {
	{ .name = "date",
	  .func = lmap_agent_set_report_date },
	{ .name = "agent-id",
	  .func = lmap_agent_set_agent_id },
	{ .name = "group-id",
	  .func = lmap_agent_set_group_id },
	{ .name = "measurement-point",
	  .func = lmap_agent_set_measurement_point },
	{ .name = NULL, .func = NULL }
    };

    assert(lmap);

    result = xmlXPathEvalExpression(BAD_CAST xpath, ctx);
    if (! result) {
	lmap_err("error in xpath expression '%s'", xpath);
	return -1;
    }

    if (!result->nodesetval || !result->nodesetval->nodeNr) {
	xmlXPathFreeObject(result);
	return 0;
    }

    if (! lmap->agent) {
	lmap->agent = lmap_agent_new();
	if (! lmap->agent) {
	    xmlXPathFreeObject(result);
	    return -1;
	}
    }

    for (i = 0; result->nodesetval && i < result->nodesetval->nodeNr; i++) {
	xmlNodePtr node = result->nodesetval->nodeTab[i];

	for (j = 0; tab[j].name; j++) {
	    if (!xmlStrcmp(node->name, BAD_CAST tab[j].name)) {
		xmlChar *content = xmlNodeGetContent(node);
		if (!xmlStrcmp(node->name, BAD_CAST "agent-id")) {
		    lmap_agent_set_report_agent_id(lmap->agent, "true");
		}
		if (!xmlStrcmp(node->name, BAD_CAST "group-id")) {
		    lmap_agent_set_report_group_id(lmap->agent, "true");
		}
		if (!xmlStrcmp(node->name, BAD_CAST "measurement-point")) {
		    lmap_agent_set_report_measurement_point(lmap->agent, "true");
		}
		if (tab[j].func) {
		    tab[j].func(lmap->agent, (char *) content);
		}
		if (content) {
		    xmlFree(content);
		}
		break;
	    }
	}
	if (! tab[j].name) {
	    lmap_wrn("unexpected element '%s'", node->name);
	}
    }
    xmlXPathFreeObject(result);

    return 0;
}

static struct value *
parse_value(xmlNodePtr value_node)
{
    struct value *value;
    xmlChar *content;

    value = lmap_value_new();
    if (! value) {
	return NULL;
    }

    content = xmlNodeGetContent(value_node);
    lmap_value_set_value(value, (char *) content);
    xmlFree(content);

    return value;
}

static struct row *
parse_row(xmlNodePtr row_node)
{
    xmlNodePtr node;
    struct row *row;

    row = lmap_row_new();
    if (! row) {
	return NULL;
    }

    for (node = xmlFirstElementChild(row_node);
	 node; node = xmlNextElementSibling(node)) {

	if (node->ns != row_node->ns) continue;

	if (!xmlStrcmp(node->name, BAD_CAST "value")) {
	    struct value *value = parse_value(node);
	    lmap_row_add_value(row, value);
	}
    }
    return row;
}

static struct table *
parse_table(xmlNodePtr table_node)
{
    xmlNodePtr node;
    struct table *tab;

    tab = lmap_table_new();
    if (! tab) {
	return NULL;
    }

    for (node = xmlFirstElementChild(table_node);
	 node; node = xmlNextElementSibling(node)) {

	if (node->ns != table_node->ns) continue;

	if (!xmlStrcmp(node->name, BAD_CAST "row")) {
	    struct row *row = parse_row(node);
	    lmap_table_add_row(tab, row);
	}
    }
    return tab;
}

static struct result *
parse_result(xmlNodePtr result_node)
{
    int j;
    xmlNodePtr node;
    struct result *res;

    struct {
	char *name;
	int (*func)(struct result *r, const char *c);
    } tab[] = {
	{ .name = "schedule",
	  .func = lmap_result_set_schedule },
	{ .name = "action",
	  .func = lmap_result_set_action },
	{ .name = "task",
	  .func = lmap_result_set_task },
	{ .name = "tag",
	  .func = lmap_result_add_tag },
	{ .name = "event",
	  .func = lmap_result_set_event },
	{ .name = "start",
	  .func = lmap_result_set_start },
	{ .name = "end",
	  .func = lmap_result_set_end },
	{ .name = "cycle-number",
	  .func = lmap_result_set_cycle_number },
	{ .name = "status",
	  .func = lmap_result_set_status },
	{ .name = NULL, .func = NULL }
    };

    res = lmap_result_new();
    if (! res) {
	return NULL;
    }

    for (node = xmlFirstElementChild(result_node);
	 node; node = xmlNextElementSibling(node)) {

	if (node->ns != result_node->ns) continue;

	if (!xmlStrcmp(node->name, BAD_CAST "option")) {
	    struct option *option = parse_option(node,
		      		 PARSE_CONFIG_TRUE | PARSE_CONFIG_FALSE);
	    lmap_result_add_option(res, option);
	    continue;
	}

	if (!xmlStrcmp(node->name, BAD_CAST "table")) {
	    struct table *tab = parse_table(node);
	    lmap_result_add_table(res, tab);
	    continue;
	}

	for (j = 0; tab[j].name; j++) {
	    if (!xmlStrcmp(node->name, BAD_CAST tab[j].name)) {
		xmlChar *content = xmlNodeGetContent(node);
		tab[j].func(res, (char *) content);
		if (content) {
		    xmlFree(content);
		}
		break;
	    }
	}
	if (! tab[j].name) {
	    lmap_wrn("unexpected element '%s'", node->name);
	}
    }

    return res;
}

/**
 * @brief Parses the schedule information
 * @details Function to parse the schedule information from the XML config
 * file
 * @return 0 on success, -1 on error
 */
static int
parse_results(struct lmap *lmap, xmlXPathContextPtr ctx)
{
    int i;
    xmlXPathObjectPtr result;
    struct result *res;

    const char *xpath = "//lmapr:report/lmapr:result";

    assert(lmap);

    result = xmlXPathEvalExpression(BAD_CAST xpath, ctx);
    if (! result) {
	lmap_err("error in xpath expression '%s'", xpath);
	return -1;
    }

    for (i = 0; result->nodesetval && i < result->nodesetval->nodeNr; i++) {
	res = parse_result(result->nodesetval->nodeTab[i]);
	if (res) {
	    lmap_add_result(lmap, res);
	}
    }

    xmlXPathFreeObject(result);
    return 0;
}

static int
parse_report_doc(struct lmap *lmap, xmlDocPtr doc)
{
    int i, ret;
    xmlXPathContextPtr ctx = NULL;

    struct {
	int (*parse)(struct lmap *lmap, xmlXPathContextPtr ctx);
    } tab[] = {
	{ parse_report },
	{ parse_results },
	{ NULL }
    };

    assert(lmap && doc);

    ctx = xmlXPathNewContext(doc);
    if (! ctx) {
	lmap_err("cannot create xpath context");
	ret = -1;
	goto exit;
    }

    ret = xmlXPathRegisterNs(ctx, BAD_CAST LMAPR_XML_PREFIX,
			     BAD_CAST LMAPR_XML_NAMESPACE);
    if (ret != 0) {
	lmap_err("cannot register xpath namespace '%s'", LMAPC_XML_NAMESPACE);
	ret = -1;
	goto exit;
    }

    for (i = 0; tab[i].parse; i++) {
	ret = tab[i].parse(lmap, ctx);
	if (ret != 0) {
	    goto exit;
	}
    }

exit:
    if (ctx) {
	xmlXPathFreeContext(ctx);
    }
    return ret;
}

int
lmap_xml_parse_report_file(struct lmap *lmap, const char *file)
{
    int ret;
    xmlDocPtr doc = NULL;

    assert(file);

    doc = xmlParseFile(file);
    if (! doc) {
	lmap_err("cannot parse state file '%s'", file);
	ret = -1;
	goto exit;
    }

    ret = parse_report_doc(lmap, doc);

exit:
    if (doc) {
	xmlFreeDoc(doc);
	xmlCleanupParser();
    }
    return ret;
}

int
lmap_xml_parse_report_string(struct lmap *lmap, const char *string)
{
    int ret;
    xmlDocPtr doc = NULL;

    assert(string);

    doc = xmlParseMemory(string, strlen(string));
    if (! doc) {
	lmap_err("cannot parse from string");
	ret = -1;
	goto exit;
    }

    ret = parse_report_doc(lmap, doc);

exit:
    if (doc) {
	xmlFreeDoc(doc);
	xmlCleanupParser();
    }
    return ret;
}

static void
render_leaf(xmlNodePtr root, xmlNsPtr ns, char *name, char *content)
{
    assert(root && ns);

    if (name && content) {
	xmlNewChild(root, ns, BAD_CAST name, BAD_CAST content);
    }
}

static void
render_leaf_int32(xmlNodePtr root, xmlNsPtr ns, char *name, int32_t value)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%" PRIi32, value);
    render_leaf(root, ns, name, buf);
}

static void
render_leaf_uint32(xmlNodePtr root, xmlNsPtr ns, char *name, uint32_t value)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%" PRIu32, value);
    render_leaf(root, ns, name, buf);
}

static void
render_leaf_uint64(xmlNodePtr root, xmlNsPtr ns, char *name, uint64_t value)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%" PRIu64, value);
    render_leaf(root, ns, name, buf);
}

static void
render_leaf_datetime(xmlNodePtr root, xmlNsPtr ns, char *name, time_t *tp)
{
    char buf[32];
    struct tm *tmp;

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

    render_leaf(root, ns, name, buf);
}

static void
render_leaf_months(xmlNodePtr root, xmlNsPtr ns, char *name, uint16_t months)
{
    int i;
    struct {
	char *name;
	uint16_t value;
    } tab[] = {
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

    if (months == UINT16_MAX) {
	render_leaf(root, ns, name, "*");
	return;
    }

    for (i = 0; tab[i].name; i++) {
	if (months & tab[i].value) {
	    render_leaf(root, ns, name, tab[i].name);
	}
    }
}

static void
render_leaf_days_of_month(xmlNodePtr root, xmlNsPtr ns, char *name, uint32_t days_of_month)
{
    int i;

    if (days_of_month == UINT32_MAX) {
	render_leaf(root, ns, name, "*");
	return;
    }

    for (i = 1; i < 32; i++) {
	if (days_of_month & (1 << i)) {
	    render_leaf_int32(root, ns, name, i);
	}
    }
}

static void
render_leaf_days_of_week(xmlNodePtr root, xmlNsPtr ns, char *name, uint8_t days_of_week)
{
    int i;
    struct {
	char *name;
	uint8_t value;
    } tab[] = {
	{ "monday",	(1 << 0) },
	{ "tuesday",	(1 << 1) },
	{ "wednesday",	(1 << 2) },
	{ "thursday",	(1 << 3) },
	{ "friday",	(1 << 4) },
	{ "saturday",	(1 << 5) },
	{ "sunday",	(1 << 6) },
	{ NULL, 0 }
    };

    if (days_of_week == UINT8_MAX) {
	render_leaf(root, ns, name, "*");
	return;
    }

    for (i = 0; tab[i].name; i++) {
	if (days_of_week & tab[i].value) {
	    render_leaf(root, ns, name, tab[i].name);
	}
    }
}

static void
render_leaf_hours(xmlNodePtr root, xmlNsPtr ns, char *name, uint32_t hours)
{
    int i;

    if (hours == UINT32_MAX) {
	render_leaf(root, ns, name, "*");
	return;
    }

    for (i = 0; i < 24; i++) {
	if (hours & (1 << i)) {
	    render_leaf_int32(root, ns, name, i);
	}
    }
}

static void
render_leaf_minsecs(xmlNodePtr root, xmlNsPtr ns, char *name, uint64_t minsecs)
{
    int i;

    if (minsecs == UINT64_MAX) {
	render_leaf(root, ns, name, "*");
	return;
    }

    for (i = 0; i < 60; i++) {
	if (minsecs & (1ull << i)) {
	    render_leaf_int32(root, ns, name, i);
	}
    }
}

static void
render_registry(struct registry *registry, xmlNodePtr root, xmlNsPtr ns)
{
    struct tag *tag;
    xmlNodePtr node;

    if (! registry) {
	return;
    }

    node = xmlNewChild(root, ns, BAD_CAST "function", NULL);
    if (! node) {
	return;
    }

    render_leaf(node, ns, "uri", registry->uri);
    for (tag = registry->roles; tag; tag = tag->next) {
	render_leaf(node, ns, "role", tag->tag);
    }
}

static void
render_option(struct option *option, xmlNodePtr root, xmlNsPtr ns)
{
    xmlNodePtr node;

    if (! option) {
	return;
    }

    node = xmlNewChild(root, ns, BAD_CAST "option", NULL);

    if (! node) {
	return;
    }

    render_leaf(node, ns, "id", option->id);
    render_leaf(node, ns, "name", option->name);
    render_leaf(node, ns, "value", option->value);
}

static void
render_agent(struct agent *agent, xmlNodePtr root, xmlNsPtr ns, int what)
{
    xmlNodePtr node;

    if (! agent) {
	return;
    }

    node = xmlNewChild(root, ns, BAD_CAST "agent", NULL);
    if (! node) {
	return;
    }

    if (what & RENDER_CONFIG_TRUE) {
	render_leaf(node, ns, "agent-id", agent->agent_id);
	render_leaf(node, ns, "group-id", agent->group_id);
	render_leaf(node, ns, "measurement-point", agent->measurement_point);
	if (agent->flags & LMAP_AGENT_FLAG_REPORT_AGENT_ID_SET) {
	    render_leaf(node, ns, "report-agent-id",
			agent->report_agent_id ? "true" : "false");
	}
	if (agent->flags & LMAP_AGENT_FLAG_REPORT_GROUP_ID_SET) {
	    render_leaf(node, ns, "report-group-id",
			agent->report_group_id ? "true" : "false");
	}
	if (agent->flags & LMAP_AGENT_FLAG_REPORT_MEASUREMENT_POINT_SET) {
	    render_leaf(node, ns, "report-measurement-point",
			agent->report_measurement_point ? "true" : "false");
	}
	if (agent->flags & LMAP_AGENT_FLAG_CONTROLLER_TIMEOUT_SET) {
	    render_leaf_uint32(node, ns, "controller-timeout",
			       agent->controller_timeout);
	}
    }
    if (what & RENDER_CONFIG_FALSE) {
	if (agent->last_started) {
	    render_leaf_datetime(node, ns, "last-started", &agent->last_started);
	}
    }
}

static void
render_agent_report(struct agent *agent, xmlNodePtr root, xmlNsPtr ns)
{
    if (! agent) {
	return;
    }

    render_leaf_datetime(root, ns, "date", &agent->report_date);
    if (agent->agent_id && agent->report_agent_id) {
	render_leaf(root, ns, "agent-id", agent->agent_id);
    }
    if (agent->group_id && agent->report_group_id) {
	render_leaf(root, ns, "group-id", agent->group_id);
    }
    if (agent->measurement_point && agent->report_measurement_point) {
	render_leaf(root, ns, "measurement-point", agent->measurement_point);
    }
}

static void
render_action(struct action *action, xmlNodePtr root, xmlNsPtr ns, int what)
{
    struct option *option;
    struct tag *tag;
    xmlNodePtr node;

    if (! action) {
	return;
    }

    node = xmlNewChild(root, ns, BAD_CAST "action", NULL);
    if (! node) {
	return;
    }

    render_leaf(node, ns, "name", action->name);
    if (what & RENDER_CONFIG_TRUE) {
	render_leaf(node, ns, "task", action->task);
	for (tag = action->destinations; tag; tag = tag->next) {
	    render_leaf(node, ns, "destination", tag->tag);
	}
	for (option = action->options; option; option = option->next) {
	    render_option(option, node, ns);
	}
	for (tag = action->tags; tag; tag = tag->next) {
	    render_leaf(node, ns, "tag", tag->tag);
	}
	for (tag = action->suppression_tags; tag; tag = tag->next) {
	    render_leaf(node, ns, "suppression-tag", tag->tag);
	}
    }
    if (what & RENDER_CONFIG_FALSE) {
	char *state = NULL;
	switch (action->state) {
	case LMAP_SCHEDULE_STATE_ENABLED:
	    state = "enabled";
	    break;
	case LMAP_SCHEDULE_STATE_DISABLED:
	    state = "disabled";
	    break;
	case LMAP_SCHEDULE_STATE_RUNNING:
	    state = "running";
	    break;
	case LMAP_SCHEDULE_STATE_SUPPRESSED:
	    state = "suppressed";
	    break;
	}
	if (state) {
	    render_leaf(node, ns, "state", state);
	}

	render_leaf_uint64(node, ns, "storage", action->storage);
	render_leaf_uint32(node, ns, "invocations", action->cnt_invocations);
	render_leaf_uint32(node, ns, "suppressions", action->cnt_suppressions);
	render_leaf_uint32(node, ns, "overlaps", action->cnt_overlaps);
	render_leaf_uint32(node, ns, "failures", action->cnt_failures);

	if (action->last_invocation) {
	    render_leaf_datetime(node, ns, "last-invocation",
				 &action->last_invocation);
	}
	if (action->last_completion) {
	    render_leaf_datetime(node, ns, "last-completion",
				 &action->last_completion);
	    render_leaf_int32(node, ns, "last-status",
			      action->last_status);
	    if (action->last_message) {
		render_leaf(node, ns, "last-message",
			    action->last_message);
	    }
	}
	if (action->last_failed_completion) {
	    render_leaf_datetime(node, ns, "last-failed-completion",
				 &action->last_failed_completion);
	    render_leaf_int32(node, ns, "last-failed-status",
			      action->last_failed_status);
	    if (action->last_failed_message) {
		render_leaf(node, ns, "last-failed-message",
			    action->last_failed_message);
	    }
	}
    }
}

static void
render_schedules(struct schedule *schedule, xmlNodePtr root, xmlNsPtr ns, int what)
{
    struct tag *tag;
    struct action *action;
    xmlNodePtr node;

    if (! schedule) {
	return;
    }

    root = xmlNewChild(root, ns, BAD_CAST "schedules", NULL);
    if (!root) {
	return;
    }

    for (; schedule; schedule = schedule->next) {
	node = xmlNewChild(root, ns, BAD_CAST "schedule", NULL);
	if (! node) {
	    continue;
	}
	render_leaf(node, ns, "name", schedule->name);
	if (what & RENDER_CONFIG_TRUE) {
	    render_leaf(node, ns, "start", schedule->start);
	    if (schedule->flags & LMAP_SCHEDULE_FLAG_END_SET) {
		render_leaf(node, ns, "end", schedule->end);
	    }
	    if (schedule->flags & LMAP_SCHEDULE_FLAG_DURATION_SET) {
		render_leaf_uint64(node, ns, "duration", schedule->duration);
	    }
	    if (schedule->flags & LMAP_SCHEDULE_FLAG_EXEC_MODE_SET) {
		char *mode = NULL;
		switch (schedule->mode) {
		case LMAP_SCHEDULE_EXEC_MODE_SEQUENTIAL:
		    mode = "sequential";
		    break;
		case LMAP_SCHEDULE_EXEC_MODE_PARALLEL:
		    mode = "parallel";
		    break;
		case LMAP_SCHEDULE_EXEC_MODE_PIPELINED:
		    mode = "pipelined";
		    break;
		}
		if (mode) {
		    render_leaf(node, ns, "execution-mode", mode);
		}
	    }
	    for (tag = schedule->tags; tag; tag = tag->next) {
		render_leaf(node, ns, "tag", tag->tag);
	    }
	    for (tag = schedule->suppression_tags; tag; tag = tag->next) {
		render_leaf(node, ns, "suppression-tag", tag->tag);
	    }
	}
	if (what & RENDER_CONFIG_FALSE) {
	    char *state = NULL;
	    switch (schedule->state) {
	    case LMAP_SCHEDULE_STATE_ENABLED:
		state = "enabled";
		break;
	    case LMAP_SCHEDULE_STATE_DISABLED:
		state = "disabled";
		break;
	    case LMAP_SCHEDULE_STATE_RUNNING:
		state = "running";
	    break;
	    case LMAP_SCHEDULE_STATE_SUPPRESSED:
		state = "suppressed";
		break;
	    }
	    if (state) {
		render_leaf(node, ns, "state", state);
	    }

	    render_leaf_uint64(node, ns, "storage", schedule->storage);
	    render_leaf_uint32(node, ns, "invocations", schedule->cnt_invocations);
	    render_leaf_uint32(node, ns, "suppressions", schedule->cnt_suppressions);
	    render_leaf_uint32(node, ns, "overlaps", schedule->cnt_overlaps);
	    render_leaf_uint32(node, ns, "failures", schedule->cnt_failures);

	    if (schedule->last_invocation) {
		render_leaf_datetime(node, ns, "last-invocation",
				     &schedule->last_invocation);
	    }
	}

	for (action = schedule->actions; action; action = action->next) {
	    render_action(action, node, ns, what);
	}
    }
}

static void
render_suppressions(struct supp *supp, xmlNodePtr root, xmlNsPtr ns, int what)
{
    struct tag *tag;
    xmlNodePtr node;

    if (! supp) {
	return;
    }

    root = xmlNewChild(root, ns, BAD_CAST "suppressions", NULL);
    if (!root) {
	return;
    }

    for (; supp; supp = supp->next) {
	node = xmlNewChild(root, ns, BAD_CAST "suppression", NULL);
	if (! node) {
	    continue;
	}
	render_leaf(node, ns, "name", supp->name);
	if (what & RENDER_CONFIG_TRUE) {
	    render_leaf(node, ns, "start", supp->start);
	    render_leaf(node, ns, "end", supp->end);
	    for (tag = supp->match; tag; tag = tag->next) {
		render_leaf(node, ns, "match", tag->tag);
	    }
	    if (supp->flags & LMAP_SUPP_FLAG_STOP_RUNNING_SET) {
		render_leaf(node, ns, "stop-running",
			    supp->stop_running ? "true" : "false");
	    }
	}
	if (what & RENDER_CONFIG_FALSE) {
	    char *state = NULL;
	    switch (supp->state) {
	    case LMAP_SUPP_STATE_ENABLED:
		state = "enabled";
		break;
	    case LMAP_SUPP_STATE_DISABLED:
		state = "disabled";
		break;
	    case LMAP_SUPP_STATE_ACTIVE:
		state = "active";
		break;
	    }
	    if (state) {
		render_leaf(node, ns, "state", state);
	    }
	}
    }
}

static void
render_tasks(struct task *task, xmlNodePtr root, xmlNsPtr ns, int what)
{
    struct registry *registry;
    struct option *option;
    struct tag *tag;
    xmlNodePtr node;

    if (! task) {
	return;
    }

    root = xmlNewChild(root, ns, BAD_CAST "tasks", NULL);
    if (!root) {
	return;
    }

    for (; task; task = task->next) {
	node = xmlNewChild(root, ns, BAD_CAST "task", NULL);
	if (! node) {
	    continue;
	}
	render_leaf(node, ns, "name", task->name);
	for (registry = task->registries; registry; registry = registry->next) {
	    render_registry(registry, node, ns);
	}
	if (what & RENDER_CONFIG_FALSE) {
	    render_leaf(node, ns, "version", task->version);
	}
	render_leaf(node, ns, "program", task->program);
	if (what & RENDER_CONFIG_TRUE) {
	    for (option = task->options; option; option = option->next) {
		render_option(option, node, ns);
	    }
	    for (tag = task->tags; tag; tag = tag->next) {
		render_leaf(node, ns, "tag", tag->tag);
	    }
	}
    }
}

static void
render_capabilities(struct capability *capability, xmlNodePtr root, xmlNsPtr ns, int what)
{
    struct tag *tag;
    xmlNodePtr node;

    if (! capability) {
	return;
    }

    if (! (what & RENDER_CONFIG_FALSE)) {
	return;
    }

    if (! capability->version && ! capability->tags && ! capability->tasks) {
	return;
    }

    node = xmlNewChild(root, ns, BAD_CAST "capabilities", NULL);
    if (! node) {
	return;
    }

    if (capability->version) {
	render_leaf(node, ns, "version", capability->version);
    }
    for (tag = capability->tags; tag; tag = tag->next) {
	render_leaf(node, ns, "tag", tag->tag);
    }
    render_tasks(capability->tasks, node, ns, what);
}

static void
render_events(struct event *event, xmlNodePtr root, xmlNsPtr ns, int what)
{
    xmlNodePtr node, subnode;

    if (! event) {
	return;
    }

    root = xmlNewChild(root, ns, BAD_CAST "events", NULL);
    if (!root) {
	return;
    }

    for (; event; event = event->next) {
	node = xmlNewChild(root, ns, BAD_CAST "event", NULL);
	if (! node) {
	    continue;
	}
	render_leaf(node, ns, "name", event->name);
	if (what & RENDER_CONFIG_TRUE) {
	    if (event->flags & LMAP_EVENT_FLAG_RANDOM_SPREAD_SET) {
		render_leaf_int32(node, ns, "random-spread", event->random_spread);
	    }
	    if (event->flags & LMAP_EVENT_FLAG_CYCLE_INTERVAL_SET) {
		render_leaf_int32(node, ns, "cycle-interval", event->cycle_interval);
	    }
	    switch (event->type) {
	    case LMAP_EVENT_TYPE_PERIODIC:
		subnode = xmlNewChild(node, ns, BAD_CAST "periodic", NULL);
		if (! subnode) {
		    continue;
		}
		if (event->flags & LMAP_EVENT_FLAG_INTERVAL_SET) {
		    render_leaf_uint32(subnode, ns, "interval", event->interval);
		}
		if (event->flags & LMAP_EVENT_FLAG_START_SET) {
		    render_leaf_datetime(subnode, ns, "start", &event->start);
		}
		if (event->flags & LMAP_EVENT_FLAG_END_SET) {
		    render_leaf_datetime(subnode, ns, "end", &event->end);
		}
		break;
	    case LMAP_EVENT_TYPE_CALENDAR:
		subnode = xmlNewChild(node, ns, BAD_CAST "calendar", NULL);
		if (! subnode) {
		    continue;
		}
		if (event->months) {
		    render_leaf_months(subnode, ns, "month", event->months);
		}
		if (event->days_of_month) {
		    render_leaf_days_of_month(subnode, ns, "day-of-month", event->days_of_month);
		}
		if (event->days_of_week) {
		    render_leaf_days_of_week(subnode, ns, "day-of-week", event->days_of_week);
		}
		if (event->hours) {
		    render_leaf_hours(subnode, ns, "hour", event->hours);
		}
		if (event->minutes) {
		    render_leaf_minsecs(subnode, ns, "minute", event->minutes);
		}
		if (event->seconds) {
		    render_leaf_minsecs(subnode, ns, "second", event->seconds);
		}
		if (event->flags & LMAP_EVENT_FLAG_TIMEZONE_OFFSET_SET) {
		    char buf[42];
		    char c = (event->timezone_offset < 0) ? '-' : '+';
		    int16_t offset = event->timezone_offset;
		    offset = (offset < 0) ? -1 * offset : offset;
		    snprintf(buf, sizeof(buf), "%c%02d:%02d",
			     c, offset / 60, offset % 60);
		    render_leaf(subnode, ns, "timezone-offset", buf);
		}
		if (event->flags & LMAP_EVENT_FLAG_START_SET) {
		    render_leaf_datetime(subnode, ns, "start", &event->start);
		}
		if (event->flags & LMAP_EVENT_FLAG_END_SET) {
		    render_leaf_datetime(subnode, ns, "end", &event->end);
		}
		break;
	    case LMAP_EVENT_TYPE_ONE_OFF:
		subnode = xmlNewChild(node, ns, BAD_CAST "one-off", NULL);
		if (! subnode) {
		    continue;
		}
		if (event->flags & LMAP_EVENT_FLAG_START_SET) {
		    render_leaf_datetime(subnode, ns, "time", &event->start);
		}
		break;
	    case LMAP_EVENT_TYPE_STARTUP:
		render_leaf(node, ns, "startup", "");
		break;
	    case LMAP_EVENT_TYPE_IMMEDIATE:
		render_leaf(node, ns, "immediate", "");
		break;
	    case LMAP_EVENT_TYPE_CONTROLLER_LOST:
		render_leaf(node, ns, "controller-lost", "");
		break;
	    case LMAP_EVENT_TYPE_CONTROLLER_CONNECTED:
		render_leaf(node, ns, "controller-connected", "");
		break;
	    }
	}
    }
}

static void
render_row(struct row *row, xmlNodePtr root, xmlNsPtr ns)
{
    xmlNodePtr node;
    struct value *val;

    node = xmlNewChild(root, ns, BAD_CAST "row", NULL);
    if (!node) {
	return;
    }

    for (val = row->values; val; val = val->next) {
	render_leaf(node, ns, "value", val->value ? val->value : "");
    }
}

static void
render_table(struct table *tab, xmlNodePtr root, xmlNsPtr ns)
{
    xmlNodePtr node;
    struct row *row;

    node = xmlNewChild(root, ns, BAD_CAST "table", NULL);
    if (!node) {
	return;
    }

    for (row = tab->rows; row; row = row->next) {
	render_row(row, node, ns);
    }
}

static void
render_result(struct result *res, xmlNodePtr root, xmlNsPtr ns)
{
    xmlNodePtr node;
    struct option *option;
    struct tag *tag;
    struct table *tab;

    node = xmlNewChild(root, ns, BAD_CAST "result", NULL);
    if (!node) {
	return;
    }

    render_leaf(node, ns, "schedule", res->schedule);
    render_leaf(node, ns, "action", res->action);
    render_leaf(node, ns, "task", res->task);
    for (option = res->options; option; option = option->next) {
	render_option(option, node, ns);
    }
    for (tag = res->tags; tag; tag = tag->next) {
	render_leaf(node, ns, "tag", tag->tag);
    }

    if (res->event) {
	render_leaf_datetime(node, ns, "event", &res->start);
    }

    if (res->start) {
	render_leaf_datetime(node, ns, "start", &res->start);
    }

    if (res->end) {
	render_leaf_datetime(node, ns, "end", &res->end);
    }

    if (res->cycle_number) {
	render_leaf(node, ns, "cycle-number", res->cycle_number);
    }

    if (res->flags & LMAP_RESULT_FLAG_STATUS_SET) {
	render_leaf_int32(node, ns, "status", res->status);
    }

    for (tab = res->tables; tab; tab = tab->next) {
	render_table(tab, node, ns);
    }
}

static char*
render_control(struct lmap *lmap, int what)
{
    xmlDocPtr doc;
    xmlNodePtr root, node;
    xmlNsPtr ns = NULL;
    char *config = NULL;
    xmlChar *p = NULL;
    int len = 0;

    assert(lmap);

    doc = xmlNewDoc(BAD_CAST "1.0");
    if (doc == NULL) {
	goto exit;
    }

    root = xmlNewNode(NULL, BAD_CAST
		      ((what & RENDER_CONFIG_FALSE) ? "data" : "config"));
    if (! root) {
	goto exit;
    }
    xmlDocSetRootElement(doc, root);

    ns = xmlNewNs(root, BAD_CAST LMAPC_XML_NAMESPACE, BAD_CAST LMAPC_XML_PREFIX);
    if (ns == NULL) {
	goto exit;
    }

    node = xmlNewChild(root, ns, BAD_CAST "lmap", NULL);
    if (! node) {
	goto exit;
    }

    render_capabilities(lmap->capabilities, node, ns, what);
    render_agent(lmap->agent, node, ns, what);
    render_tasks(lmap->tasks, node, ns, what);
    render_schedules(lmap->schedules, node, ns, what);
    render_suppressions(lmap->supps, node, ns, what);
    render_events(lmap->events, node, ns, what);

    xmlDocDumpFormatMemoryEnc(doc, &p, &len, "UTF-8", 1);
    if (p) {
	config = strdup((char *) p);
	xmlFree(p);
    }

exit:
    if (doc) xmlFreeDoc(doc);
    xmlCleanupParser();
    return config;
}

/**
 * @brief Returns an XML rendering of the lmap configuration
 *
 * This function renders the current lmap configuration into an XML
 * document according to the IETF's LMAP YANG data model.
 *
 * @param lmap The pointer to the lmap config to be rendered.
 * @return An XML document as a string that must be freed by the
 *         caller or NULL on error
 */

char *
lmap_xml_render_config(struct lmap *lmap)
{
    return render_control(lmap, RENDER_CONFIG_TRUE);
}

/**
 * @brief Returns an XML rendering of the lmap state
 *
 * This function renders the current lmap state into an XML document
 * according to the IETF's LMAP YANG data model.
 *
 * @param lmap The pointer to the lmap state to be rendered.
 * @return An XML document as a string that must be freed by the
 *         caller or NULL on error
 */

char *
lmap_xml_render_state(struct lmap *lmap)
{
    return render_control(lmap, (RENDER_CONFIG_TRUE | RENDER_CONFIG_FALSE));
}

/**
 * @brief Returns an XML rendering of the lmap report
 *
 * This function renders the current lmap report into an XML document
 * according to the IETF's LMAP YANG data model.
 *
 * @param lmap The pointer to the lmap report to be rendered.
 * @return An XML document as a string that must be freed by the
 *         caller or NULL on error
 */

char *
lmap_xml_render_report(struct lmap *lmap)
{
    xmlDocPtr doc;
    xmlNodePtr root, node;
    xmlNsPtr ns = NULL;
    char *report = NULL;
    xmlChar *p = NULL;
    int len = 0;
    struct result *res;

    assert(lmap);

    doc = xmlNewDoc(BAD_CAST "1.0");
    if (doc == NULL) {
	goto exit;
    }

    root = xmlNewNode(NULL, BAD_CAST "rpc");
    if (! root) {
	goto exit;
    }
    xmlDocSetRootElement(doc, root);

    ns = xmlNewNs(root, BAD_CAST LMAPR_XML_NAMESPACE, BAD_CAST LMAPR_XML_PREFIX);
    if (ns == NULL) {
	goto exit;
    }

    node = xmlNewChild(root, ns, BAD_CAST "report", NULL);
    if (! node) {
	goto exit;
    }
    render_agent_report(lmap->agent, node, ns);
    for (res = lmap->results; res; res = res->next) {
	render_result(res, node, ns);
    }
    xmlDocDumpFormatMemoryEnc(doc, &p, &len, "UTF-8", 1);
    if (p) {
	report = strdup((char *) p);
	xmlFree(p);
    }

exit:
    if (doc) xmlFreeDoc(doc);
    xmlCleanupParser();
    return report;
}
