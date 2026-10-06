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
MULTIGEN = m68k-palmos-multigen

CFLAGS   = -O2 -Wall -Wno-multichar -Wno-unused -palmos5r3 -Isrc
LDFLAGS  = -palmos5r3
LIBS     = -lPalmOSGlue

SRCS = src/main.c src/mainform.c src/detail.c src/compose.c src/viewer.c \
       src/prefs.c src/timeline.c src/render.c src/image.c src/net.c \
       src/proto.c src/util.c src/account.c src/search.c
OBJS = $(SRCS:src/%.c=build/%.o)

all: $(APP).prc

build:
	mkdir -p build build/rsc

build/%.o: src/%.c src/PalmFedi.h src/PalmFediRsc.h | build
	$(CC) $(CFLAGS) -c $< -o $@

# Two code segments (see PalmFedi.def): multigen writes the section stubs
# and the linker script.
build/$(APP)-sections.s build/$(APP)-sections.ld: $(APP).def | build
	$(MULTIGEN) -b build/$(APP)-sections $(APP).def

build/$(APP)-sections.o: build/$(APP)-sections.s
	$(CC) -c $< -o $@

build/$(APP): $(OBJS) build/$(APP)-sections.o build/$(APP)-sections.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) build/$(APP)-sections.o build/$(APP)-sections.ld $(LIBS)

build/rsc/stamp: rsc/PalmFedi.rcp src/PalmFediRsc.h $(wildcard rsc/*.bmp) | build
	rm -f build/rsc/*.bin
	cd rsc && $(PILRC) -q -I ../src PalmFedi.rcp ../build/rsc
	touch $@

$(APP).prc: build/$(APP) build/rsc/stamp
	$(BUILDPRC) -o $@ $(APP).def build/$(APP) build/rsc/*.bin

clean:
	rm -rf build $(APP).prc

.PHONY: all clean
