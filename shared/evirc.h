// IRC client code on top of libevent2

#ifndef EVIRC_H__
#define EVIRC_H__

#include <event2/event.h>
#include <event2/dns.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include <vector>

namespace IRC {

struct Peer {
	char *nick;
	int count;
	char data[512]; // user data actually
	void set_nick(char *nick_) {
		if(nick) free(nick);
		if(nick_) nick = strdup(nick_);
		else nick = NULL;
	}

	Peer(): nick(0), count(0) { data[0] = 0; }
	~Peer() {
		if(nick) free(nick);
	}
};

struct ChannelPeer {
	Peer *peer;
};

struct Server;
struct Channel {
	char *name;
	char *alias;
	std::vector <ChannelPeer *> peers;
	int verbosity;
	Server *server;

	ChannelPeer *peerjoin(char *peername);
	void peerpart(char *nick);

	void speak(const char *fmt, ...);
	void vspeak(const char *fmt, va_list ap);
	void speak(int verbosity, const char *fmt, ...);
	void vspeak(int verbosity, const char *fmt, va_list ap);
};

struct Client;
struct Server {
	char *host;
	char *alias; // alias for the hostname ie irc.quakenet.org -> QuakeNet
	int port;
	char *nick;
	enum { None, Connecting, Connected, SentIdent, Active, Quitting };
	int state;
	bufferevent *buf;
	event *reconnect_event;
	evbuffer *evbuf;
	Client *client;
	std::vector <Peer *> peers;
	std::vector <Channel *> channels;

	Server() {}
	~Server() {}

	void init();

	bool connect(const char *host, const char *nick, int port = 6667, const char *alias = NULL);
	void quit(const char *msg = NULL, int quitsecs = 1); // quit with a message, force disconnect after quitsecs seconds
	void join(const char *channel, int verbosity_ = 0, const char *alias = NULL);
	void part(const char *channel);
	void read(void);

	void parse(char *line);
	void process(char *prefix, char *command, char *params[], int nparams, char *trailing);

	Channel *findchan(char *name) {
		if(!name) return NULL;
		for(unsigned int i = 0; i < channels.size(); i++) {
			if(!strcasecmp(channels[i]->name, name)) return channels[i];
		}
		return NULL;
	}

	Peer *addpeer(char *peername);
	inline Peer *findpeer(char *peername) { return addpeer(peername); }
	void delpeer(Peer *p);
	void changenick(char *old, char *nick_);

	bool is_chan(char *name) { // checks if a string is a channel name
		return (*name == '#' || *name == '&'); //FIXME: get channel types from server messages
	}

	void speak(const char *fmt, ...); // speaks on all channels that we joined on this server
	void vspeak(const char *fmt, va_list ap);

	void speak(int verbosity, const char *fmt, ...); // speaks on all channels that we joined on this server, if verbosity is not higher than the channel's verbosity
	void vspeak(int verbosity, const char *fmt, va_list ap);

	void speakto(const char *to, const char *fmt, ...); // speaks to the specified channel or peer
	void vspeakto(const char *to, const char *fmt, va_list ap);
	void bspeakto(const char *to, evbuffer *evb);
};

struct Source {
	Server *server;
	Client *client;
	Peer *peer; //FIXME: replace with Peer *
	Channel *channel;

	void reply(const char *fmt, ...); // on channels it replies with "nick: message". in privmsg it replies with "message"
	void vreply(const char *fmt, va_list ap); // on channels it replies with "nick: message". in privmsg it replies with "message"
	void speak(const char *fmt, ...); // simply speak, without prepending "nick: "
	void vspeak(const char *fmt, va_list ap); // simply speak, without prepending "nick: "
};

struct Client {
	std::vector <Server *> servers;
	event_base *base;
	evdns_base *dnsbase;

	Client() { init(); }
	Client(event_base *b, evdns_base *db) { base = b; dnsbase = db; init(); }
	~Client() {}

	void init() {
		channel_message_cb = private_message_cb = 0;
		channel_action_message_cb = private_action_message_cb = 0;
		notice_cb = motd_cb = unhandled_cb = ping_cb = 0;
		join_cb = 0;
		part_cb = 0;
		server_quit_cb = 0;
		empty_cb = 0;
		mode_cb = 0;
		nick_cb = 0;
	}

	typedef void (*GenericCallback)(Server *, char*, char *);
	typedef void (*MessageCallback)(Source *, char *msg);
	typedef void (*ServerQuitCallback)(Server *);
	typedef void (*EmptyCallback)(void);
	typedef void (*JoinCallback)(Source *);
	typedef void (*PartCallback)(Source *, char *reason);
	typedef void (*ModeCallback)(Source *, char *who, char *target, char *mode, char *args);
	typedef void (*NickCallback)(Source *, char *newnick);

	MessageCallback channel_message_cb, private_message_cb;
	MessageCallback channel_action_message_cb, private_action_message_cb;
	GenericCallback notice_cb, motd_cb, unhandled_cb, ping_cb;
	ModeCallback mode_cb;
	JoinCallback join_cb;
	PartCallback part_cb;
	ServerQuitCallback server_quit_cb; // gets called when a server quitses
	EmptyCallback empty_cb; // gets called when all servers are disconnected
	NickCallback nick_cb;

	bool connect(const char *host, const char *nick, int port = 6667, const char *alias = NULL);
	void quit(const char *msg);
	Server *findserv(const char *host) {
		for(unsigned int i = 0; i < servers.size(); i++) {
			if(!strcmp(servers[i]->host, host)) return servers[i];
		}
		return NULL;
	}
	bool delserv(Server *s) {
		if(!s) return false;
		delete s;
		for(unsigned int i = 0; i < servers.size(); i++) {
			if(servers[i] == s) {
				servers.erase(servers.begin() + i);
				if(servers.size() == 0 && empty_cb) empty_cb();
				return true;
			}
		}
		return false;
	}
	void join(const char *host, const char *channel, int verbosity = 0, const char *alias = NULL) {
		Server *s = findserv(host);
		if(s) s->join(channel, verbosity, alias);
	}
	void part(const char *host, const char *channel) {
		Server *s = findserv(host);
		if(s) s->part(channel);
	}
	void cleanup(void);

	void speak(const char *fmt, ...);
	void vspeak(const char *fmt, va_list ap);
	void speak(int verbosity, const char *fmt, ...);
	void vspeak(int verbosity, const char *fmt, va_list ap);
};

char *stripident(char *ident);
}

#endif /* EVIRC_H__ */
