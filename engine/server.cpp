//engine/server.cpp for QServ: little more than enhanced multicaster, runs dedicated or as client coroutine
  
#include "engine.h"
#include <pthread.h>
#include "IRCbot.h"
#include "sauerLua.h"

VAR(enableirc, 0, 0, 1);
extern ircBot irc;
luaVM luavm;

#ifdef STANDALONE
void fatal(const char *s, ...)
{
    void cleanupserver();
    cleanupserver();
    defvformatstring(msg,s,s);
    printf("\33[31mFatal Error: %s\33[0m\n", msg);
    exit(EXIT_FAILURE);
}

void conoutfv(int type, const char *fmt, va_list args)
{
    string sf, sp;
    vformatstring(sf, fmt, args);
    filtertext(sp, sf);
    puts(sp);
}

void conoutf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    conoutfv(CON_INFO, fmt, args);
    va_end(args);
}

void conoutf(int type, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    conoutfv(type, fmt, args);
    va_end(args);
}
#endif

// all network traffic is in 32bit ints, which are then compressed using the following simple scheme (assumes that most values are small).

template<class T>
static inline void putint_(T &p, int n)
{
    if(n<128 && n>-127) p.put(n);
    else if(n<0x8000 && n>=-0x8000) { p.put(0x80); p.put(n); p.put(n>>8); }
    else { p.put(0x81); p.put(n); p.put(n>>8); p.put(n>>16); p.put(n>>24); }
}
void putint(ucharbuf &p, int n) { putint_(p, n); }
void putint(packetbuf &p, int n) { putint_(p, n); }
void putint(vector<uchar> &p, int n) { putint_(p, n); }

int getint(ucharbuf &p)
{
    int c = (char)p.get();
    if(c==-128) { int n = p.get(); n |= char(p.get())<<8; return n; }
    else if(c==-127) { int n = p.get(); n |= p.get()<<8; n |= p.get()<<16; return n|(p.get()<<24); }
    else return c;
}

// much smaller encoding for unsigned integers up to 28 bits, but can handle signed
template<class T>
static inline void putuint_(T &p, int n)
{
    if(n < 0 || n >= (1<<21))
    {
        p.put(0x80 | (n & 0x7F));
        p.put(0x80 | ((n >> 7) & 0x7F));
        p.put(0x80 | ((n >> 14) & 0x7F));
        p.put(n >> 21);
    }
    else if(n < (1<<7)) p.put(n);
    else if(n < (1<<14))
    {
        p.put(0x80 | (n & 0x7F));
        p.put(n >> 7);
    }
    else
    {
        p.put(0x80 | (n & 0x7F));
        p.put(0x80 | ((n >> 7) & 0x7F));
        p.put(n >> 14);
    }
}
void putuint(ucharbuf &p, int n) { putuint_(p, n); }
void putuint(packetbuf &p, int n) { putuint_(p, n); }
void putuint(vector<uchar> &p, int n) { putuint_(p, n); }

int getuint(ucharbuf &p)
{
    int n = p.get();
    if(n & 0x80)
    {
        n += (p.get() << 7) - 0x80;
        if(n & (1<<14)) n += (p.get() << 14) - (1<<14);
        if(n & (1<<21)) n += (p.get() << 21) - (1<<21);
        if(n & (1<<28)) n |= -1<<28;
    }
    return n;
}

template<class T>
static inline void putfloat_(T &p, float f)
{
    lilswap(&f, 1);
    p.put((uchar *)&f, sizeof(float));
}
void putfloat(ucharbuf &p, float f) { putfloat_(p, f); }
void putfloat(packetbuf &p, float f) { putfloat_(p, f); }
void putfloat(vector<uchar> &p, float f) { putfloat_(p, f); }

float getfloat(ucharbuf &p)
{
    float f;
    p.get((uchar *)&f, sizeof(float));
    return lilswap(f);
}

