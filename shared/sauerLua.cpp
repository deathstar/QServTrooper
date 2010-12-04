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

static const luaL_Reg qservlib[] = {
    {"toserver", lua_toserver},
    {NULL, NULL}
};

void luaVM::qservLuaInit()
{
    L=lua_open();
    luaL_openlibs(L);
    luaL_register(L, "qserv", qservlib);
    printf("\33[33mLua Successfully initialized\33[0m\n");
}
