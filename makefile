# Makefile for Zip, ZipNote, ZipCloak and ZipSplit

# what you can make ...
all:
	@echo ''
	@echo 'Make what?  You must say what system to make Zip for--e.g.'
	@echo '"make bsd".  Choices: generic, 386bsd, 3b1, aix, att6300, aux,'
	@echo 'bsd, bsdold, bull, convex, coherent, cray, cray_v3, dec_osf1,'
	@echo 'dnix, dynix, hpux, isc, linux, minix, next10, next2x, next3x,'
	@echo 'nextfat, pixel, ptx, rs6000, scodos, sgi, sun, sun_gcc, sysv,'
	@echo 'sysv_gcc, sysv_386, sys_386_gcc, sysv_old, ultrix, v7, xenix,'
	@echo 'xos, zilog.'
	@echo 'See the files install.doc and zip.doc for more information.'
	@echo ''

list:   all

MAKE = make
SHELL = /bin/sh

# (to use the Gnu compiler, change cc to gcc in CC and BIND)
CC = cc
BIND = $(CC)
E =
CPP = /lib/cpp -DSYSV

# probably can change this to 'install' if you have it
INSTALL = cp

# target directories - where to install executables and man pages to
BINDIR = /usr/local/bin
MANDIR = /usr/man/manl

# flags
#   CFLAGS    flags for C compile
#   LFLAGS1   flags after output file spec, before obj file list
#   LFLAGS2   flags after obj file list (libraries, etc)
CFLAGS = -O
LFLAGS1 =
LFLAGS2 = -s

# object file lists
OBJZ = zip.o zipfile.o zipup.o fileio.o util.o globals.o crypt.o

OBJI = deflate.o trees.o bits.o
OBJA =
OBJU = zipfile_.o zipup_.o fileio_.o util_.o globals.o
OBJN = zipnote.o  $(OBJU)
OBJC = zipcloak.o $(OBJU) crypt_.o
OBJS = zipsplit.o $(OBJU)

# suffix rules
.SUFFIXES:
.SUFFIXES: _.o .o .c .doc .1
.c_.o:
	rm -f $*_.c; ln $< $*_.c
	$(CC) $(CFLAGS) -DUTIL -c $*_.c
	rm -f $*_.c
.c.o:
	$(CC) $(CFLAGS) -c $<

.1.doc:
	nroff -man $< | col -b | uniq > $@

# rules for zip, zipnote, zipcloak, zipsplit, and zip.doc.
$(OBJZ): zip.h ziperr.h tailor.h
$(OBJI): zip.h ziperr.h tailor.h
$(OBJN): zip.h ziperr.h tailor.h
$(OBJS): zip.h ziperr.h tailor.h
$(OBJC): zip.h ziperr.h tailor.h
zip.o zipup.o crypt.o bits.o zipup_.o zipcloak.o crypt_.o:  crypt.h

match.o: match.s
	$(CPP) match.s > _match.s
	$(CC) -c _match.s
	mv _match.o match.o
	rm -f _match.s

ZIPS = zip$E zipnote$E zipsplit$E zipcloak$E

zip.o zipup.o zipnote.o zipcloak.o zipsplit.o: revision.h
zips: $(ZIPS)
zipsman: zip zipnote zipsplit zipcloak zip.doc

zip$E: $(OBJZ) $(OBJI) $(OBJA)
	$(BIND) -o zip$E $(LFLAGS1) $(OBJZ) $(OBJI) $(OBJA) $(LFLAGS2)
zipnote$E: $(OBJN)
	$(BIND) -o zipnote$E $(LFLAGS1) $(OBJN) $(LFLAGS2)
zipcloak$E: $(OBJC)
	$(BIND) -o zipcloak$E $(LFLAGS1) $(OBJC) $(LFLAGS2)
zipsplit$E: $(OBJS)
	$(BIND) -o zipsplit$E $(LFLAGS1) $(OBJS) $(LFLAGS2)

# install
install:        $(ZIPS)
	$(INSTALL) $(ZIPS) $(BINDIR)
	$(INSTALL) zip.1 $(MANDIR)/zip.l

flags:  configure
	sh configure flags

# These symbols, when #defined using -D have these effects on compilation:
# ZMEM          - includes C language versions of memset(), memcpy(), and
#                 memcmp() (util.c).
# SYSV          - use <sys/dirent.h> and the library opendir()
# DIRENT        - use <sys/dirent.h> and getdents() instead of <sys/dir.h>
#                 and opendir(), etc. (fileio.c).
# NODIR         - used for 3B1, which has neither getdents() nor opendir().
# NDIR          - use "ndir.h" instead of <sys/dir.h> (fileio.c).
# UTIL          - select routines for utilities (note, cloak, and split).
# PROTO         - enable function prototypes.
# RMDIR         - remove directories using a system("rmdir ...") call.
# CONVEX        - for Convex make target.
# AIX           - for AIX make target.
# LINUX         - for linux make target.

