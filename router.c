/*
 * module-agl-audio -- PulseAudio module for providing audio routing support
 * (forked from "module-murphy-ivi" - https://github.com/otcshare )
 * Copyright (c) 2012, Intel Corporation.
 * Copyright (c) 2016, IoT.bzh
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St - Fifth Floor, Boston,
 * MA 02110-1301 USA.
 *
 */
#include "router.h"
#include "switch.h"
#include "zone.h"
#include "utils.h"

agl_router *agl_router_init (struct userdata *u)
{
	agl_router *router;
	size_t num_classes;

	num_classes = agl_application_class_end;

	router = pa_xnew0 (agl_router, 1);
	router->rtgroups.input = pa_hashmap_new (pa_idxset_string_hash_func,
		                                 pa_idxset_string_compare_func);
	router->rtgroups.output = pa_hashmap_new (pa_idxset_string_hash_func,
		                                  pa_idxset_string_compare_func);
	router->maplen = num_classes;
        router->priormap = pa_xnew0 (int, num_classes);

	AGL_DLIST_INIT (router->nodlist);
	AGL_DLIST_INIT (router->connlist);

	return router;
}

void agl_router_done (struct userdata *u)
{
	agl_router *router;
	agl_node *e,*n;
	agl_connection *conn, *c;
	agl_rtgroup *rtg;
	agl_rtgroup **map;
	int i;

	if (u && (router = u->router)) {
		AGL_DLIST_FOR_EACH_SAFE(agl_node, rtprilist, e,n, &router->nodlist)
			AGL_DLIST_UNLINK(agl_node, rtprilist, e);
		AGL_DLIST_FOR_EACH_SAFE(agl_connection, link, conn,c, &router->connlist) {
			AGL_DLIST_UNLINK(agl_connection, link, conn);
			pa_xfree (conn);
		}
		/*
		PA_HASHMAP_FOREACH(rtg, router->rtgroups.input, state) {
			rtgroup_destroy(u, rtg);
		}
		PA_HASHMAP_FOREACH(rtg, router->rtgroups.output, state) {
			rtgroup_destroy(u, rtg);
		}*/
		pa_hashmap_free (router->rtgroups.input);
		pa_hashmap_free (router->rtgroups.output);

		for (i = 0;  i < AGL_ZONE_MAX;  i++) {
			if ((map = router->classmap.input[i]))
				pa_xfree(map);
			if ((map = router->classmap.output[i]))
				pa_xfree(map);
		}

		pa_xfree (router->priormap);
		pa_xfree (router);

		u->router = NULL;
	}
}

bool agl_router_default_accept (struct userdata *u, agl_rtgroup *rtg, agl_node *node)
{
	/* TODO */
	return true;
}

bool agl_router_phone_accept (struct userdata *u, agl_rtgroup *rtg, agl_node *node)
{
	/* TODO */
	return true;
}

int agl_router_default_effect (struct userdata *u, agl_rtgroup *rtg, agl_node *node, bool new)
{
	/* TODO */
	return 1;
}

int agl_router_phone_effect (struct userdata *u, agl_rtgroup *rtg, agl_node *node, bool new)
{
	pa_assert (u);
	pa_assert (node);

	if (new)
		agl_utils_volume_ramp (u, node->nullsink, false);
	else
		agl_utils_volume_ramp (u, node->nullsink, true);

	return 1;
}

agl_rtgroup *agl_router_create_rtgroup (struct userdata *u, agl_direction type, const char *name, const char *node_desc, agl_rtgroup_accept_t accept, agl_rtgroup_effect_t effect)
{
	agl_router *router;
	agl_rtgroup *rtg;
	agl_nodeset *nodeset;
	agl_node *node;
	pa_hashmap *table;

	pa_assert (u);
	pa_assert (type == agl_input || type == agl_output);
	pa_assert (name);
	pa_assert_se (router = u->router);
	pa_assert_se (nodeset = u->nodeset);

	if (type == agl_input)
		table = router->rtgroups.input;
	else
		table = router->rtgroups.output;
	pa_assert (table);

	rtg = pa_xnew0 (agl_rtgroup, 1);
	rtg->name = pa_xstrdup (name);
	rtg->accept = accept;
	rtg->effect = effect;
	AGL_DLIST_INIT(rtg->entries);
	/* associate an agl_output node for an agl_input routing group */
	if (type == agl_input) {
		node = agl_node_create (u, NULL);
		node->direction = agl_output;
		node->implement = agl_device;
		node->visible = true;
		node->available = true;
		node->paname = pa_xstrdup (node_desc);
		 /* add to global nodeset */
		pa_idxset_put (nodeset->nodes, node, &node->index);
		rtg->node = node;
	} else {
		rtg->node = NULL;
	}
	

	pa_hashmap_put (table, rtg->name, rtg);

	pa_log_debug ("routing group '%s' created", name);

	return rtg;
}

