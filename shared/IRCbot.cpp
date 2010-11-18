#include "game.h"
#include "IRCbot.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <iostream>
#include <string>
#include <map>

SVAR(irchost, "78.40.125.4");
VAR(ircport, 0, 6667, 65535);
SVAR(ircchan, "#none");
SVAR(ircbotname, "QServ");

ircBot irc;

int ircBot::speak(const char *fmt, ...){
    char msg[1000], k[1000];
    va_list list;
    va_start(list,fmt);
    vsnprintf(k,1000,fmt,list);
    snprintf(msg,1000,"PRIVMSG %s :%s\r\n\0",ircchan,k);
    va_end(list);

    return send(sock,msg,strlen(msg),0);
}

void ircBot::ParseMessage(char *buff){
    if(sscanf(buff,":%[^!]!%[^@]@%[^ ] %*[^ ] %[^ :] :%[^\r\n]",msg.nick,msg.user,msg.host,msg.chan,msg.message) == 5){
        msg.is_ready = 1;
        if(msg.chan[0] != '#') strcpy(msg.chan,msg.nick);
    } else msg.is_ready = 0;
}

void ircBot::init()
{
    int con;
    struct sockaddr_in sa;

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = inet_addr(irchost);
    sa.sin_port = htons(ircport);

    con = connect(sock, (struct sockaddr *)&sa, sizeof(sa));
    defformatstring(user)("USER %s 0 * :%s\r\n", ircbotname, ircbotname);
    send(sock, user, strlen(user), 0);
    defformatstring(nick)("NICK %s\r\n\0", ircbotname);
    send(sock, nick, strlen(nick), 0);
    defformatstring(join)("JOIN %s\r\n\0", ircchan);
    send(sock, join, strlen(join), 0);
    int n;
    char mybuffer[1000];
    char out[30];
    while(1){
        n = recv(sock, mybuffer, sizeof(mybuffer), 0);
        puts(mybuffer);

        ParseMessage(mybuffer);
		
        if(sscanf(mybuffer,"PING: %s",mybuffer)==1){
            snprintf(out,30,"PONG: %s",out);
            send(sock,out,strlen(out),0);
        }
		if(sscanf(mybuffer,"#")==0)
		{
        defformatstring(toserver)("\f4%s \f3%s \f7- \f0%s\f7: %s", newstring(irchost), newstring(ircchan), msg.nick, msg.message);
        server::sendservmsg(toserver);
		}

        memset(mybuffer,'\0',1000);
        memset(out,'\0',30);
    }
}

void echo(int type, const char *fmt, ...)
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