#               Generic BSD and SysV targets:

generic: flags
	eval $(MAKE) zips `cat flags`

# BSD 4.3 (also Unisys 7000--AT&T System V with heavy BSD 4.2)
bsd:
	$(MAKE) zips CFLAGS="-O"

# BSD, but missing memset(), memcmp().
bsdold:
	$(MAKE) zips CFLAGS="-O -DZMEM"

# AT&T System V, Rel 3.  Also SCO Unix, OpenDeskTop, ETA-10P*, SGI.
sysv_old:
	$(MAKE) zips CFLAGS="-O -DDIRENT"

# AT&T System V, Rel 4. Also any system with readdir() and termio.
svr4: sysv
sysv:
	$(MAKE) zips CFLAGS="-O -DSYSV"

sysv_gcc:
	$(MAKE) zips CFLAGS="-O2 -DSYSV" CC=gcc

# AT&T System V, Rel 4 for 386 (uses asm version):
sysv_386:
	$(MAKE) zips CFLAGS="-O -DSYSV -DASMV" OBJA=match.o

sysv_386_gcc:
	$(MAKE) zips CFLAGS="-O2 -DSYSV -DASMV" OBJA=match.o CC=gcc


#            Specific targets in alphabetical order:

# 386BSD 0.1
386bsd:
	$(MAKE) zips CFLAGS="-O -DASMV" CPP=/usr/bin/cpp OBJA=match.o

# AT&T 3B1: System V, but missing a few things.
3b1:
	$(MAKE) zips CFLAGS="-O -DNODIR -DRMDIR"

# AIX Version 3.1 for RISC System/6000 
rs6000: aix
aix:
	$(MAKE) zips CC="c89" BIND="c89" \
	   CFLAGS="-O -D_POSIX_SOURCE -D_ALL_SOURCE -D_BSD -DAIX"

# AT&T 6300 PLUS (don't know yet how to allocate 64K bytes):
att6300:
	$(MAKE) zips LFLAGS1="-Ml" \
	CFLAGS="-O -Ml -DNODIR -DRMDIR -DDYN_ALLOC -DMEDIUM_MEM -DWSIZE=16384"

# A/UX:
aux:
	$(MAKE) zips CFLAGS="-O -DTERMIO"

# Bull DPX/2 - BOS 02.00.69, aka "set 6"
bull:
	$(MAKE) zips CFLAGS="-O -v -DSYSV"

# Coherent
coherent:
	$(MAKE) zips CFLAGS="-O -DDIRENT"

# Convex C-120, C-210, OS 9.0, cc v. 4.0, no vectorization used.
# Do not use -O2, there is a compiler bug.
convex:
	$(MAKE) zips CFLAGS="-O"

# Cray Unicos 5.1.10 & 6.0.11, Standard C compiler 2.0
cray:
	$(MAKE) zips CFLAGS="-O -DDIRENT" CC="scc"

# Cray Unicos 6.1, Standard C compiler 3.0 (all routines except trees.c
# may be compiled with vector3; internal compiler bug in 3.0.2.3 and
# earlier requires vector2 for trees.c)
cray_v3:
	$(MAKE) zips CFLAGS="-O -h vector2 -h scalar3 -DDIRENT" CC="scc"

# DEC OSF/1
dec_osf1:
	$(MAKE) zips CFLAGS="-O -Olimit 1000 -DOSF -D_BSD"

# DNIX 5.x: like System V but optimization is messed up.
# There is a bug in cc for the dnix 5.3 2.2 on the 68030 but this
# bug is not pesent dnix 5.3 1.4.3 on 68010. (To be investigated.)
dnix:
	$(MAKE) zips CFLAGS="-DDIRENT"

# DYNIX (R) V3.0.18 (no memset() or memcmp(), rindex() instead of strrchr())
# See also ptx entry below.
dynix:
	$(MAKE) zips CFLAGS="-O -DZMEM -Dstrrchr=rindex"

# HPUX: System V, but use <ndir.h> and opendir(), etc.
hp:     hpux
hpux:
	$(MAKE) zips CFLAGS="-O -DNDIR"

# Interactive Systems Corporation System V/386, Rel 3.2--optimizer problems
isc:
	$(MAKE) zips CFLAGS="-DDIRENT"

# Linux 0.97 with GCC 2.2.2, dies with GCC <= 2.11c. builtin functions are
# disabled because '#define const' removes const from normal functions
# but not builtin ones. And keeping const causes problems on other systems.
linux:
	$(MAKE) zips CFLAGS="-O -fno-builtin -DSYSV -DTERMIO -DLINUX" \
	  CC=gcc BIND=gcc

# MINIX 1.5.10 with Bruce Evans 386 patches and gcc/GNU make
minix:
	$(MAKE) zips CFLAGS="-O -DDIRENT -DMINIX -DNO_TERMIO" CC=gcc
	chmem =262144 zip

