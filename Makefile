#-------------------------------------------------------------------------------
# Project Specific Parameters
#-------------------------------------------------------------------------------

include Make.defines

#-------------------------------------------------------------------------------
# Directories
#-------------------------------------------------------------------------------

PREFIX      = /usr/local
DESTDIR     = 
BINDIR      = $(DESTDIR)/$(PREFIX)/bin
INCDIR      = $(DESTDIR)/$(PREFIX)/include
LIBDIR      = $(DESTDIR)/$(PREFIX)/lib
INFODIR     = $(DESTDIR)/$(PREFIX)/info
MANDIR      = $(DESTDIR)/$(PREFIX)/man
TOP_SRCDIR  = .
SRCDIR      = .

include Make.linux

#-------------------------------------------------------------------------------
# Compile Options
#-------------------------------------------------------------------------------

CPP         = g++
CC          = gcc
CFLAGS      = $(ADDLCFLAGS) $(INCPATHS) \
              $(PROFILING) $(MEM_TRACING) $(DEBUGGING) $(TRACING)
CPPFLAGS    =  $(ADDCPPFLAGS)
LDFLAGS     =   $(ADDLDFLAGS)
LIBS        =      $(ADDLIBS)
INSTALL     = /usr/bin/install -c

#-------------------------------------------------------------------------------
# Targets
#-------------------------------------------------------------------------------

LIBVER      = 0.0.1
A_LIB       = lib$(NAME).a
S_LIB       = lib$(NAME).$(DSO_EXTENSION)
VS_LIB      = $(S_LIB).$(LIBVER)
PROGS       = all
FILES       = 
LIBFILES    = lib.o example.o fs.o common.o
HDR         =
CLEANFILES  = gmon.out prof.txt *core		      \
	          *.o *~ *.$(DSO_EXTENSION) *.a 

BUILDFILES  = config.h autoscan.log config.status \
              tmp.txt configure config.log configure.scan

#-------------------------------------------------------------------------------
# Build Programs
#-------------------------------------------------------------------------------

AR			= ar
ARFLAGS		= cru

#-------------------------------------------------------------------------------
# Build
#-------------------------------------------------------------------------------

# Build the library and test program
all:		program shared

# Build both a shared and static library
lib:		shared static

program:	$(LIBFILES) main.o sqlite3.o
	$(CC) $(LINKFLAGS) -o program main.o sqlite3.o $(LDFLAGS) $(LIBS) -ldl

# Share library
shared:	$(LIBFILES)
	$(CC) -shared $(LINKFLAGS) -o $(S_LIB) $(LIBFILES) $(LDFLAGS) $(LIBS)

clean:
	rm -f ${PROGS} ${CLEANFILES} program

distclean: clean
	rm -f ${BUILDFILES}

.cpp.o:
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $<

.c.o:
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $<

#-------------------------------------------------------------------------------
# Autoconf
#-------------------------------------------------------------------------------

# automatic re-running of configure if the ocnfigure.in file has changed
${SRCDIR}/configure: configure.in
	cd ${SRCDIR} && autoconf

# autoheader might not change config.h.in, so touch a stamp file
${SRCDIR}/config.h.in: stamp-h.in
${SRCDIR}/stamp-h.in: configure.in aclocal.m4
		cd ${SRCDIR} && autoheader
		echo timestamp > ${SRCDIR}/stamp-h.in

config.h: stamp-h

stamp-h: config.h.in config.status
	./config.status

Makefile: Makefile.in config.status
	./config.status

config.status: configure
	./config.status --recheck
