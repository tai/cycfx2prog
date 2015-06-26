CC = gcc -pipe
VERSION = 0.47
CFLAGS = -O2 -fno-rtti -fno-exceptions -DCYCFX2PROG_VERSION=\"$(VERSION)\" \
	-W -Wall -Wformat
LDFLAGS = -lusb
DIST_DEST = cycfx2prog-$(VERSION)

all: cycfx2prog

# NOTE: Also add sources to the "dist:" target!
cycfx2prog: cycfx2prog.o cycfx2dev.o
	$(CC) $(LDFLAGS) cycfx2prog.o cycfx2dev.o -o cycfx2prog

clean:
	-rm -f *.o

distclean: clean
	-rm -f cycfx2prog

dist:
	mkdir -p "$(DIST_DEST)"
	cp Makefile "$(DIST_DEST)"
	cp cycfx2dev.cc cycfx2dev.h "$(DIST_DEST)"
	cp cycfx2prog.cc "$(DIST_DEST)"
	tar -c "$(DIST_DEST)" | gzip -9 > "cycfx2prog-$(VERSION).tar.gz"
	rm -r "$(DIST_DEST)"

.cc.o:
	$(CC) -c $(CFLAGS) $<

cycfx2dev.o: cycfx2dev.cc cycfx2dev.h
cycfx2prog.o: cycfx2prog.cc cycfx2dev.h
