CROSS_COMPILE ?= arm-linux-
CC := $(CROSS_COMPILE)gcc

CFLAGS := -O2 -ggdb -Wall -Wextra -Wno-unused-parameter -Wmissing-prototypes -ansi
LDFLAGS := -Wl,--no-undefined -Wl,--as-needed

override CFLAGS += -std=c99 -D_GNU_SOURCE -DGST_DISABLE_DEPRECATED

GST_CFLAGS := $(shell pkg-config --cflags gstreamer-0.10)
GST_LIBS := $(shell pkg-config --libs gstreamer-0.10)

DSP_API := 2
SN_API := 2

override CFLAGS += -DDSP_API=$(DSP_API) -DSN_API=$(SN_API)

all:

version := $(shell ./get-version)
dspdir := /lib/dsp
prefix := /usr

D = $(DESTDIR)

tidsp.a: tidsp/td_mp4vdec.o tidsp/td_h264dec.o tidsp/td_wmvdec.o \
	tidsp/td_jpegdec.o \
	tidsp/td_mp4venc.o tidsp/td_jpegenc.o tidsp/td_h264enc.o \
	tidsp/td_vpp.o tidsp/td_aacdec.o
tidsp.a: override CFLAGS += -fPIC -I.

# plugin

gst_plugin := libgstdsp.so

$(gst_plugin): plugin.o gstdspbuffer.o gstdspdummy.o gstdspbase.o gstdspvdec.o \
	gstdspvenc.o gstdsph263enc.o gstdspmp4venc.o gstdspjpegenc.o \
	dsp_bridge.o util.o log.o gstdspparse.o async_queue.o gstdsph264enc.o \
	gstdspvpp.o gstdspadec.o gstdspipp.o \
	tidsp.a
$(gst_plugin): override CFLAGS += $(GST_CFLAGS) -fPIC \
	-D VERSION='"$(version)"' -D DSPDIR='"$(dspdir)"'
$(gst_plugin): override LIBS += $(GST_LIBS)

targets += $(gst_plugin)

doc: $(gst_plugin)
	$(MAKE) -C doc

doc-install: doc
	$(MAKE) -C doc install

all: $(targets)

# pretty print
ifndef V
QUIET_CC    = @echo '   CC         '$@;
QUIET_LINK  = @echo '   LINK       '$@;
QUIET_CLEAN = @echo '   CLEAN      '$@;
endif

.PHONY: doc doc-install

install: $(targets)
	install -m 755 -D libgstdsp.so $(D)$(prefix)/lib/gstreamer-0.10/libgstdsp.so

%.o:: %.c
	$(QUIET_CC)$(CC) $(CFLAGS) -MMD -o $@ -c $<

%.so::
	$(QUIET_LINK)$(CC) $(LDFLAGS) -shared -o $@ $^ $(LIBS)

%.a::
	$(QUIET_LINK)$(AR) rcs $@ $^

clean:
	$(QUIET_CLEAN)$(RM) -v $(targets) *.o *.d tidsp/*.d tidsp/*.o

dist: base := gst-dsp-$(version)
dist:
	git archive --format=tar --prefix=$(base)/ HEAD > /tmp/$(base).tar
	mkdir -p $(base)
	echo $(version) > $(base)/.version
	chmod 664 $(base)/.version
	tar --append -f /tmp/$(base).tar --owner root --group root $(base)/.version
	rm -r $(base)
	gzip /tmp/$(base).tar

-include *.d tidsp/*.d
