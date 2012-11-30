.PHONY: all clean

LIBROPE = ../librope
LIBOT = ../libot
LIBUV = ../libuv

CFLAGS = -O2 -emit-llvm -Wall -arch x86_64 -I. -I$(LIBROPE) -I$(LIBOT) -I$(LIBUV)/include

UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
LIBS = -framework CoreFoundation -framework CoreServices
else
LIBS =
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

# You may have to compile uv using CFLAGS=-emit-llvm
sharedb: $(LIBUV)/uv.a $(LIBOT)/libot.a $(SOURCES:%.c=%.o)
	$(CC) $(CFLAGS) $(LIBS) $+ -o $@

# Only need corefoundation to run the tests on mac
test: libot.a test.c 
	$(CC) $(CFLAGS) $+ -o $@

