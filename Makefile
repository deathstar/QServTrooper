CC = g++

CFLAGS =-w -fomit-frame-pointer -fsigned-char -Ienet/include -DSTANDALONE -Ishared -Iengine -Ifpsgame

COMPILE = $(CC) $(CFLAGS) -c
LDFLAGS := -lGeoIP -lenet -lz

SERVER := qserv

OBJFILES := shared/crypto.o shared/stream.o shared/tools.o engine/command.o engine/server.o fpsgame/server.o shared/IRCbot.o

all: $(SERVER)

$(SERVER): $(OBJFILES)
	$(CC) -o $(SERVER) $(OBJFILES) $(LDFLAGS)

%.o: %.cpp
	$(COMPILE) -o $@ $<

clean:
	@rm -f $(OBJFILES) $(SERVER)