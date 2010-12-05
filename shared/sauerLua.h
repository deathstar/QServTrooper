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
#include "tools.h"
class luaVM
{
    public:
        void qservLuaInit();
        lua_State *getState();
        hashtable<int, vector<char *> > eventFunc;
    private:
        lua_State *L;
};

extern int initClientLib(lua_State *L);
extern void luaCallback(int event, int sender, char *text = "");
extern luaVM luavm;

#endif ///__LUA_INCLUDED
