CONTIKI_PROJECT=probe
all: $(CONTIKI_PROJECT) 

CONTIKI= ../../

WITH_UIP6=1
UIP_CONF_IPV6=1

#linker optimizations
SMALL=1

CFLAGS += -DWITH_COAP
APPS += er-coap
APPS += rest-engine

include $(CONTIKI)/Makefile.include