# NeXT info.
next:
	@echo
	@echo\
 '  Please pick a specific NeXT target:  "make next10" will create a generic'
	@echo\
 '  NeXT executable; "make next2x" will create a smaller executable (for'
	@echo\
 '  NeXTstep 2.0 and higher); "make next3x" will create a small executable'
	@echo\
 '  with significantly better optimization (NeXTstep 3.0 and higher only).'
	@echo\
 '  "make nextfat" will create a fat executable (NeXTstep 3.1 only).'
	@echo

# NeXT 1.0: BSD, but use shared library.
next10:
	$(MAKE) zips CFLAGS="-O" LFLAGS2="-s -lsys_s"

# NeXT 2.x: BSD, but use MH_OBJECT format for smaller executables.
next2x:
	$(MAKE) zips CFLAGS="-O" LFLAGS2="-s -object"

# NeXT 3.x: like above, but better optimization.
next3x:
	$(MAKE) zips CFLAGS="-O2" LFLAGS2="-s -object"

# NeXT 3.1: like above, but make executables "fat".
nextfat:
	$(MAKE) zips CFLAGS="-O2 -arch i386 -arch m68k" \
	  LFLAGS2="-arch i386 -arch m68k -s -object"


# Pixel Computer 80 or 100.
# Old V7 BSD, missing memset(), memcmp(), getdents(), opendir()
pixel:  v7
v7:
	$(MAKE) zips CFLAGS="-O -DNODIR -DRMDIR -DZMEM -Dstrrchr=rindex"

# Dynix/ptx 1.3; needs libseq for readlink()
ptx:
	$(MAKE) zips CFLAGS="-O -DSYSV -DTERMIO" LFLAGS2="-lseq"

# SCO 386 cross compile for MS-DOS
# Note: zip.exe should be lzexe'd on DOS to reduce its size
scodos:
	$(MAKE) zips CFLAGS="-O -Mc -dos -DNO_ASM" LFLAGS1="-Mc -dos" \
	 LFLAGS2="-F 1000" E=".exe"

# SCO Xenix for 286. Warning: this is untested.
sco_x286:
	$(MAKE) zips LFLAGS1="-Ml2" CFLAGS="-O -Ml2 -DRMDIR"

# Silicon Graphics Indigo with IRIX 4.0.5F
sgi:
	$(MAKE) zips CFLAGS="-O2 -DSYSV"

# Sun OS 4.x: BSD, but use getdents(). If you have gcc, use 'make sun_gcc'
# instead since the code produced is better.
sun_bsd:
	$(MAKE) zips CFLAGS="-O2 -DDIRENT"

# Sun OS 4.x and Solaris. If you have gcc, use 'make sun_gcc'
# or (better) 'make mmap_gcc' instead since the code produced is better.
sun:
	$(MAKE) zips CFLAGS="-O2 -DSYSV"

# Sun OS 4.x with gcc (bug with -s linker flag). Use -O if your version
# of gcc does not like -O2.
sun_gcc:
	$(MAKE) zips CFLAGS="-O2 -DSYSV" CC=gcc LFLAGS2=""
	strip $(ZIPS)

# Ultrix
ultrix:
	$(MAKE) zips CFLAGS="-O -Olimit 1000"

# SCO Xenix
xenix:
	$(MAKE) zips CFLAGS="-O -DSYSV" LFLAGS2="-lx -s"

# xos: Olivetti LSX-3005..3045 with X/OS 2.3 or 2.4
xos:
	$(MAKE) zips CFLAGS="-O -DTERMIO"

# zilog zeus 3.21
zilog:
	$(MAKE) zips CFLAGS="-O -DZMEM -DNDIR -DRMDIR" CC="scc -i" BIND="scc"


# clean up after making stuff and installing it
clean:
	rm -f *.o $(ZIPS) flags

# This one's for Mark:
it:
	$(MAKE) zipsman CFLAGS="-O -Wall -DPROTO"\
	LFLAGS2="-s -object" VPATH="${HOME}/Unix/bin"

# and these are for Jean-loup:
gcc_d:
	$(MAKE) zip CFLAGS="-g -DDEBUG -DMMAP -DSYSV" CC=gcc LFLAGS2="-g"
	mv zip zipd

old_gcc:
	$(MAKE) zips CFLAGS="-O -fstrength-reduce -DSYSV" CC=gcc LFLAGS2=""
	strip $(ZIPS)

big_gcc:
	$(MAKE) zips CFLAGS="-O2 -DSYSV -DBIG_MEM -W -Wall" CC=gcc LFLAGS2=""
	strip $(ZIPS)

mmap_gcc:
	$(MAKE) zips CFLAGS="-O2 -DSYSV -DMMAP -W -Wall" CC=gcc LFLAGS2=""
	strip $(ZIPS)

# end of Makefile
