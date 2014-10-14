#source dir
SOURCEDIR=src
DOCDIR=doc

#install
PREFIX=/usr
INSTALLPATH=$(DESTDIR)$(PREFIX)
UNAME := $(shell sh -c 'uname -s 2>/dev/null || echo not')

#BINDIR
BINDIR=$(INSTALLPATH)/bin
#MANDIR
MANDIR=$(INSTALLPATH)/share/man/man1
#MANUAL
MANDOC=rmlint.1
MANDOC_COMPRESSED=rmlint.1.gz

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


MKDIR=mkdir -p
GZIP=gzip -c -f

#compiling stuff
CC?=gcc

#Heavy Warnlevel 
WARN=-Wall -pedantic

#Link flags 
LDFLAGS+=-lpthread -lm

ifdef DEBUG
	CFLAGS?=-pipe -ggdb3 $(WARN)
else
	CFLAGS?=-pipe $(OPTI) $(WARN)
	LDFLAGS+=-s
endif


SOURCES= \
	$(SOURCEDIR)/md5.c \
	$(SOURCEDIR)/useridcheck.c \
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
	@$(CC) $(INCLUDE) $(CFLAGS) -D_FILE_OFFSET_BITS=64 -c $< -o $@

.PHONY : clean
clean:
	@$(ECHO) "<> Making clean."
	@$(RM) $(SOURCEDIR)/*.o $(EXECUTABLE) 2> /dev/null

.PHONY : install
install: rmlint
	@$(MKDIR) $(BINDIR)
	@$(ECHO) "++ Copying executable to $(BINDIR)"
	@$(CP) $(EXECUTABLE) $(BINDIR)
	@$(MKDIR) $(MANDIR)
	@$(ECHO) "++ Zipping manpage"
	@$(GZIP) "$(DOCDIR)/$(MANDOC)" > "$(DOCDIR)/$(MANDOC_COMPRESSED)"
	@$(ECHO) "++ Copying manpage to $(MANDIR)."
	@$(CP) "$(DOCDIR)/$(MANDOC_COMPRESSED)" $(MANDIR)

.PHONY : uninstall
uninstall:
	@$(ECHO) "-- Removing the lint of rmlint" 
	$(RM) "$(BINDIR)/$(EXECUTABLE)"
	$(RM) "$(MANDIR)/$(MANDOC)"
