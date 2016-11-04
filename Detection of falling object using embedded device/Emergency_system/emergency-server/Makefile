CONTIKI_PROJECT=emergency_server
all: $(CONTIKI_PROJECT) 

CONTIKI= ../../

WITH_UIP6=1
UIP_CONF_IPV6=1
CFLAGS+= -DUIP_CONF_IPV6_RPL
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

#linker optimizations
SMALL=1

CFLAGS += -DWITH_COAP
APPS += er-coap
APPS += rest-engine

PROJECT_SOURCEFILES += slip-bridge.c

CFLAGS += -DWEBSERVER=1
PROJECT_SOURCEFILES += httpd-simple.c

ifeq ($(PREFIX),)
 PREFIX = aaaa::1/64
endif

include $(CONTIKI)/Makefile.include

$(CONTIKI)/tools/tunslip6:	$(CONTIKI)/tools/tunslip6.c
	(cd $(CONTIKI)/tools && $(MAKE) tunslip6)

connect-router:	$(CONTIKI)/tools/tunslip6
	sudo $(CONTIKI)/tools/tunslip6 $(PREFIX)
