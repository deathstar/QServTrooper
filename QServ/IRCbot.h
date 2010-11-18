/**

    header for the IRC bot used in the QServ sauerbraten server mod

**/

#ifndef __IRCBOT_INCLUDED
#define __IRCBOT_INCLUDED

struct IrcMsg
{
    char nick[32];
    char user[32];
    char host[64];
    char chan[32];
    char message[512];
    int is_ready;
};

struct IrcUser
{
    char *ircname, *password;
    bool isLoggedIn;
    IrcUser() : ircname(""), password(""), isLoggedIn(0) {}
};

int ircsay(const char *fmt, ...);

#endif ///__IRCBOT_INCLUDED
