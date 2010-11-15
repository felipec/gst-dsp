m := gst-dsp
common := /usr/share/gst-common

PYTHON := python

GST_CFLAGS := $(shell pkg-config --cflags gstreamer-0.10)
GST_LDFLAGS := $(shell pkg-config --libs gstreamer-0.10)

IGNORE_HFILES := log.h sem.h util.h dmm_buffer.h dsp_bridge.h async_queue.h gstdspparse.h \
	tidsp/td_hdcodec.h tidsp/td_h264dec_common.h tidsp/td_mp4venc_common.h

all: html

$(m)-decl.txt: $(m).types
	gtkdoc-scan --source-dir=.. --module=$(m) --ignore-headers="$(IGNORE_HFILES)"

tmpl: $(m)-decl.txt
	gtkdoc-mktmpl --module=$(m) --output-dir=$@

inspect: ../libgstdsp.so
	mkdir -p $@
	CFLAGS="$(GST_CFLAGS)" LDFLAGS="$(GST_LDFLAGS)" \
	GST_PLUGIN_PATH=.. GST_REGISTRY=inspect-registry.bin \
	$(common)/gstdoc-scangobj \
		--module=$(m) --source=$(m) --inspect-dir=inspect \
		--type-init-func="gst_init(NULL,NULL)"
	$(common)/scangobj-merge.py $(m)

mangle: tmpl inspect
	$(PYTHON) $(common)/mangle-tmpl.py inspect tmpl

mangle.stamp: mangle
	touch $@

.PHONY: mangle

xml: mangle.stamp
	mkdir -p $@
	for x in inspect/*.xml; do \
	    xsltproc --stringparam module $(m) \
		$(common)/plugins.xsl $$x > $@/`basename $$x`; done
	gtkdoc-mkdb \
		--module=$(m) \
		--source-dir=.. \
		--main-sgml-file=$(m)-docs.sgml \
		--output-format=xml \
		--sgml-mode --source-suffixes=c,h,cc

html: xml
	$(RM) -r $@
	mkdir -p $@
	cd $@ && gtkdoc-mkhtml --path=.. $(m) ../$(m)-docs.sgml
	gtkdoc-fixxref --module=$(m) --module-dir=$@

prefix := /usr

D = $(DESTDIR)

install: html
	mkdir -p $(D)$(prefix)/share/gtk-doc/html/$(m)
	cp -av html/* $(D)$(prefix)/share/gtk-doc/html/$(m)/
