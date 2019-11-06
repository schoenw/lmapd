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

#include <json.h>

#include "lmap.h"
#include "utils.h"
#include "json-io.h"


static void
render_leaf(json_object *jobj, char *name, char *content)
{
    assert(jobj);
    
    if (name && content) {
	json_object_object_add(jobj, name, json_object_new_string(content));
    }
}

static void
render_leaf_int32(json_object *jobj, char *name, int32_t value)
{
    json_object_object_add(jobj, name, json_object_new_int(value));
}

static void
render_leaf_datetime(json_object *jobj, char *name, time_t *tp)
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
    
    render_leaf(jobj, name, buf); 
}

static void
render_option(struct option *option, json_object *jobj)
{
    json_object *robj;
    
    if (! option) {
	return;
    }

    robj = json_object_new_object();
    if (! robj) {
	return;
    }
    json_object_array_add(jobj, robj);

    render_leaf(robj, "id", option->id);
    render_leaf(robj, "name", option->name);
    render_leaf(robj, "value", option->value);
}

static void
render_agent_report(struct agent *agent, json_object *jobj)
{
    if (! agent) {
	return;
    }

    render_leaf_datetime(jobj, "date", &agent->report_date);
    if (agent->agent_id && agent->report_agent_id) {
	render_leaf(jobj, "agent-id", agent->agent_id);
    }
    if (agent->group_id && agent->report_group_id) {
	render_leaf(jobj, "group-id", agent->group_id);
    }
    if (agent->measurement_point && agent->report_measurement_point) {
	render_leaf(jobj, "measurement-point", agent->measurement_point);
    }
}

static void
render_row(struct row *row, json_object *jobj)
{
    json_object *robj;
    json_object *aobj;
    struct value *val;

    robj = json_object_new_object();
    if (! robj) {
	return;
    }
    json_object_array_add(jobj, robj);

    aobj = json_object_new_array();
    if (!aobj) {
	return;
    }

    json_object_object_add(robj, "value", aobj);
    for (val = row->values; val; val = val->next) {
	json_object_array_add(aobj, json_object_new_string(val->value ? val->value : ""));
    }
}

static void
render_table(struct table *tab, json_object *jobj)
{
    json_object *robj;
    json_object *aobj;
    struct row *row;

    robj = json_object_new_object();
    if (! robj) {
	return;
    }
    json_object_array_add(jobj, robj);

    aobj = json_object_new_array();
    if (! aobj) {
	return;
    }

    json_object_object_add(robj, "row", aobj);
    for (row = tab->rows; row; row = row->next) {
	render_row(row, aobj);
    }
}

static void
render_result(struct result *res, json_object *jobj)
{
    json_object *robj;
    json_object *aobj;
    struct option *option;
    struct tag *tag;
    struct table *tab;
    
    robj = json_object_new_object();
    if (! robj) {
	return;
    }
    json_object_array_add(jobj, robj);
    
    render_leaf(robj, "schedule", res->schedule);
    render_leaf(robj, "action", res->action);
    render_leaf(robj, "task", res->task);
    aobj = json_object_new_array();
    if (aobj) {
	json_object_object_add(robj, "option", aobj);
	for (option = res->options; option; option = option->next) {
	    render_option(option, aobj);
	}
    }
    aobj = json_object_new_array();
    if (aobj) {
	json_object_object_add(robj, "tag", aobj);
	for (tag = res->tags; tag; tag = tag->next) {
	    json_object_array_add(aobj, json_object_new_string(tag->tag));
	}
    }
    
    if (res->event) {
	render_leaf_datetime(robj, "event", &res->event);
    }
    
    if (res->start) {
	render_leaf_datetime(robj, "start", &res->start);
    }
    
    if (res->end) {
	render_leaf_datetime(robj, "end", &res->end);
    }

    if (res->cycle_number) {
	render_leaf(robj, "cycle-number", res->cycle_number);
    }

    if (res->flags & LMAP_RESULT_FLAG_STATUS_SET) {
	render_leaf_int32(robj, "status", res->status);
    }

    aobj = json_object_new_array();
    if (aobj) {
	json_object_object_add(robj, "table", aobj);
	for (tab = res->tables; tab; tab = tab->next) {
	    render_table(tab, aobj);
	}
    }
}

/**
 * @brief Returns a JSON rendering of the lmap report
 *
 * This function renders the current lmap report into an JSON document
 * according to the IETF's LMAP YANG data model.
 *
 * @param lmap The pointer to the lmap report to be rendered.
 * @return An JSON document as a string that must be freed by the
 *         caller or NULL on error
 */

char *
lmap_json_render_report(struct lmap *lmap)
{
    const char *report = NULL;
    json_object *jobj, *aobj, *robj;
    struct result *res;

    assert(lmap);

    jobj = json_object_new_object();
    robj = json_object_new_object();
    json_object_object_add(jobj, LMAPR_JSON_NAMESPACE ":" "report", robj);
    render_agent_report(lmap->agent, robj);
    aobj = json_object_new_array();
    json_object_object_add(robj, "result", aobj);
    for (res = lmap->results; res; res = res->next) {
	render_result(res, aobj);
    }
    report = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_PRETTY);
    return report ? strdup(report) : NULL;
}
