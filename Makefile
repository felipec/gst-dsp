CROSS_COMPILE ?= arm-linux-
CC := $(CROSS_COMPILE)gcc

CFLAGS := -O2 -ggdb -Wall -Wextra -Wno-unused-parameter -ansi -std=c99
LDFLAGS := -Wl,--no-undefined -Wl,--as-needed

override CFLAGS += -D_GNU_SOURCE -DGST_DISABLE_DEPRECATED

GST_CFLAGS := $(shell pkg-config --cflags gstreamer-0.10)
GST_LIBS := $(shell pkg-config --libs gstreamer-0.10)

DSP_API := 1
SN_API := 1

override CFLAGS += -DDSP_API=$(DSP_API) -DSN_API=$(SN_API)

all:

version := $(shell ./get-version)
dspdir := /lib/dsp
prefix := /usr

D = $(DESTDIR)

# plugin

gst_plugin := libgstdsp.so

$(gst_plugin): plugin.o gstdspdummy.o gstdspbase.o gstdspvdec.o \
	gstdspvenc.o gstdsph263enc.o gstdspmp4venc.o gstdspjpegenc.o \
	dsp_bridge.o util.o log.o gstdspparse.o async_queue.o gstdsph264enc.o
$(gst_plugin): override CFLAGS += $(GST_CFLAGS) -fPIC \
	-D VERSION='"$(version)"' -D DSPDIR='"$(dspdir)"'
$(gst_plugin): override LIBS += $(GST_LIBS)

targets += $(gst_plugin)

all: $(targets)

# pretty print
ifndef V
QUIET_CC    = @echo '   CC         '$@;
QUIET_LINK  = @echo '   LINK       '$@;
QUIET_CLEAN = @echo '   CLEAN      '$@;
endif

install: $(targets)
	install -m 755 -D libgstdsp.so $(D)$(prefix)/lib/gstreamer-0.10/libgstdsp.so

%.o:: %.c
	$(QUIET_CC)$(CC) $(CFLAGS) -MMD -o $@ -c $<

%.so::
	$(QUIET_LINK)$(CC) $(LDFLAGS) -shared -o $@ $^ $(LIBS)

clean:
	$(QUIET_CLEAN)$(RM) -v $(targets) *.o *.d

dist: base := gst-dsp-$(version)
dist:
	git archive --format=tar --prefix=$(base)/ HEAD > /tmp/$(base).tar
	mkdir -p $(base)
	echo $(version) > $(base)/.version
	chmod 664 $(base)/.version
	tar --append -f /tmp/$(base).tar --owner root --group root $(base)/.version
	rm -r $(base)
	gzip /tmp/$(base).tar

-include *.d
