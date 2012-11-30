.PHONY: all clean

LIBROPE = ../librope
LIBOT = ../libot
LIBUV = ../libuv

CFLAGS = -O2 -Wall -fblocks -I. -I$(LIBROPE) -I$(LIBOT) -I$(LIBUV)/include

UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
LIBS := -framework CoreFoundation -framework CoreServices
CFLAGS := $(CFLAGS) -emit-llvm -arch x86_64
else
# Blocks support on ubuntu is in libblocksruntime-dev
LIBS := -lBlocksRuntime -lpthread -lrt
endif

SOURCES=$(wildcard src/*.c)

all: sharedb

clean:
	rm -f sharedb src/*.o

foo:
	echo $(SOURCES)

# We don't need to build librope because libot contains it.
$(LIBOT)/libot.a:
	$(MAKE) -C $(LIBOT) libot.a

$(LIBUV)/libuv.a:
	$(MAKE) -C $(LIBUV) libuv.a

# You may have to compile uv using CFLAGS=-emit-llvm
sharedb: $(SOURCES:%.c=%.o) $(LIBUV)/libuv.a $(LIBOT)/libot.a
	$(CC) $(CFLAGS) $+ -o $@ $(LIBS)

# Only need corefoundation to run the tests on mac
test: libot.a test.c 
	$(CC) $(CFLAGS) $+ -o $@

