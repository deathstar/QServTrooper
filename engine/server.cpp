// server.cpp: little more than enhanced multicaster
// runs dedicated or as client coroutine

#include "engine.h"

event_base *evbase;
evdns_base *dnsbase;
static event serverhost_input_event;
static event pongsock_input_event;
static event lansock_input_event;
static event update_event;
static event netstats_event;
static event stdin_event;
IRC::Client irc;

#ifdef STANDALONE
// thanks to Catelite for making this list
char irc2sauer[] = {
	'7', // 0 = white/default
	'4', // 1 = black/default
	'1', // 2
	'0', // 3
	'3', // 4
	'3', // 5
	'5', // 6
	'6', // 7
	'2', // 8
	'0', // 9
	'1', // 10
	'1', // 11
	'1', // 12
	'5', // 13
	'7', // 14
	'4'  // 15
};

void color_irc2sauer(char *src, char *dst) {
	char *c = src;
	char *d = dst;
	//FIXME: use FSM logic instead
	while(*c) {
		if(*c == 3) {
			c++;
			int color = 0;
			for(int i = 0; i < 2; i++)
				if(*(c) >= '0' && *(c) <= '9') { color *= 10; color += *c - '0'; c++; }
			if(*(c) == ',') { // strip background color
				c++;
				for(int i = 0; i < 2; i++) if(*(c) >= '0' && *(c) <= '9') c++;
			}
			*d++ = '\f';
			if(color < 16) *d++ = irc2sauer[color];
		} else if (*c == 2 || *c == 0x1F || *c == 0x16) c++; // skip bold, underline and italic
		else if(*c == 0x0f) { *d++ = '\f'; *d++ = '7'; }
		*d++ = *c++;
		*d = 0;
	}
}

char sauer2irc[] = {
	3,
	2,
	8,
	4,
	15,
	6,
	7,
	14
};
void color_sauer2irc(char *src, char *dst) {
	char *c = src, *d = dst;
	while(*c) {
		if(*c == '\f') {
			c++;
			int col = *c++ - '0';
			if(col < 0) col = 0;
			if(col > 7) col = 7;
			col = sauer2irc[col];
			if(col == 7) *d++ = 15;
			else {
				*d++ = 3;
				*d++ = '0' + col / 10;
				*d++ = '0' + col % 10;
			}
		} else *d++ = *c++;
		*d = 0;
	}
}

char sauer2console[] = {
	2, // 0 green
	4, // 1 blue
	3, // 2 yellow
	1, // 3 red
	0, // 4 gray
	5, // 5 magenta
	6, // 6 orange -> cyan (no better replacement)
	7, // 7 white
};

void color_sauer2console(char *src, char *dst) {
	copystring(dst, "\033[1;37m");
	for(char *c = src; *c; c++) {
		if(*c == '\f') {
			c++;
			sprintf(dst + strlen(dst), "\033[1;%02dm", 30 + sauer2console[(*c >= '0' && *c <= '7') ? *c - '0' : 7]);
		} else sprintf(dst + strlen(dst), "%c", *c);
	}
	strcat(dst, "\033[0m");
}

void fatal(const char *s, ...)
{
    void cleanupserver();
    cleanupserver(); 
    defvformatstring(msg,s,s);
    printf("servererror: %s\n", msg); 
    exit(EXIT_FAILURE); 
}

void voutf(int v, const char *fmt, va_list args)
{
    string sf, sp;
    vformatstring(sf, fmt, args);

	color_sauer2console(sf, sp);
	if(!(v & OUT_NOCONSOLE)) puts(sp);
    if(irc.base && !(v & OUT_NOIRC)) {
    	color_sauer2irc(sf, sp);
    	irc.speak(v & 0xff, "%s", sp);
    }
    if(!(v & OUT_NOGAME)) server::sendservmsg(sf);
}

void outf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    voutf(OUT_DEFAULT_VERBOSITY, fmt, args);
    va_end(args);
}

void outf(int v, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    voutf(v, fmt, args);
    va_end(args);
}


