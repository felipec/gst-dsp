CC := gcc
CFLAGS := -O2 -ggdb -Wall -Wextra -Wno-unused-parameter -ansi -std=c99

override CFLAGS += -D_GNU_SOURCE

GST_CFLAGS := $(shell pkg-config --cflags gstreamer-0.10 gstreamer-base-0.10)
GST_LIBS := $(shell pkg-config --libs gstreamer-0.10 gstreamer-base-0.10)

all:

version := $(shell ./get-version.sh)
dspdir := /lib/dsp

# plugin

gst_plugin := libgstdsp.so

$(gst_plugin): plugin.o gstdspdummy.o gstdspbase.o gstdspvdec.o \
	gstdspvenc.o gstdsph263enc.o gstdspmp4venc.o gstdspjpegenc.o \
	dsp_bridge.o util.o log.o gstdspparse.o async_queue.o
$(gst_plugin): CFLAGS := $(CFLAGS) $(UTIL_CFLAGS) $(GST_CFLAGS) -D VERSION='"$(version)"' -D DSPDIR='"$(dspdir)"'
$(gst_plugin): LIBS := $(UTIL_LIBS) $(GST_LIBS)

targets += $(gst_plugin)

all: $(targets)

# pretty print
V = @
Q = $(V:y=)
QUIET_CC    = $(Q:@=@echo '   CC         '$@;)
QUIET_LINK  = $(Q:@=@echo '   LINK       '$@;)
QUIET_CLEAN = $(Q:@=@echo '   CLEAN      '$@;)

%.o:: %.c
	$(QUIET_CC)$(CC) $(CFLAGS) -MMD -o $@ -c $<

%.so::
	$(QUIET_LINK)$(CC) $(LDFLAGS) -shared -Wl,--no-undefined -o $@ $^ $(LIBS)

clean:
	$(QUIET_CLEAN)$(RM) -v $(targets) *.o *.d

-include *.d
