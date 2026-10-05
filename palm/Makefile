# PalmFedi - Palm OS 5 build (prc-tools-remix + PilRC + Palm OS SDK 5r3)
#
#   make            -> PalmFedi.prc
#   make clean
#
# See ../docs/BUILDING.md for how to install the toolchain.

APP      = PalmFedi
CREATOR  = PFdi
TYPE     = appl

CC       = m68k-palmos-gcc
PILRC    = pilrc
BUILDPRC = build-prc
OBJRES   = m68k-palmos-obj-res

CFLAGS   = -O2 -Wall -Wno-multichar -Wno-unused -palmos5r3 -Isrc
LDFLAGS  = -palmos5r3
LIBS     = -lPalmOSGlue

SRCS = src/main.c src/mainform.c src/detail.c src/compose.c src/viewer.c \
       src/prefs.c src/timeline.c src/render.c src/image.c src/net.c \
       src/proto.c src/util.c
OBJS = $(SRCS:src/%.c=build/%.o)

all: $(APP).prc

build:
	mkdir -p build build/rsc

build/%.o: src/%.c src/PalmFedi.h src/PalmFediRsc.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/$(APP): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

build/rsc/stamp: rsc/PalmFedi.rcp src/PalmFediRsc.h $(wildcard rsc/*.bmp) | build
	rm -f build/rsc/*.bin
	cd rsc && $(PILRC) -q -I ../src PalmFedi.rcp ../build/rsc
	touch $@

$(APP).prc: build/$(APP) build/rsc/stamp
	$(BUILDPRC) -o $@ -n "$(APP)" -c $(CREATOR) -t $(TYPE) build/$(APP) build/rsc/*.bin

clean:
	rm -rf build $(APP).prc

.PHONY: all clean
