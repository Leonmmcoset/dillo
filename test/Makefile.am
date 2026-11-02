SUBDIRS = unit dw

if ENABLE_HTML_TESTS
SUBDIRS += html
endif

if ENABLE_DILLOC_TESTS
SUBDIRS += dilloc
if ENABLE_JAVASCRIPT
SUBDIRS += javascript
endif
endif

EXTRA_DIST = \
	lib/dilloc.sh \
	lib/workdir.sh \
	lib/xorg.sh
