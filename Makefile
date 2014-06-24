#source dir
SOURCEDIR=src
DOCDIR=doc

#MANDIR
MANDIR=$(DESTDIR)/usr/share/man/man1
#MANUAL
MANDOC=rmlint.1
MANDOC_COMPRESSED=rmlint.1.gz

#install
INSTALLPATH=$(DESTDIR)/usr/bin
UNAME := $(shell sh -c 'uname -s 2>/dev/null || echo not')

#Programs 
ECHO=echo
RM=rm -rf
CAT=cat
CP=cp

ifeq ($(UNAME),Darwin)
	STRIP=strip
	OPTI=-O4
	CC?=clang
else
	STRIP=strip -s
	OPTI=-march=native -O3 -finline-functions
	CC?=gcc
endif

#Link flags
GLIB_FLAGS := $(shell sh -c 'pkg-config --libs --cflags glib-2.0')
LDFLAGS+=-lpthread -lm $(GLIB_FLAGS)

MKDIR=mkdir -p
GZIP=gzip -c -f

#compiling stuff
CC?=gcc

#Heavy Warnlevel 
WARN=-Wall -pedantic

#Link flags 
LDFLAGS+=-lpthread -lm -lelf

ifdef DEBUG
	CFLAGS?=-pipe -ggdb3 $(WARN)
else
	CFLAGS?=-pipe $(OPTI) $(WARN)
	LDFLAGS+=-s
endif

CFLAGS+=$(GLIB_FLAGS) -std=c11 -D_GNU_SOURCE -Wall -Wextra

SOURCES= \
	$(SOURCEDIR)/md5.c \
	$(SOURCEDIR)/useridcheck.c \
	$(SOURCEDIR)/filter.c \
	$(SOURCEDIR)/mode.c \
	$(SOURCEDIR)/list.c \
	$(SOURCEDIR)/rmlint.c \
	$(SOURCEDIR)/linttests.c \
	$(SOURCEDIR)/traverse.c \
	$(SOURCEDIR)/main.c


OBJECTS=$(SOURCES:.c=.o)
INCLUDE=
EXECUTABLE=rmlint 

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE):  $(OBJECTS)
		 @$(CC) $(OBJECTS) -o $@ $(LDFLAGS) 
ifdef DEBUG
	     @$(ECHO) "=> Building debug target."
else
	     @$(STRIP) $(EXECUTABLE)
	     @$(ECHO) "=> Stripping program done."
endif
	     @$(ECHO) "=> Linking program done."  

.c.o: 	
	@$(ECHO) "-> Compiling $<"
#	@$(CC) $(INCLUDE) $(CFLAGS) -D_FILE_OFFSET_BITS=64 -c $< -o $@
	@$(CC) $(INCLUDE) $(CFLAGS) -c $< -o $@

.PHONY : clean
clean:
	@$(ECHO) "<> Making clean."
	@$(RM) $(SOURCEDIR)/*.o $(EXECUTABLE) 2> /dev/null

.PHONY : install
install: rmlint
	@$(MKDIR) $(INSTALLPATH)
	@$(ECHO) "++ Copying executable to $(INSTALLPATH)"
	@$(CP) $(EXECUTABLE) $(INSTALLPATH)
	@$(MKDIR) $(MANDIR)
	@$(ECHO) "++ Zipping manpage"
	@$(GZIP) "$(DOCDIR)/$(MANDOC)" > "$(DOCDIR)/$(MANDOC_COMPRESSED)"
	@$(ECHO) "++ Copying manpage to $(MANDIR)."
	@$(CP) "$(DOCDIR)/$(MANDOC_COMPRESSED)" $(MANDIR)

.PHONY : uninstall
uninstall:
	@$(ECHO) "-- Removing the lint of rmlint" 
	$(RM) "$(INSTALLPATH)/$(EXECUTABLE)"
	$(RM) "$(MANDIR)/$(MANDOC)"
