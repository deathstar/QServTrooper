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

enum
{
    LUAEVENT_TEXT = 0,
    LUAEVENT_CONNECTED,
    LUAEVENT_INTERMISSION,
    LUAEVENT_SUICIDE,
    LUAEVENT_SHOOT,
    LUAEVENT_SAYTEAM,
    LUAEVENT_MAPCHANGE,
    LUAEVENT_KICK,
    LUAEVENT_GETMAP,
    LUAEVENT_NEWMAP,
    LUAEVENT_ADDBOT,
    LUAEVENT_DELBOT,
    LUAEVENT_PAUSEGAME
};

extern int initClientLib(lua_State *L);
extern void luaCallback(int event, int sender, char *text = "");
extern luaVM luavm;

#endif ///__LUA_INCLUDED
