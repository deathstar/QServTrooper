#include "game.h"
#include "IRCbot.h"

SVAR(ircloginpass, "changeme");

ICOMMAND(login, "s", (char *s), {
        IrcMsg *msg = &irc.lastmsg();
        if(!strcmp(s, ircloginpass)){
            irc.IRCusers[msg->host] = 1;
            irc.speak("%s has logged in", msg->nick);
        }
        else irc.notice(msg->nick, "Invalid Password");
});

ICOMMAND(clearbans, "", (), {
    if(isloggedin())
        server::clearbans();
});

ICOMMAND(join, "s", (char *s), {
    if(isloggedin())
        irc.join(s);
});

ICOMMAND(part, "s", (char *s), {
    if(isloggedin())
        irc.part(s);
});

ICOMMAND(kick, "i", (int *i), {
    if(isloggedin())
        disconnect_client(*i, DISC_KICK);
});

ICOMMAND(kick, "i", (int *i), {
    if(isloggedin())
        server::banPlayer(*i);
});