void conoutfv(int type, const char *fmt, va_list args)
{
	voutf(3 | OUT_NOIRC | OUT_NOGAME, fmt, args);
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
        switch(c)
        {
        case '\f': ++src; continue;
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
//void disconnect_client(int n, int reason);

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
							  
const char *disc_reasons[] = { "network error", "end of packet", "client num", "banned", "tag type (hacking)", "ip is banned", "server is in private mode", "server is full", "connection timed out", "flooding" };

void disconnect_client(int n, int reason)
{
    if(!clients.inrange(n) || clients[n]->type!=ST_TCPIP) return;
    enet_peer_disconnect(clients[n]->peer, reason);
    server::clientdisconnect(n);
    clients[n]->type = ST_EMPTY;
    clients[n]->peer->data = NULL;
    server::deleteclientinfo(clients[n]->info);
    clients[n]->info = NULL;
    defformatstring(s)("Client %s disconnected: %s", clients[n]->hostname, disc_reasons[reason]);
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

void disconnectmaster(bool resetupdate = false)
{
    if(mastersock != ENET_SOCKET_NULL) {
	    enet_socket_destroy(mastersock);
	    mastersock = ENET_SOCKET_NULL;
	}

    masterout.setsize(0);
    masterin.setsize(0);
    masteroutpos = masterinpos = 0;

    masteraddress.host = ENET_HOST_ANY;
    masteraddress.port = ENET_PORT_ANY;

    if(resetupdate) lastupdatemaster = 0;
}

SVARF(mastername, server::defaultmaster(), disconnectmaster(true)); // these two commands are the only time we disconnect on purpose
VARF(masterport, 1, server::masterport(), 0xFFFF, disconnectmaster(true));

ENetSocket connectmaster()
{
    if(!mastername[0]) return ENET_SOCKET_NULL;

    if(masteraddress.host == ENET_HOST_ANY)
    {
#ifdef STANDALONE
        printf("looking up %s...\n", mastername);
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
        printf(sock==ENET_SOCKET_NULL ? "could not open socket\n" : "\f3could not connect\n"); 
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
            outf(2 | OUT_NOGAME, "FATAL: registration to masterserver failed: %s", args);
        else if(!strncmp(input, "succreg", cmdlen))
            outf(2 | OUT_NOGAME, "master server registration succeeded: server available from list");
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

VARF(maxclients, 0, DEFAULTCLIENTS, MAXCLIENTS, { if(!maxclients) maxclients = DEFAULTCLIENTS; });
VAR(serveruprate, 0, 0, INT_MAX);
SVAR(serverip, "");
VARF(serverport, 0, server::serverport(), 0xFFFF, { if(!serverport) serverport = server::serverport(); });

#ifdef STANDALONE
int curtime = 0, lastmillis = 0, totalmillis = 0;
#endif

void updatemasterserver()
{
    if(mastername[0] && allowupdatemaster) requestmasterf("regserv %d\n", serverport);
    lastupdatemaster = totalmillis ? totalmillis : 1;
}

void serverhost_process_event(ENetEvent & event) {
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
			outf(2 | OUT_NOGAME, "IP: %s", c.hostname);
	

            int reason = server::clientconnect(c.num, c.peer->address.host);
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
			outf(2 | OUT_NOGAME, "IP: %s", c->hostname);
            //printf("IP: (%s)\n", c->hostname);
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
    printf("QServ started, waiting for clients...\nCtrl-C to exit and stop server\n\n");
    event_base_dispatch(evbase);
}

bool servererror(bool dedicated, const char *desc)
{
#ifndef STANDALONE
    if(!dedicated)
    {    q

        conoutf(CON_ERROR, desc);
        cleanupserver();
    }
    else
#endif
        fatal(desc);
    return false;
}

static void serverhost_input(int fd, short e, void *arg) {
	if(!(e & EV_READ)) return;
	ENetEvent event;
	while(enet_host_service(serverhost, &event, 0) == 1) serverhost_process_event(event);
	if(server::sendpackets()) enet_host_flush(serverhost); //treat EWOULDBLOCK as packet loss
}

static void serverinfo_input(int fd, short e, void *arg) {
	if(!(e & EV_READ)) return;
	ENetBuffer buf;
	uchar pong[MAXTRANS];
	buf.data = pong;
	buf.dataLength = sizeof(pong);
	int len = enet_socket_receive(fd, &pongaddr, &buf, 1);
	if(len < 0) return;
	ucharbuf req(pong, len), p(pong, sizeof(pong));
	p.len += len;
	server::serverinforeply(req, p);
}
//Server Status printf in terminal 
static void netstats_event_handler(int, short, void *) {     
	if(nonlocalclients || serverhost->totalSentData || serverhost->totalReceivedData)  printf("Status: %d client(s) | %.1f send | %.1f recive (K/sec) \n", nonlocalclients, serverhost->totalSentData/60.0f/1024, serverhost->totalReceivedData/60.0f/1024);	
	serverhost->totalSentData = serverhost->totalReceivedData = 0;
	timeval one_min;
	one_min.tv_sec = 60;
	one_min.tv_usec = 0;
	event_add(&netstats_event, &one_min);
}

static void update_server(int fd, short e, void *arg) {
	timeval to;
	to.tv_sec = 0;
	to.tv_usec = 5000;
	evtimer_add(&update_event, &to);

    if(!lastupdatemaster || (totalmillis-lastupdatemaster) > 3600000) {      // send alive signal to masterserver every hour of uptime
		printf("update_server totalmillis=%d lastupdatemaster=%d totalmillis-lastupdatemaster=%d %d=60*60*1000\n",
			totalmillis, lastupdatemaster, totalmillis - lastupdatemaster, 60*60*1000);
        updatemasterserver();
    }

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

    int millis = (int)enet_time_get();
    curtime = server::ispaused() ? 0 : millis - totalmillis;
    totalmillis = millis;
    lastmillis += curtime;

    server::serverupdate();

    flushmasteroutput();
    checkserversockets();
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
    if(!serverhost) return servererror(dedicated, "could not create server host");
    loopi(maxclients) serverhost->peers[i].data = NULL;
    address.port = server::serverinfoport(serverport > 0 ? serverport : -1);
    pongsock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if(pongsock != ENET_SOCKET_NULL && enet_socket_bind(pongsock, &address) < 0)
    {
        enet_socket_destroy(pongsock);
        pongsock = ENET_SOCKET_NULL;
    }
    if(pongsock == ENET_SOCKET_NULL) return servererror(dedicated, "could not create server info socket");
    else enet_socket_set_option(pongsock, ENET_SOCKOPT_NONBLOCK, 1);
    address.port = server::laninfoport();
    lansock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if(lansock != ENET_SOCKET_NULL && (enet_socket_set_option(lansock, ENET_SOCKOPT_REUSEADDR, 1) < 0 || enet_socket_bind(lansock, &address) < 0))
    {
        enet_socket_destroy(lansock);
        lansock = ENET_SOCKET_NULL;
    }
    if(lansock == ENET_SOCKET_NULL) conoutf(CON_WARN, "WARNING: could not create LAN server info socket");
    else enet_socket_set_option(lansock, ENET_SOCKOPT_NONBLOCK, 1);

	event_assign(&serverhost_input_event, evbase, serverhost->socket, EV_READ | EV_PERSIST, &serverhost_input, NULL);
	event_add(&serverhost_input_event, NULL);
	event_priority_set(&serverhost_input_event, 1);

	event_assign(&pongsock_input_event, evbase, pongsock, EV_READ | EV_PERSIST, &serverinfo_input, NULL);
	event_add(&pongsock_input_event, NULL);

	event_assign(&lansock_input_event, evbase, lansock, EV_READ | EV_PERSIST, &serverinfo_input, NULL);
	event_add(&lansock_input_event, NULL);

	timeval five_ms;
	five_ms.tv_sec = 0;
	five_ms.tv_usec = 5000;
	evtimer_assign(&update_event, evbase, &update_server, NULL);
	evtimer_add(&update_event, &five_ms);

	timeval one_min;
	one_min.tv_sec = 60;
	one_min.tv_usec = 0;
	evtimer_assign(&netstats_event, evbase, &netstats_event_handler, NULL);
	event_add(&netstats_event, &one_min);

    return true;
}
/***************************
 * libevent
 ***************************/
void evinit() {
	evbase = event_base_new();
	dnsbase = evdns_base_new(evbase, 1);
	event_base_priority_init(evbase, 10);

}

/***************************
 *  IRC
 ***************************/
void ircinit() {
	irc.base = evbase;
	irc.dnsbase = dnsbase;
}
ICOMMAND(ircconnect, "ssis", (const char *s, const char *n, int *p, const char *a), {
	if(s && *s && n && *n) irc.connect(s, n, (p&&*p)?*p:6667, (a&&*a)?a:NULL);
});
ICOMMAND(ircjoin, "ssis", (const char *s, const char *c, const int *v, const char *a), {
	if(s && *s && c && *c) irc.join(s, c, (v&&*v)?*v:0, (a&&*a)?a:NULL);
});
ICOMMAND(ircpart, "ss", (const char *s, const char *c), {
	if(s && *s && c && *c) irc.part(s, c);
});
ICOMMAND(ircecho, "C", (const char *msg), {
	string buf;
//	color_sauer2irc((char *)msg, buf);
//	if(scriptircsource) scriptircsource->speak(buf);
});

void initserver(bool listen, bool dedicated)
{
	evinit();
	ircinit();

    if(dedicated) execfile("server-init.cfg", false);

    if(listen) setuplistenserver(dedicated);

    server::serverinit();

    if(listen)
    {
        updatemasterserver();
        if(dedicated) rundedicatedserver(); // never returns
#ifndef STANDALONE
        else conoutf("listen server started");
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
    if(!serverhost) { conoutf(CON_ERROR, "listen server is not running"); return; }

    kicknonlocalclients();
    enet_host_flush(serverhost);
    cleanupserver();

    conoutf("listen server stopped");
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
int main(int argc, char* argv[]) {
    if(enet_initialize()<0) fatal("Unable to initialise network module");
    atexit(enet_deinitialize);
    enet_time_set(0);
    for(int i = 1; i<argc; i++) if(argv[i][0]!='-' || !serveroption(argv[i])) gameargs.add(argv[i]);
    game::parseoptions(gameargs);
    initserver(true, true);
    return 0;
}
#endif
