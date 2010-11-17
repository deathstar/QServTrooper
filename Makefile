DEBUG=true

#QSERV_VERSION=$(shell git log --abbrev-commit --pretty=format:%h -1)

programs=qserv
eventdir=libevent2
enetdir=enet

qserv_SRCS=shared/crypto.cpp shared/stream.cpp shared/tools.cpp engine/command.cpp engine/server.cpp fpsgame/server.cpp
qserv_EXTRA_DEPS=$(enetdir)/.libs/libenet.a $(eventdir)/.libs/libevent.a
qserv_CXXFLAGS=-Wall -fomit-frame-pointer -fsigned-char -Ienet/include -I$(eventdir)/include -I$(eventdir) -DSTANDALONE -Ishared -Iengine
qserv_LDFLAGS=$(enetdir)/.libs/libenet.a $(eventdir)/.libs/libevent.a
qserv_LIBS=GeoIP z resolv
extra=config.h config.mk

ifeq ($(DEBUG),true)
qserv_CXXFLAGS+=-g
qserv_LDFLAGS+=-g
else
qserv_CXXFLAGS+=-O3
endif

-include config.mk

include common.mk

config.h:
config.mk:
	@if ! ./config.sh; then exit 1; fi

enet/.libs/libenet.a: enet/Makefile
	@echo "$(COMPILING)Building enet$(RESETC)"
	@cd enet && $(MAKE) libenet.la

enet/Makefile:
	@echo "$(COMPILING)Configuring enet$(RESETC)"
	@cd enet && ./configure

$(eventdir)/.libs/libevent.a: $(eventdir)/Makefile
	@echo "$(COMPILING)Building libevent$(RESETC)"
	@cd $(eventdir) && $(MAKE) event-config.h libevent.la

$(eventdir)/Makefile: $(eventdir)/configure
	@echo "$(COMPILING)Configuring libevent$(RESETC)"
	@cd $(eventdir) && ./configure

.PHONY: eclean
eclean: clean
	@if [ -f enet/Makefile ]; then cd enet && $(MAKE) distclean; fi
	@if [ -f $(eventdir)/Makefile ]; then cd $(eventdir) && $(MAKE) distclean; fi

.PHONY: distclean
distclean: eclean
	@rm -f config.mk config.h

