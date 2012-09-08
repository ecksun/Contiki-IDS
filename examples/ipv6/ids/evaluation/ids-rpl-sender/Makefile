CONTIKI = ../../../../..
APPS = powertrace ids-client
CONTIKI_PROJECT = udp-sender

WITH_UIP6=1
UIP_CONF_IPV6=1
CFLAGS+= -DUIP_CONF_IPV6_RPL

CFLAGS += -DPROJECT_CONF_H=\"../project-conf.h\"

ifdef PERIOD
CFLAGS=-DPERIOD=$(PERIOD)
endif

all: $(CONTIKI_PROJECT)

include $(CONTIKI)/Makefile.include

