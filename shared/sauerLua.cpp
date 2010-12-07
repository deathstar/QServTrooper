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

/*void luaCallback(int event, int sender, char *text)
{
    loopv(luavm.eventFunc[event])
    {
        lua_getglobal(luavm.getState(), luavm.eventFunc[event][i]);
        if(!lua_isfunction(luavm.getState(),-1))
        {
            lua_pop(luavm.getState(),1);
            return;
        }
        lua_pushnumber(luavm.getState(), sender);
        lua_pushstring(luavm.getState(), text);
        lua_pcall(luavm.getState(), 2, 0, 0);
    }
}*/

void luaCallback(int event, int cn, int numargs, const char *format, ...)
{
    vector<int> nums;
    vector<const char *> strings;

    va_list args;
    va_start(args, format);
    while(*format) switch(*format++)
    {
        case 'i':
        {
            int n = isdigit(*format) ? *format++-'0' : 1;
            loopi(n) nums.add(va_arg(args, int));
            break;
        }
        case 's':
           strings.add(va_arg(args, const char *));
            break;
    }
    va_end(args);

    loopv(luavm.eventFunc[event])
    {
        lua_getglobal(luavm.getState(), luavm.eventFunc[event][i]);
        if(!lua_isfunction(luavm.getState(),-1))
        {
            lua_pop(luavm.getState(),1);
            return;
        }
        lua_pushnumber(luavm.getState(), cn);
        loopv(strings) lua_pushstring(luavm.getState(), strings[i]);
        loopv(nums) lua_pushnumber(luavm.getState(), nums[i]);
        lua_pcall(luavm.getState(), numargs+1, 0, 0);
    }
}

static int lua_addcallback(lua_State *Lua)
{
    int arg1 = luaL_checkint(Lua, 1);
    char *arg2 = (char *)luaL_checkstring(Lua, 2);
    printf("adding callback %i : %s\n", arg1, arg2);
    luavm.eventFunc[arg1].add(arg2);
    return 0;
}


static const luaL_Reg qservlib[] =
{
    {"toserver", lua_toserver},
    {"clientcount", lua_getnumclients},
    {"getclientname", lua_getclientname},
    {"intermission", lua_intermission},
    {"AddCallback", lua_addcallback},
    {NULL, NULL}
};

void luaVM::qservLuaInit()
{
    L=lua_open();
    luaL_openlibs(L);
    luaL_register(L, "qserv", qservlib);
    initClientLib(L);

    lua_pushnumber(L, LUAEVENT_TEXT);
    lua_setfield(L, -2, "LUAEVENT_TEXT");
    lua_pushnumber(L, LUAEVENT_CONNECTED);
    lua_setfield(L, -2, "LUAEVENT_CONNECTED");
    lua_pushnumber(L, LUAEVENT_INTERMISSION);
    lua_setfield(L, -2, "LUAEVENT_INTERMISSION");
    lua_pushnumber(L, LUAEVENT_SUICIDE);
    lua_setfield(L, -2, "LUAEVENT_SUICIDE");
    lua_pushnumber(L, LUAEVENT_SHOOT);
    lua_setfield(L, -2, "LUAEVENT_SHOOT");
    lua_pushnumber(L, LUAEVENT_SAYTEAM);
    lua_setfield(L, -2, "LUAEVENT_SAYTEAM");
    lua_pushnumber(L, LUAEVENT_MAPCHANGE);
    lua_setfield(L, -2, "LUAEVENT_MAPCHANGE");
    lua_pushnumber(L, LUAEVENT_GETMAP);
    lua_setfield(L, -2, "LUAEVENT_GETMAP");
    lua_pushnumber(L, LUAEVENT_KICK);
    lua_setfield(L, -2, "LUAEVENT_KICK");
    lua_pushnumber(L, LUAEVENT_NEWMAP);
    lua_setfield(L, -2, "LUAEVENT_NEWMAP");
    lua_pushnumber(L, LUAEVENT_ADDBOT);
    lua_setfield(L, -2, "LUAEVENT_ADDBOT");
    lua_pushnumber(L, LUAEVENT_DELBOT);
    lua_setfield(L, -2, "LUAEVENT_DELBOT");
    lua_pushnumber(L, LUAEVENT_PAUSEGAME);
    lua_setfield(L, -2, "LUAEVENT_PAUSEGAME");

    luaL_dofile(L, "scripts/luaInit.lua");

    printf("\33[33mLua Successfully initialized\33[0m\n");
}
