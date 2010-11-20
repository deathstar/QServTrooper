#include "game.h"
#ifndef WIN32
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#endif
#include "IRCbot.h"

SVAR(irchost, "irc.gamesurge.net");
VAR(ircport, 0, 6667, 65535);
SVAR(ircchan, "#c2");
SVAR(ircbotname, "QServ");

SVAR(ircloginpass, "changeme");

ircBot irc;

bool isloggedin()
{
    IrcMsg *msg = &irc.lastmsg();

    if(!irc.IRCusers.access(msg->host)){
        irc.notice(msg->nick, "Insufficient Priveleges");
        return false;
    }
    return true;
}

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

bool IsCommand(IrcMsg *msg)
{
    if(msg->message[0] == '#' || msg->message[0] == '@')
    {
        char *c = msg->message;
        c++;
        execute(c);
        return true;
    }return false;
}

int ircBot::getSock()
{
    return sock;
}

int ircBot::speak(const char *fmt, ...){
    char Amsg[1000], k[1000];
    va_list list;
    va_start(list,fmt);
    vsnprintf(k,1000,fmt,list);
    snprintf(Amsg,1000,"PRIVMSG %s :%s\r\n\0",ircchan,k);
    va_end(list);

    return send(sock,Amsg,strlen(Amsg),0);
}

IrcMsg ircBot::lastmsg(){
    return msg;
}
void ircBot::join(char *channel){
    defformatstring(joinchan)("JOIN %s\r\n", channel);
    send(sock,joinchan,strlen(joinchan),0);
}

void ircBot::part(char *channel){
    defformatstring(partchan)("PART %s\r\n", channel);
    send(sock,partchan,strlen(partchan),0);
}

void ircBot::notice(char *user, char *message){
    defformatstring(noticeuser)("NOTICE %s :%s\r\n", user, message);
    send(sock,noticeuser,strlen(noticeuser),0);
}


void ircBot::ParseMessage(char *buff){
    if(sscanf(buff,":%[^!]!%[^@]@%[^ ] %*[^ ] %[^ :] :%[^\r\n]",msg.nick,msg.user,msg.host,msg.chan,msg.message) == 5){
        msg.is_ready = 1;
        if(msg.chan[0] != '#') strcpy(msg.chan,msg.nick);
    } else msg.is_ready = 0;
}

void ircBot::checkping(char *buff)
{
    printf("%s\n", buff);
    char Pingout[60];
    memset(Pingout,'\0',60);
    if(sscanf(buff,"PING :%s",buff)==1)
    {
        snprintf(Pingout,60,"PONG :%s\r\n",buff);
        send(sock,Pingout,strlen(Pingout),0);
        printf("SENT: %s\n", Pingout);
    }
    memset(Pingout,'\0',60);
}

void ircBot::init()
{
    int con;
    struct sockaddr_in sa;
    struct hostent *he;
    char mybuffer[1000];
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    memset(&sa, 0, sizeof sa);
    memset(&he, 0, sizeof he);
    sa.sin_family = AF_INET;
    he = gethostbyname(irchost);
    bcopy(*he->h_addr_list, (char *)&sa.sin_addr.s_addr, sizeof(sa.sin_addr.s_addr));
    sa.sin_port = htons(ircport);
    connected = false;
    con = connect(sock, (struct sockaddr *)&sa, sizeof(sa));

    defformatstring(user)("USER %s 0 * :%s\r\n", ircbotname, ircbotname);
    send(sock, user, strlen(user), 0);
    defformatstring(nick)("NICK %s\r\n", ircbotname);
    send(sock, nick, strlen(nick), 0);
    defformatstring(join)("JOIN %s\r\n", ircchan);

    int n;
    while(1){
        n = recv(sock, mybuffer, sizeof(mybuffer), 0);
        checkping(mybuffer);
        if(!connected)
        {
            send(sock, join, strlen(join), 0);
            connected = true;
        }
        ParseMessage(mybuffer);

        if(!IsCommand(&msg)){
            defformatstring(toserver)("\f4%s \f3%s \f7- \f0%s\f7: %s", newstring(irchost), newstring(ircchan), msg.nick, msg.message);
            server::sendservmsg(toserver);
        }
        memset(mybuffer,'\0',1000);
    }
}

void out(int type, char *fmt, ...)
{
    char msg[1000];
    va_list list;
    va_start(list,fmt);
    vsnprintf(msg,1000,fmt,list);
    va_end(list);

    switch(type)
    {
        case ECHO_ALL:
        {
            server::sendservmsg(msg);
            irc.speak(msg);
            puts(msg);
            break;
        }
        case ECHO_IRC:
        {
            irc.speak(msg);
            break;
        }
        case ECHO_CONSOLE:
        {
            puts(msg);
            break;
        }
        case ECHO_SERV:
        {
            server::sendservmsg(msg);
            break;
        }
        case ECHO_MASTER:
        {
            int ci = server::getmastercn();
            if(ci >= 0)
                sendf(ci, 1, "ris", N_SERVMSG, msg);
            break;
        }
        default:
            break;
    }
}
