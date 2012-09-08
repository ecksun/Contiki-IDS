CONTIKI_PROJECT=border-router
all: $(CONTIKI_PROJECT)
APPS += slip-cmd ids-common ids-server

CONTIKI=../../../../..

WITH_UIP6=1
UIP_CONF_IPV6=1
CFLAGS+= -DUIP_CONF_IPV6_RPL

#linker optimizations
# SMALL=1K

CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"
PROJECT_SOURCEFILES += border-router-cmds.c tun-bridge.c border-router-rdc.c \
slip-config.c slip-dev.c ids-common.c

WITH_WEBSERVER=0

include $(CONTIKI)/Makefile.include

connect-router:	border-router.native
	sudo ./border-router.native aaaa::1/64
