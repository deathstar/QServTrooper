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

class ircBot
{
    public:
        void init();
        int getSock();
        int speak(const char *fmt, ...);

    private:
        void ParseMessage(char *buff);
        int sock;
        IrcMsg msg;
};

extern ircBot irc;

enum
{
    ECHO_ALL = 0,
    ECHO_IRC ,
    ECHO_CONSOLE,
    ECHO_SERV,
    ECHO_MASTER,
};

extern void echo(int type, char *fmt, ...);

#endif ///__IRCBOT_INCLUDED