void agl_router_destroy_rtgroup (struct userdata *u, agl_direction type, const char *name)
{
	agl_router *router;
	agl_rtgroup *rtg;
	pa_hashmap *table;

	pa_assert (u);
	pa_assert (name);
	pa_assert_se (router = u->router);

	if (type == agl_input)
		table = router->rtgroups.input;
	else
		table = router->rtgroups.output;
	pa_assert (table);

	rtg = pa_hashmap_remove (table, name);
	if (!rtg) {
		pa_log_debug ("can't destroy routing group '%s': not found", name);
	} else {
		//rtgroup_destroy (u, rtg);
		pa_log_debug ("routing group '%s' destroyed", name);
	}
}

bool agl_router_assign_class_to_rtgroup (struct userdata *u, agl_node_type class, uint32_t zone, agl_direction type, const char *name)
{
	agl_router *router;
	pa_hashmap *rtable;
	agl_rtgroup ***classmap;
	agl_rtgroup **zonemap;
	const char *classname;
	const char *direction;
	agl_rtgroup *rtg;
	agl_zone *rzone;

	pa_assert (u);
	pa_assert (zone < AGL_ZONE_MAX);
	pa_assert (type == agl_input || type == agl_output);
	pa_assert (name);
	pa_assert_se (router = u->router);

	if (type == agl_input) {
		rtable = router->rtgroups.input;
		classmap = router->classmap.input;
	} else {
		rtable = router->rtgroups.output;
		classmap = router->classmap.output;
	}

	if (class < 0 || class >= router->maplen) {
		pa_log_debug ("Cannot assign class to routing group '%s': "
			      "id %d out of range (0 - %d)",
			      name, class, router->maplen);
		return false;
	}

	classname = agl_node_type_str (class); /* "Player", "Radio"... */
	direction = agl_node_direction_str (type); /* "input", "output" */

	rtg = pa_hashmap_get (rtable, name);
	if (!rtg) {
		pa_log_debug ("Cannot assign class to routing group '%s': "
			      "router group not found", name);
		return false;
	}

	zonemap = classmap[zone];
	if (!zonemap) {
		zonemap = pa_xnew0 (agl_rtgroup *, router->maplen);
		classmap[zone] = zonemap;
	}

	zonemap[class] = rtg;

	 /* try to get zone name for logging, if fails, only print id number */
	rzone = agl_zoneset_get_zone_by_index (u, zone);
	if (rzone) {
		pa_log_debug ("class '%s'@'%s' assigned to routing group '%s'",
			      classname, rzone->name, name); 
	} else {
		pa_log_debug ("class '%s'@zone%d assigned to routing group '%s'",
			      classname, zone, name);
	}

	return true;
}

agl_rtgroup * agl_router_get_rtgroup_from_class (struct userdata *u, agl_node_type class, uint32_t zone, agl_direction type)
{
	agl_router *router;
	pa_hashmap *rtable;
	agl_rtgroup ***classmap;
	agl_rtgroup **zonemap;
	agl_rtgroup * rtg;

	pa_assert (u);
	pa_assert_se (router = u->router);
	pa_assert (class >= 0 && class < router->maplen);
	pa_assert (zone < AGL_ZONE_MAX);
	pa_assert (type == agl_input || type == agl_output);

	if (type == agl_input) {
		rtable = router->rtgroups.input;
		classmap = router->classmap.input;
	} else {
		rtable = router->rtgroups.output;
		classmap = router->classmap.output;
	}

	zonemap = classmap[zone];
	rtg = zonemap[class];

	return rtg;
}

void agl_router_assign_class_priority (struct userdata *u, agl_node_type class, int priority)
{
	agl_router *router;
	int *priormap;

	pa_assert (u);
	pa_assert_se (router = u->router);
	pa_assert_se (priormap = router->priormap);

	if (class > 0 && class < router->maplen) {
		pa_log_debug ("assigning priority %d to class '%s'",
			      priority, agl_node_type_str (class));
		priormap[class] = priority;
	}
}

int agl_router_get_node_priority (struct userdata *u, agl_node *node)
{
	agl_router *router;
	int class;

	pa_assert (u);
	pa_assert (node);
	pa_assert_se (router = u->router);

	class = node->type;

	if (class < 0 || class >= (int)router->maplen)
		return 0;

	return router->priormap[class];
}

bool agl_router_apply_node_priority_effect (struct userdata *u, agl_node *node, bool new)
{
	agl_router *router;
	agl_rtgroup *rtg;
	agl_nodeset *nodeset;
	agl_node *n;
	pa_sink *sink;
	int priority;
	uint32_t index;

	pa_assert (u);
	pa_assert (node);
	pa_assert_se (router = u->router);
	pa_assert_se (nodeset = u->nodeset);

	rtg = agl_router_get_rtgroup_from_class(u, node->type, 0, node->direction);

	/* now let us compare priorities, and apply effect if needed */
	/* "new" case */
	if (new) {
		priority = agl_router_get_node_priority (u, node);
		PA_IDXSET_FOREACH(n, nodeset->nodes, index) {
			if (n->nullsink && (priority > agl_router_get_node_priority (u, n))) {
				sink = agl_utils_get_null_sink (u, n->nullsink);
				if (sink) {
					/* do we have a custom effect ? otherwise, just mute it */
					if (rtg && rtg->effect)
						rtg->effect (u, rtg, n, new);
					else
						pa_sink_set_mute (sink, new, false);
				}
			}
		}
	} else {
	/* "old" case */
		if (!agl_node_has_highest_priority (u, node))
			return true;
		PA_IDXSET_FOREACH(n, nodeset->nodes, index) {
			if (n->nullsink) {
				sink = agl_utils_get_null_sink (u, n->nullsink);
				if (sink) {
					/* do we have a custom effect ? otherwise, just unmute it */
					if (rtg && rtg->effect)
						rtg->effect (u, rtg, n, new);
					else
						pa_sink_set_mute (sink, new, false);
				}
			}
		}
	}

	return true;
}

