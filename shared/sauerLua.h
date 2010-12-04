/**

    header for the LUA module for the QServ sauerbraten server mod

**/

#ifndef __LUA_INCLUDED
#define __LUA_INCLUDED

extern "C"
{
    #include "lua.h"
    #include "lualib.h"
    #include "lauxlib.h"
}

#include "game.h"

class luaVM
{
    public:
        void qservLuaInit();
        lua_State *getState();
    private:
        lua_State *L;
};

static struct luaEvent
{
    const char *name;
    int eventnum;
} luaEvents[] =
{
    {"servermsg", N_TEXT}
};

extern luaVM luavm;

#endif ///__LUA_INCLUDED
