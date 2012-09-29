CONTIKI = ../../../..
APPS = firewall-client
CONTIKI_PROJECT = firewall-client-impl

WITH_UIP6=1
UIP_CONF_IPV6=1
UIP_CONF_ICMP6=1
CFLAGS+= -DUIP_CONF_IPV6_RPL -DUIP_CONF_ICMP6

ifdef PERIOD
CFLAGS=-DPERIOD=$(PERIOD)
endif

all: $(CONTIKI_PROJECT)

include $(CONTIKI)/Makefile.include