template<class T>
static inline void sendstring_(const char *t, T &p)
{
    while(*t) putint(p, *t++);
    putint(p, 0);
}
void sendstring(const char *t, ucharbuf &p) { sendstring_(t, p); }
void sendstring(const char *t, packetbuf &p) { sendstring_(t, p); }
void sendstring(const char *t, vector<uchar> &p) { sendstring_(t, p); }

void getstring(char *text, ucharbuf &p, int len)
{
    char *t = text;
    do
    {
        if(t>=&text[len]) { text[len-1] = 0; return; }
        if(!p.remaining()) { *t = 0; return; }
        *t = getint(p);
    }
    while(*t++);
}

void filtertext(char *dst, const char *src, bool whitespace, int len)
{
    for(int c = *src; c; c = *++src)
    {
        if(c == '\f')
        {
        if(!*++src) break;
        continue;
        }
        if(isspace(c) ? whitespace : isprint(c))
        {
            *dst++ = c;
            if(!--len) break;
        }
    }
    *dst = '\0';
}

enum { ST_EMPTY, ST_LOCAL, ST_TCPIP };

struct client                   // server side version of "dynent" type
{
    int type;
    int num;
    ENetPeer *peer;
    string hostname;
    void *info;
};

vector<client *> clients;

ENetHost *serverhost = NULL;
int laststatus = 0;
ENetSocket pongsock = ENET_SOCKET_NULL, lansock = ENET_SOCKET_NULL;

void cleanupserver()
{
    if(serverhost) enet_host_destroy(serverhost);
    serverhost = NULL;

    if(pongsock != ENET_SOCKET_NULL) enet_socket_destroy(pongsock);
    if(lansock != ENET_SOCKET_NULL) enet_socket_destroy(lansock);
    pongsock = lansock = ENET_SOCKET_NULL;
}

void process(ENetPacket *packet, int sender, int chan);

void *getclientinfo(int i) { return !clients.inrange(i) || clients[i]->type==ST_EMPTY ? NULL : clients[i]->info; }
ENetPeer *getclientpeer(int i) { return clients.inrange(i) && clients[i]->type==ST_TCPIP ? clients[i]->peer : NULL; }
int getnumclients()        { return clients.length(); }
uint getclientip(int n)    { return clients.inrange(n) && clients[n]->type==ST_TCPIP ? clients[n]->peer->address.host : 0; }

void sendpacket(int n, int chan, ENetPacket *packet, int exclude)
{
    if(n<0)
    {
        server::recordpacket(chan, packet->data, packet->dataLength);
        loopv(clients) if(i!=exclude && server::allowbroadcast(i)) sendpacket(i, chan, packet);
        return;
    }
    switch(clients[n]->type)
    {
        case ST_TCPIP:
        {
            enet_peer_send(clients[n]->peer, chan, packet);
            break;
        }

#ifndef STANDALONE
        case ST_LOCAL:
            localservertoclient(chan, packet);
            break;
#endif
    }
}

