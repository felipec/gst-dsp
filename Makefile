CC := gcc
CFLAGS := -O2 -Wall -Werror -ansi -std=c99 -D_XOPEN_SOURCE=600

GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS := $(shell pkg-config --libs glib-2.0)

GST_CFLAGS := $(shell pkg-config --cflags gstreamer-0.10 gstreamer-base-0.10)
GST_LIBS := $(shell pkg-config --libs gstreamer-0.10 gstreamer-base-0.10)

all:

version := $(shell ./get-version.sh)

# plugin

gst_plugin := libgstdsp.so

$(gst_plugin): plugin.o gstdspdummy.o dsp_bridge.o
$(gst_plugin): CFLAGS := $(CFLAGS) $(UTIL_CFLAGS) $(GST_CFLAGS) -D VERSION='"$(version)"'
$(gst_plugin): LIBS := $(UTIL_LIBS) $(GST_LIBS)

targets += $(gst_plugin)

all: $(targets)

# pretty print
V = @
Q = $(V:y=)
QUIET_CC    = $(Q:@=@echo '   CC         '$@;)
QUIET_LINK  = $(Q:@=@echo '   LINK       '$@;)
QUIET_AR    = $(Q:@=@echo '   AR         '$@;)
QUIET_CLEAN = $(Q:@=@echo '   CLEAN      '$@;)
QUIET_DLL   = $(Q:@=@echo '   DLLCREATE  '$@;)

%.a::
	$(QUIET_AR)$(AR) rcs $@ $^

%.o:: %.c
	$(QUIET_CC)$(CC) $(CFLAGS) -MMD -o $@ -c $<

%.so::
	$(QUIET_LINK)$(CC) $(LDFLAGS) -shared -o $@ $^ $(LIBS)

clean:
	$(QUIET_CLEAN)$(RM) -v $(targets) *.o *.d

-include *.d
