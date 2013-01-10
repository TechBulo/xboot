/*
 * framework/event/m_event.c
 *
 * Copyright (c) 2007-2012  jianjun jiang <jerryjianjun@gmail.com>
 * official site: http://xboot.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <framework/framework.h>

static int l_event_new(lua_State * L)
{
	struct event_t event;
	struct event_t * e;

	event.type = (enum event_type_t)luaL_optint(L, 1, 0);

	switch(event.type)
	{
	case EVENT_TYPE_UNKNOWN:
		break;

	case EVENT_TYPE_MOUSE_RAW:
		event.e.mouse_raw.btndown = (u8_t)luaL_optint(L, 2, 0);
		event.e.mouse_raw.btnup = (u8_t)luaL_optint(L, 3, 0);
		event.e.mouse_raw.xrel = (s32_t)luaL_optint(L, 4, 0);
		event.e.mouse_raw.yrel = (s32_t)luaL_optint(L, 5, 0);
		event.e.mouse_raw.zrel = (s32_t)luaL_optint(L, 6, 0);
		break;

	default:
		break;
	}

	e = (struct event_t *)lua_newuserdata(L, sizeof(struct event_t));
	memcpy(e, &event, sizeof(struct event_t));
	luaL_setmetatable(L, LUA_TYPE_EVENT);

	return 1;
}

static int l_event_send(lua_State * L)
{
	struct event_t * e = (struct event_t *)luaL_checkudata(L, 1, LUA_TYPE_EVENT);

	event_send(e);
	return 0;
}

static const luaL_Reg l_event[] = {
	{"new",			l_event_new},
	{"send",		l_event_send},
	{NULL, NULL}
};

#if 0
static int m_event_index(lua_State * L)
{
	struct event_t * e = (struct event_t *)luaL_checkudata(L, 1, LUA_TYPE_EVENT);
	const char * k = luaL_checkstring(L, 2);

	if(strcmp(k, "type") == 0)
	{
		lua_pushnumber(L, e->type);
		return 1;
	}
	else if(strcmp(k, "timestamp") == 0)
	{
		lua_pushnumber(L, e->timestamp);
		return 1;
	}
	else if(strcmp(k, "event") == 0)
	{
		switch(e->type)
		{
		case EVENT_TYPE_UNKNOWN:
			return 0;

		case EVENT_TYPE_MOUSE_RAW:
			lua_pushnumber(L, e->e.mouse_raw.btndown);
			lua_pushnumber(L, e->e.mouse_raw.btnup);
			lua_pushnumber(L, e->e.mouse_raw.xrel);
			lua_pushnumber(L, e->e.mouse_raw.yrel);
			lua_pushnumber(L, e->e.mouse_raw.zrel);
			return 5;

		default:
			return 0;
		}
	}

	return 0;
}

static int m_event_newindex(lua_State * L)
{
	struct event_t * e = (struct event_t *)luaL_checkudata(L, 1, LUA_TYPE_EVENT);
	const char * k = luaL_checkstring(L, 2);

	if(strcmp(k, "type") == 0)
	{
		e->type = (enum event_type_t)luaL_optint(L, 3, 0);
	}
	else if(strcmp(k, "timestamp") == 0)
	{
		e->timestamp = luaL_optint(L, 3, jiffies);
	}
	else if(strcmp(k, "event") == 0)
	{

	}

	return 0;
}
#endif

static int m_event_gc(lua_State * L)
{
	return 0;
}

static int m_event_tostring(lua_State * L)
{
	struct event_t * e = lua_touserdata(L, 1);

	if(!e)
		return 0;

	lua_pushfstring(L, "event [%d, %d]", e->type, e->timestamp);
	return 1;
}

static const luaL_Reg m_event[] = {
/*	{"__index",		m_event_index},
	{"__newindex",	m_event_newindex},*/
	{"__gc",		m_event_gc},
	{"__tostring",	m_event_tostring},
	{NULL, NULL}
};

int luaopen_event(lua_State * L)
{
	luaL_newlib(L, l_event);

	luaL_newmetatable(L, LUA_TYPE_EVENT);	/* create new metatable */
	lua_pushvalue(L, -1);					/* push metatable */
	lua_setfield(L, -2, "__index");			/* metatable.__index = metatable */
	luaL_setfuncs(L, m_event, 0);			/* add methods to new metatable */
	lua_pop(L, 1);

	return 1;
}
