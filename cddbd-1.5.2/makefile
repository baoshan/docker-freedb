#
#   @(#)$Id: makefile,v 1.40.2.9 2006/07/01 10:48:36 joerg78 Exp $
#
#   cddbd - CD Database Protocol Server
#
#   Copyright (C) 1996       Steve Scherf (steve@moonsoft.com)
#   Portions Copyright (C) 2001-2006  by various authors 
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#

CMD =    cddbd
SRC =    cddbd.c charset.c crc.c db.c encode.c fuzzy.c help.c inet.c mail.c \
         strutil.c xmit.c bit_array.c
OBJ =    cddbd.o charset.o crc.o db.o encode.o fuzzy.o help.o inet.o mail.o \
         strutil.o secure.o xmit.o bit_array.o
HDR =    cddbd.h list.h patchlevel.h utf8.h bit_array.h
GHDR =	 access.h configurables.h
GSRC =	 secure.c
GEN =    $(GHDR) $(GSRC) .accessfile
SCRIPT = config.sh install.sh
TEXT =   CDDBPROTO CDDBD_HOWTO COPYING DBFORMAT README CHANGELOG
MISC =   access.hdr makefile passwd.hdr sites.hdr submit.cgi.template
MANIFEST=MANIFEST
INSTALL = access access.old access.alt access.alt.old motd motd.old passwd sites submit.cgi
RELEASE = $(MANIFEST) $(CMD).1

# HPUX requires special flags for to make the compiler ANSI
# OSCFLAGS = -Ae

# SVR4-derived systems might need these libraries
# Solaris 9 / SunOS 5.9 needs these libraries
# OSLDFLAGS = -lnsl -lsocket

# cf ... debugging; mostly for use with gcc
cf= # -g -Wall -Wuninitialized -Wunused -pedantic -Wformat
# the following is used for Alpha/OSF1 V4.0
# cf=-g4 -w0 -O0

CFLAGS += -O $(OSCFLAGS) $(cf)
LDFLAGS = $(OSLDFLAGS)

CDDBDVER := cddbd-1.5.2
DOCDATE := `date +'%F'`

all: $(CMD)
	@echo You must run install.sh to install the new server.

$(CMD):	$(OBJ)
	$(CC) $(CFLAGS) -o $(CMD) $(OBJ) $(LDFLAGS)

config:
	./config.sh

install:
	./install.sh

.c.o:
	$(CC) $(CFLAGS) -c $<

$(CMD).1: $(CMD).pod
	pod2man --section=1 --center="CD Database Server" --date=$(DOCDATE) --release=$(CDDBDVER) $(CMD).pod > $@

clean:
	rm -rf $(OBJ) $(GEN) $(CDDBDVER)

distclean: clean
	rm -f $(CMD) $(CMD).exe $(CDDBDVER).tar* $(INSTALL)

develclean: distclean
	rm -f $(RELEASE)

lint:
	lint $(SRC) $(GSRC)

touch:
	touch $(SRC) $(GSRC)

tags:
	ctags $(SRC) $(HDR) $(GEN)

tar: $(CMD).1
	# Make the tar directory.
	rm -rf $(CDDBDVER)
	mkdir $(CDDBDVER)
	cp $(SRC) $(HDR) $(SCRIPT) $(TEXT) $(MISC) $(CMD).1 $(CDDBDVER)

	# Create a dummy manifest.
	touch $(CDDBDVER)/$(MANIFEST)
	chmod 444 $(CDDBDVER)/*
	for i in $(SCRIPT) ; \
	do \
		chmod 544 $(CDDBDVER)/$${i} ; \
	done

	# Create the second dummy manifest, which should be the correct size.
	rm -f $(MANIFEST)
	cp $(MANIFEST).hdr $(MANIFEST)
	ls -l $(CDDBDVER) | grep -v total >> $(MANIFEST)
	chmod 644 $(CDDBDVER)/$(MANIFEST)
	cp $(MANIFEST) $(CDDBDVER)
	chmod 444 $(CDDBDVER)/*
	for i in $(SCRIPT) ; \
	do \
		chmod 544 $(CDDBDVER)/$${i} ; \
	done

	# Create the real manifest.
	cp $(MANIFEST).hdr $(MANIFEST)
	ls -l $(CDDBDVER) | grep -v total >> $(MANIFEST)
	chmod 644 $(CDDBDVER)/$(MANIFEST)
	cp $(MANIFEST) $(CDDBDVER)

	# Create the tar file.
	chmod 444 $(CDDBDVER)/*
	for i in $(SCRIPT) ; \
	do \
		chmod 544 $(CDDBDVER)/$${i} ; \
	done
	chmod 755 $(CDDBDVER)
	tar cvf $(CDDBDVER).tar $(CDDBDVER)

gzip: tar
	gzip -c9 $(CDDBDVER).tar > $(CDDBDVER).tar.gz

uu: tar
	gzip -c9 $(CDDBDVER).tar > $(CDDBDVER).tar.gz
	uuencode $(CDDBDVER).tar.gz $(CDDBDVER).tar.gz > $(CDDBDVER).uu

size:
	wc $(SRC) $(HDR)

ci:
	ci $(TEXT) $(HDR) $(MISC) $(SCRIPT) $(SRC) $(MANIFEST).hdr

co:
	co -l $(TEXT) $(HDR) $(MISC) $(SCRIPT) $(SRC) $(MANIFEST).hdr

# Header dependencies
access.h: config.sh
	sh config.sh
bit_array.o: bit_array.h
cddbd.o: access.h cddbd.h configurables.h list.h patchlevel.h
charset.o: cddbd.h utf8.h
crc.o: cddbd.h list.h
db.o: cddbd.h configurables.h list.h patchlevel.h
encode.o: cddbd.h list.h
fuzzy.o: bit_array.h cddbd.h configurables.h list.h
help.o:
inet.o: access.h cddbd.h configurables.h list.h patchlevel.h
mail.o: cddbd.h configurables.h list.h
secure.o: access.h secure.c
xmit.o: cddbd.h configurables.h list.h