void sendf(int cn, int chan, const char *format, ...)
{
    int exclude = -1;
    bool reliable = false;
    if(*format=='r') { reliable = true; ++format; }
    packetbuf p(MAXTRANS, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    va_list args;
    va_start(args, format);
    while(*format) switch(*format++)
    {
        case 'x':
            exclude = va_arg(args, int);
            break;

        case 'v':
        {
            int n = va_arg(args, int);
            int *v = va_arg(args, int *);
            loopi(n) putint(p, v[i]);
            break;
        }

        case 'i':
        {
            int n = isdigit(*format) ? *format++-'0' : 1;
            loopi(n) putint(p, va_arg(args, int));
            break;
        }
        case 'f':
        {
            int n = isdigit(*format) ? *format++-'0' : 1;
            loopi(n) putfloat(p, (float)va_arg(args, double));
            break;
        }
        case 's': sendstring(va_arg(args, const char *), p); break;
        case 'm':
        {
            int n = va_arg(args, int);
            p.put(va_arg(args, uchar *), n);
            break;
        }
    }
    va_end(args);
    sendpacket(cn, chan, p.finalize(), exclude);
}

void sendfile(int cn, int chan, stream *file, const char *format, ...)
{
    if(cn < 0)
    {
#ifdef STANDALONE
        return;
#endif
    }
    else if(!clients.inrange(cn)) return;

    int len = file->size();
    if(len <= 0) return;

    packetbuf p(MAXTRANS+len, ENET_PACKET_FLAG_RELIABLE);
    va_list args;
    va_start(args, format);
    while(*format) switch(*format++)
    {
        case 'i':
        {
            int n = isdigit(*format) ? *format++-'0' : 1;
            loopi(n) putint(p, va_arg(args, int));
            break;
        }
        case 's': sendstring(va_arg(args, const char *), p); break;
        case 'l': putint(p, len); break;
    }
    va_end(args);

    file->seek(0, SEEK_SET);
    file->read(p.subbuf(len).buf, len);

    ENetPacket *packet = p.finalize();
    if(cn >= 0) sendpacket(cn, chan, packet, -1);
#ifndef STANDALONE
    else sendclientpacket(packet, chan);
#endif
}
const char *disc_reasons[] = { "normal", "end of packet error", "CN error", "temporarily banned", "tag type (client error)", "banned", "server is in private mode", "server full", "connection timed out", "kicked for spam", "banned"};
void disconnect_client(int n, int reason)
{
    if(clients[n]->type!=ST_TCPIP) return;
    enet_peer_disconnect(clients[n]->peer, reason);
    server::clientdisconnect(n);
    clients[n]->type = ST_EMPTY;
    clients[n]->peer->data = NULL;
    server::deleteclientinfo(clients[n]->info);
    clients[n]->info = NULL;
    defformatstring(s)("client (%s) disconnected: %s", clients[n]->hostname, disc_reasons[reason]);
    puts(s);
    server::sendservmsg(s);

}
void kicknonlocalclients(int reason)
{
    loopv(clients) if(clients[i]->type==ST_TCPIP) disconnect_client(i, reason);
}

void process(ENetPacket *packet, int sender, int chan)   // sender may be -1
{
    packetbuf p(packet);
    server::parsepacket(sender, chan, p);
    if(p.overread()) { disconnect_client(sender, DISC_EOP); return; }
}

void localclienttoserver(int chan, ENetPacket *packet)
{
    client *c = NULL;
    loopv(clients) if(clients[i]->type==ST_LOCAL) { c = clients[i]; break; }
    if(c) process(packet, c->num, chan);
}

client &addclient()
{
    loopv(clients) if(clients[i]->type==ST_EMPTY)
    {
        clients[i]->info = server::newclientinfo();
        return *clients[i];
    }
    client *c = new client;
    c->num = clients.length();
    c->info = server::newclientinfo();
    clients.add(c);
    return *c;
}

int localclients = 0, nonlocalclients = 0;

bool hasnonlocalclients() { return nonlocalclients!=0; }
bool haslocalclients() { return localclients!=0; }

#ifdef STANDALONE
bool resolverwait(const char *name, ENetAddress *address)
{
    return enet_address_set_host(address, name) >= 0;
}

int connectwithtimeout(ENetSocket sock, const char *hostname, const ENetAddress &remoteaddress)
{
    int result = enet_socket_connect(sock, &remoteaddress);
    if(result<0) enet_socket_destroy(sock);
    return result;
}
#endif

ENetSocket mastersock = ENET_SOCKET_NULL;
ENetAddress masteraddress = { ENET_HOST_ANY, ENET_PORT_ANY }, serveraddress = { ENET_HOST_ANY, ENET_PORT_ANY };
int lastupdatemaster = 0;
vector<char> masterout, masterin;
int masteroutpos = 0, masterinpos = 0;
VARN(updatemaster, allowupdatemaster, 0, 1, 1);

void disconnectmaster()
{
    if(mastersock == ENET_SOCKET_NULL) return;

    enet_socket_destroy(mastersock);
    mastersock = ENET_SOCKET_NULL;

    masterout.setsize(0);
    masterin.setsize(0);
    masteroutpos = masterinpos = 0;

    masteraddress.host = ENET_HOST_ANY;
    masteraddress.port = ENET_PORT_ANY;

    lastupdatemaster = 0;
}

SVARF(mastername, server::defaultmaster(), disconnectmaster());
VARF(masterport, 1, server::masterport(), 0xFFFF, disconnectmaster());

ENetSocket connectmaster()
{
    if(!mastername[0]) return ENET_SOCKET_NULL;

    if(masteraddress.host == ENET_HOST_ANY)
    {
#ifdef STANDALONE
        printf("\33[32mConnecting to %s...\33[0m\n", mastername);
#endif
        masteraddress.port = masterport;
        if(!resolverwait(mastername, &masteraddress)) return ENET_SOCKET_NULL;
    }
    ENetSocket sock = enet_socket_create(ENET_SOCKET_TYPE_STREAM);
    if(sock != ENET_SOCKET_NULL && serveraddress.host != ENET_HOST_ANY && enet_socket_bind(sock, &serveraddress) < 0)
    {
        enet_socket_destroy(sock);
        sock = ENET_SOCKET_NULL;
    }
    if(sock == ENET_SOCKET_NULL || connectwithtimeout(sock, mastername, masteraddress) < 0)
    {
#ifdef STANDALONE
        printf(sock==ENET_SOCKET_NULL ? "could not open socket\n" : "could not connect to %s\n", mastername);
#endif
        return ENET_SOCKET_NULL;
    }

    enet_socket_set_option(sock, ENET_SOCKOPT_NONBLOCK, 1);
    return sock;
}

bool requestmaster(const char *req)
{
    if(mastersock == ENET_SOCKET_NULL)
    {
        mastersock = connectmaster();
        if(mastersock == ENET_SOCKET_NULL) return false;
    }

    masterout.put(req, strlen(req));
    return true;
}

bool requestmasterf(const char *fmt, ...)
{
    defvformatstring(req, fmt, fmt);
    return requestmaster(req);
}
void processmasterinput()
{
    if(masterinpos >= masterin.length()) return;

    char *input = &masterin[masterinpos], *end = (char *)memchr(input, '\n', masterin.length() - masterinpos);
    while(end)
    {
        *end++ = '\0';

        const char *args = input;
        while(args < end && !isspace(*args)) args++;
        int cmdlen = args - input;
        while(args < end && isspace(*args)) args++;

        if(!strncmp(input, "failreg", cmdlen))
            conoutf(CON_ERROR, "\nError: Master server registration to %s: %s\n", mastername, args);
        else if(!strncmp(input, "succreg", cmdlen))
        	conoutf("Successfully registered to Masterserver: %s", mastername);
        else server::processmasterinput(input, cmdlen, args);

        masterinpos = end - masterin.getbuf();
        input = end;
        end = (char *)memchr(input, '\n', masterin.length() - masterinpos);
    }

    if(masterinpos >= masterin.length())
    {
        masterin.setsize(0);
        masterinpos = 0;
    }
}

void flushmasteroutput()
{
    if(masterout.empty()) return;

    ENetBuffer buf;
    buf.data = &masterout[masteroutpos];
    buf.dataLength = masterout.length() - masteroutpos;
    int sent = enet_socket_send(mastersock, NULL, &buf, 1);
    if(sent >= 0)
    {
        masteroutpos += sent;
        if(masteroutpos >= masterout.length())
        {
            masterout.setsize(0);
            masteroutpos = 0;
        }
    }
    else disconnectmaster();
}

void flushmasterinput()
{
    if(masterin.length() >= masterin.capacity())
        masterin.reserve(4096);

    ENetBuffer buf;
    buf.data = &masterin[masterin.length()];
    buf.dataLength = masterin.capacity() - masterin.length();
    int recv = enet_socket_receive(mastersock, NULL, &buf, 1);
    if(recv > 0)
    {
        masterin.advance(recv);
        processmasterinput();
    }
    else disconnectmaster();
}

static ENetAddress pongaddr;

void sendserverinforeply(ucharbuf &p)
{
    ENetBuffer buf;
    buf.data = p.buf;
    buf.dataLength = p.length();
    enet_socket_send(pongsock, &pongaddr, &buf, 1);
}

void checkserversockets()        // reply all server info requests
{
    static ENetSocketSet sockset;
    ENET_SOCKETSET_EMPTY(sockset);
    ENetSocket maxsock = pongsock;
    ENET_SOCKETSET_ADD(sockset, pongsock);
    if(mastersock != ENET_SOCKET_NULL)
    {
        maxsock = max(maxsock, mastersock);
        ENET_SOCKETSET_ADD(sockset, mastersock);
    }
    if(lansock != ENET_SOCKET_NULL)
    {
        maxsock = max(maxsock, lansock);
        ENET_SOCKETSET_ADD(sockset, lansock);
    }
    if(enet_socketset_select(maxsock, &sockset, NULL, 0) <= 0) return;

    ENetBuffer buf;
    uchar pong[MAXTRANS];
    loopi(2)
    {
        ENetSocket sock = i ? lansock : pongsock;
        if(sock == ENET_SOCKET_NULL || !ENET_SOCKETSET_CHECK(sockset, sock)) continue;

        buf.data = pong;
        buf.dataLength = sizeof(pong);
        int len = enet_socket_receive(sock, &pongaddr, &buf, 1);
        if(len < 0) return;
        ucharbuf req(pong, len), p(pong, sizeof(pong));
        p.len += len;
        server::serverinforeply(req, p);
    }

    if(mastersock != ENET_SOCKET_NULL && ENET_SOCKETSET_CHECK(sockset, mastersock)) flushmasterinput();
}

#define DEFAULTCLIENTS 8

#ifdef STANDALONE
int curtime = 0, lastmillis = 0, totalmillis = 0;
#endif
VARF(serverport, 0, server::serverport(), 0xFFFF, { if(!serverport) serverport = server::serverport(); });
void updatemasterserver()
{
    if(mastername[0] && allowupdatemaster) requestmasterf("regserv %d\n", serverport);
    lastupdatemaster = totalmillis ? totalmillis : 1;
}

VARF(maxclients, 0, DEFAULTCLIENTS, MAXCLIENTS, { if(!maxclients) maxclients = DEFAULTCLIENTS; });
VAR(serveruprate, 0, 0, INT_MAX);
SVAR(serverip, "");

void serverslice(bool dedicated, uint timeout)   // main server update, called from main loop in sp, or from below in dedicated server
{
    localclients = nonlocalclients = 0;
    loopv(clients) switch(clients[i]->type)
    {
        case ST_LOCAL: localclients++; break;
        case ST_TCPIP: nonlocalclients++; break;
    }

    if(!serverhost)
    {
        server::serverupdate();
        server::sendpackets();
        return;
    }

    // below is network only

    if(dedicated)
    {
        int millis = (int)enet_time_get();
        curtime = server::ispaused() ? 0 : millis - totalmillis;
        totalmillis = millis;
        lastmillis += curtime;
    }
    server::serverupdate();

    flushmasteroutput();
    checkserversockets();

    if(!lastupdatemaster || totalmillis-lastupdatemaster>60*60*1000)       // send alive signal to masterserver every hour of uptime
        updatemasterserver();

    if(totalmillis-laststatus>60*1000)   // display bandwidth stats, useful for server ops
    {
        int slots = maxclients-nonlocalclients;
        laststatus = totalmillis;
        if(nonlocalclients || serverhost->totalSentData || serverhost->totalReceivedData) printf("\nQServ Status: %d client(s) connected. %d slot(s) remaining. Max clients allowed: %i \n%.1f Packet(s) sent, %.1f packet(s) recieved (K/sec)\n\n", nonlocalclients, slots, maxclients, serverhost->totalSentData/60.0f/1024, serverhost->totalReceivedData/60.0f/1024);
		serverhost->totalSentData = serverhost->totalReceivedData = 0;
        
    }

    ENetEvent event;
    bool serviced = false;
    while(!serviced)
    {
        if(enet_host_check_events(serverhost, &event) <= 0)
        {
            if(enet_host_service(serverhost, &event, timeout) <= 0) break;
            serviced = true;
        }
        switch(event.type)
        {
            case ENET_EVENT_TYPE_CONNECT:
            {
                client &c = addclient();
                c.type = ST_TCPIP;
                c.peer = event.peer;
                c.peer->data = &c;
                char hn[1024];

                copystring(c.hostname, (enet_address_get_host_ip(&c.peer->address, hn, sizeof(hn))==0) ? hn : "unknown");

		        //printf("Incomming connection (%s)\n", c.hostname);
                int reason = server::clientconnect(c.num, c.peer->address.host, c.hostname);
                if(!reason) nonlocalclients++;
                else disconnect_client(c.num, reason);
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE:
            {
                client *c = (client *)event.peer->data;
                if(c) process(event.packet, c->num, event.channelID);
                if(event.packet->referenceCount==0) enet_packet_destroy(event.packet);
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT:
            {
                client *c = (client *)event.peer->data;
                if(!c) break;
                //printf("Client disconnected (%s)\n", c->hostname);
                server::clientdisconnect(c->num);
                nonlocalclients--;
                c->type = ST_EMPTY;
                event.peer->data = NULL;
                server::deleteclientinfo(c->info);
                c->info = NULL;
                break;
            }
            default:
                break;
        }
    }
    if(server::sendpackets()) enet_host_flush(serverhost);
}

void flushserver(bool force)
{
    if(server::sendpackets(force) && serverhost) enet_host_flush(serverhost);
}

#ifndef STANDALONE
void localdisconnect(bool cleanup)
{
    bool disconnected = false;
    loopv(clients) if(clients[i]->type==ST_LOCAL)
    {
        server::localdisconnect(i);
        localclients--;
        clients[i]->type = ST_EMPTY;
        server::deleteclientinfo(clients[i]->info);
        clients[i]->info = NULL;
        disconnected = true;
    }
    if(!disconnected) return;
    game::gamedisconnect(cleanup);
    mainmenu = 1;
}

void localconnect()
{
    client &c = addclient();
    c.type = ST_LOCAL;
    copystring(c.hostname, "local");
    localclients++;
    game::gameconnect(false);
    server::localconnect(c.num);
}
#endif

void rundedicatedserver()
{
    #ifdef WIN32
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    #endif
	server::startserv();
    for(;;) serverslice(true, 5);
}

bool servererror(bool dedicated, const char *desc)
{
#ifndef STANDALONE
    if(!dedicated)
    {
        conoutf(CON_ERROR, desc);
        cleanupserver();
    }
    else
#endif
        fatal(desc);
    return false;
}

bool setuplistenserver(bool dedicated)
{
    ENetAddress address = { ENET_HOST_ANY, serverport <= 0 ? server::serverport() : serverport };
    if(*serverip)
    {
        if(enet_address_set_host(&address, serverip)<0) conoutf(CON_WARN, "WARNING: server ip not resolved");
        else serveraddress.host = address.host;
    }
    serverhost = enet_host_create(&address, min(maxclients + server::reserveclients(), MAXCLIENTS), server::numchannels(), 0, serveruprate);
    if(!serverhost) return servererror(dedicated, "\f3Error: \f7Could not create server host");
    loopi(maxclients)serverhost->peers[i].data = NULL;
    address.port = server::serverinfoport(serverport > 0 ? serverport : -1);
    pongsock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if(pongsock != ENET_SOCKET_NULL && enet_socket_bind(pongsock, &address) < 0)
    {
        enet_socket_destroy(pongsock);
        pongsock = ENET_SOCKET_NULL;
    }
    if(pongsock == ENET_SOCKET_NULL) return servererror(dedicated, "\f3Error: \f7could not create server info socket");
    else enet_socket_set_option(pongsock, ENET_SOCKOPT_NONBLOCK, 1);
    address.port = server::laninfoport();
    lansock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if(lansock != ENET_SOCKET_NULL && (enet_socket_set_option(lansock, ENET_SOCKOPT_REUSEADDR, 1) < 0 || enet_socket_bind(lansock, &address) < 0))
    {
        enet_socket_destroy(lansock);
        lansock = ENET_SOCKET_NULL;
    }
    if(lansock == ENET_SOCKET_NULL) conoutf(CON_WARN, "WARNING: Could not create LAN server info socket");
    else enet_socket_set_option(lansock, ENET_SOCKOPT_NONBLOCK, 1);
    return true;
}

void initserver(bool listen, bool dedicated)
{
    if(dedicated) execfile("server-init.cfg", false);

    if(listen) setuplistenserver(dedicated);

    server::serverinit();

    if(listen)
    {
        updatemasterserver();
        if(dedicated) rundedicatedserver(); // never returns
#ifndef STANDALONE
        else conoutf("Listen server started");
#endif
    }
}

#ifndef STANDALONE
void startlistenserver(int *usemaster)
{
    if(serverhost) { conoutf(CON_ERROR, "listen server is already running"); return; }

    allowupdatemaster = *usemaster>0 ? 1 : 0;

    if(!setuplistenserver(false)) return;

    updatemasterserver();

    conoutf("listen server started for %d clients%s", maxclients, allowupdatemaster ? " and listed with master server" : "");
}
COMMAND(startlistenserver, "i");

void stoplistenserver()
{
    if(!serverhost) { conoutf(CON_ERROR, "\f3Error: \f7Listen server is not running"); return; }

    kicknonlocalclients();
    enet_host_flush(serverhost);
    cleanupserver();

    conoutf("Listen server stopped");
}
COMMAND(stoplistenserver, "");
#endif

bool serveroption(char *opt)
{
    switch(opt[1])
    {
        case 'u': setvar("serveruprate", atoi(opt+2)); return true;
        case 'c': setvar("maxclients", atoi(opt+2)); return true;
        case 'i': setsvar("serverip", opt+2); return true;
        case 'j': setvar("serverport", atoi(opt+2)); return true;
        case 'm': setsvar("mastername", opt+2); setvar("updatemaster", mastername[0] ? 1 : 0); return true;
#ifdef STANDALONE
        case 'q': printf("Using home directory: %s\n", opt+2); sethomedir(opt+2); return true;
        case 'k': printf("Adding package directory: %s\n", opt+2); addpackagedir(opt+2); return true;
#endif
        default: return false;
    }
}

vector<const char *> gameargs;

#ifdef STANDALONE
void *ServerInit(void *x)
{
    initserver(true, true);
}
void *IRCInit(void *x)
{
	sleep(1); //avoid segmentation fault by waiting for the server to start before we begin irc
    if(getvar("enableirc")) {irc.init();}
}
int main(int argc, char* argv[])
{
    luavm.qservLuaInit();
    pthread_t server_thread, irc_thread;
    int iserver,iirc;
    iserver = 1;
    iirc = 2;

    void *ServerInit(void *);
    if(enet_initialize()<0) fatal("\f3Fatal Error: \f7Unable to initialise network module");
    atexit(enet_deinitialize);
    enet_time_set(0);
    for(int i = 1; i<argc; i++) if(argv[i][0]!='-' || !serveroption(argv[i])) gameargs.add(argv[i]);
    game::parseoptions(gameargs);

    pthread_create(&server_thread,NULL,ServerInit,&iserver);
    pthread_create(&irc_thread,NULL,IRCInit,&iirc);
    pthread_join(server_thread,NULL);
    pthread_join(irc_thread,NULL);

    return 0;
}

#endif
