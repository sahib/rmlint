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
STRIP=strip -s

#compiling stuff
CC=gcc

#Heavy Warnlevel 
WARN=-ansi -Wall -pedantic 

#Quite heavy optimization 
OPTI= -march=native -Os -finline-functions -fomit-frame-pointer -s 

#Link with google's malloc 
TCMALLOC=-fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free -L tcmalloc/libtcmalloc_minimal.a

#also use -pipe 
CFLAGS=-c -pipe $(OPTI) $(WARN) $(TCMALLOC) 

#Link flags 
LDFLAGS=-lpthread -lm -flto 

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
	     @$(STRIP) $(EXECUTABLE)
	     @$(ECHO) "=> Stripping program done."
	     @$(ECHO) "=> Linking program done."  

.c.o: 	
	@$(ECHO) "-> Compiling $<"
	@$(CC) $(INCLUDE) $(CFLAGS) $< -o $@

clean:
	@$(ECHO) "<> Making clean."
	@$(RM) $(SOURCEDIR)/*.o $(EXECUTABLE) 2> /dev/null

install: 
	@$(ECHO) "++ Copying executable to /usr/bin."
	@$(CP) $(EXECUTABLE) $(INSTALLPATH)
	@$(ECHO) "++ Copying manpage to /usr/share/man/man1."
	@$(CP) doc/rmlint.1.gz $(MANDIR)
