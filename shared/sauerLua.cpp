#include "sauerLua.h"

lua_State *luaVM::getState()
{
    return L;
}

static int lua_toserver(lua_State *Lua)
{
    char *msg = (char *)luaL_checkstring(Lua, 1);
    server::sendservmsg(msg);
    return 0;
}

static int lua_intermission(lua_State *Lua)
{
    server::startintermission();
    return 0;
}

static int lua_getnumclients(lua_State *Lua)
{
    lua_pushinteger(Lua, getnumclients());
    return 1;
}

static int lua_getclientname(lua_State *Lua)
{
    lua_pushstring(Lua, server::getclientname(luaL_checkint(Lua, 1)));
    return 1;
}

static const luaL_Reg qservlib[] = {
    {"toserver", lua_toserver},
    {"clientcount", lua_getnumclients},
    {"getclientname", lua_getclientname},
    {"intermission", lua_intermission},
    {NULL, NULL}
};

void luaVM::qservLuaInit()
{
    L=lua_open();
    luaL_openlibs(L);
    luaL_register(L, "qserv", qservlib);
    printf("\33[33mLua Successfully initialized\33[0m\n");
}
