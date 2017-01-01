CONTIKI = ../../..
APPS = powertrace collect-view ids
CONTIKI_PROJECT = udp-sender udp-sink network-mapper network-mapper-client
PROJECT_SOURCEFILES += collect-common.c

WITH_UIP6=1
UIP_CONF_IPV6=1
UIP_CONF_ICMP6=1
CFLAGS+= -DUIP_CONF_IPV6_RPL -DUIP_CONF_ICMP6

CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

ifdef PERIOD
CFLAGS=-DPERIOD=$(PERIOD)
endif

all: $(CONTIKI_PROJECT)

include $(CONTIKI)/Makefile.include

