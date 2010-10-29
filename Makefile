## This is the makefile for mpdbox 
## Just type 'make -j 4'
## Targets: 'mpdbox','clean'
##

#source dir
SOURCEDIR=src

#MANDIR
MANDIR=/usr/share/man/man1

#install
INSTALLPATH=/usr/bin

#Programs 
ECHO=echo
RM=rm -rf
CAT=cat
CP=cp

#compiling stuff
CC=gcc
 
WARN=-std=c99 -Wall -pedantic 
OPTI= -march=native -s -O3
TCMALLOC=-fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free -L tcmalloc/libtcmalloc_minimal.a
CFLAGS=-c -pipe $(OPTI) $(WARN) $(TCMALLOC)

LDFLAGS=-lpthread -lm -pg

SOURCES= \
	$(SOURCEDIR)/md5.c \
	$(SOURCEDIR)/filter.c \
	$(SOURCEDIR)/mode.c \
	$(SOURCEDIR)/list.c \
	$(SOURCEDIR)/rmlint.c \
	$(SOURCEDIR)/main.c


OBJECTS=$(SOURCES:.c=.o)
INCLUDE=
EXECUTABLE=rmlint 

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE):  $(OBJECTS)
	     @$(CC) $(LDFLAGS) $(OBJECTS) -o $@
	     @$(ECHO) "=> Linking program done."  

.c.o: 	
	@$(ECHO) "-> Compiling $<"
	@$(CC) $(INCLUDE) $(LDFLAGS)  $(CFLAGS) $< -o $@

clean:
	@$(ECHO) "<> Making clean."
	@$(RM) $(SOURCEDIR)/*.o $(EXECUTABLE) 2> /dev/null

install: 
	$(CP) $(EXECUTABLE) $(INSTALLPATH)
	$(CP) doc/rmlint.1.gz $(MANDIR)