void agl_router_register_node (struct userdata *u, agl_node *node)
{
	agl_router *router;
	agl_rtgroup *rtg;

	pa_assert (u);
	pa_assert (node);
	pa_assert_se (router = u->router);

	/* we try to discover node routing group from the configuration, "Phone" for instance,
	 * see defaults in "config.c. Otherwise we just say NULL, a.k.a. default */
	rtg = agl_router_get_rtgroup_from_class(u, node->type, 0, node->direction);

	if (node->direction == agl_input) {
		if (rtg)
			implement_default_route (u, node, rtg->node, agl_utils_new_stamp ());
		else
			implement_default_route (u, node, NULL, agl_utils_new_stamp ());
	} else {
		if (rtg)
			implement_default_route (u, rtg->node, node, agl_utils_new_stamp ());
		else
			implement_default_route (u, NULL, node, agl_utils_new_stamp ());
	}
}

void agl_router_unregister_node (struct userdata *u, agl_node *node)
{
	pa_assert (u);
	pa_assert (node);

	remove_routes (u, node, NULL, agl_utils_new_stamp ());
}

agl_node *agl_router_make_prerouting (struct userdata *u, agl_node *data)
{
	agl_router *router;
	int priority;
	static bool done_prerouting;
	uint32_t stamp;
	agl_node *start, *end;
	agl_node *target;

	pa_assert (u);
	pa_assert_se (router = u->router);
	pa_assert_se (data->implement == agl_stream);

	//priority = node_priority (u, data);

	done_prerouting = false;
	target = NULL;
	stamp = agl_utils_new_stamp ();

	//make_explicit_routes (u, stamp);

	//pa_audiomgr_delete_default_routes(u);

	AGL_DLIST_FOR_EACH_BACKWARDS(agl_node, rtprilist, start, &router->nodlist) {
		//if ((start->implement == agl_device) &&
		//    (!start->loop))	/* only manage looped real devices */
		//	continue;

		/*if (priority >= node_priority (u, start)) {
			target = find_default_route (u, data, stamp);
			if (target)
				implement_preroute (u, data, target, stamp);
			else
				done_prerouting = true;
		}*/

		if (start->stamp >= stamp)
			continue;

		//end = find_default_route (u, start, stamp);
		//if (end)
		//	implement_default_route(u, start, end, stamp);
	}

	if (!done_prerouting) {
		pa_log_debug ("Prerouting failed, trying to find default route as last resort");

		//target = find_default_route (u, data, stamp);
		//if (target)
		//	implement_preroute (u, data, target, stamp);
	}

	return target;
}

void agl_router_make_routing (struct userdata *u)
{
	agl_router *router;
	static bool ongoing_routing;	/* true while we are actively routing */
	uint32_t stamp;
	agl_node *start, *end;

	pa_assert (u);
	pa_assert_se (router = u->router);

	if (ongoing_routing)		/* already routing, canceling */
		return;
	ongoing_routing = true;
	stamp = agl_utils_new_stamp ();

	pa_log_debug("stamp for routing: %d", stamp);

	// make_explicit_routes (u, stamp);

	// pa_audiomgr_delete_default_routes (u);

	AGL_DLIST_FOR_EACH_BACKWARDS(agl_node, rtprilist, start, &router->nodlist) {
		//if ((start->implement == agl_device) &&
		//   (!start->loop))	/* only manage looped real devices */
		//	continue;

		if (start->stamp >= stamp)
			continue;

		end = find_default_route (u, start, stamp);
		if (end)
			implement_default_route (u, start, end, stamp);
	}

	// pa_audiomgr_send_default_routes (u);

	ongoing_routing = false;
}

void implement_default_route (struct userdata *u,
                              agl_node *start, agl_node *end,
                              uint32_t stamp)
{
	if (start->direction == agl_input)
		agl_switch_setup_link (u, start, end);
	else
		agl_switch_setup_link (u, end, start);
}

agl_node *find_default_route (struct userdata *u, agl_node *start, uint32_t stamp)
{
	/* TODO */

	return NULL;
}

void remove_routes (struct userdata *u, agl_node *start, agl_node *end, uint32_t stamp)
{
	if (start->direction == agl_input) {
		agl_switch_teardown_link (u, start, end);
	} else {
		agl_switch_teardown_link (u, end, start);
	}
}
