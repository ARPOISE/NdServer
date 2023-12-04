##############################################################################
#
#  Copyright (c) 2023, Peter Graf
#  All rights reserved
#
##############################################################################

EXPORTPATH = ../exports

AR=      /usr/bin/ar
RANLIB=  /usr/bin/ar ts
NULL=
IPATH=   -I. -I$(EXPORTPATH)
CFLAGS=  -Wall -O3 ${IPATH}
CC= gcc

EXE_OBJS =   ndServer.o ndConnection.o ndDispatch.o ndScene.o pblLog.o tcpPacket.o ndConnectionMap.o ndRequest.o pblProcessInit.o

INCLIB   = $(EXPORTPATH)/lxgc/libpbl.a \


THEEXE    = ndserver

all: $(THEEXE)

$(THEEXE):  $(EXE_OBJS) $(THELIB)
	$(CC) -O3 -o $(THEEXE) $(EXE_OBJS) $(THELIB) $(INCLIB) -lm

export: exportinclude exportlib

exportinclude:

exportlib:

clean:
	rm -f ${EXE_OBJS} $(THEEXE)
