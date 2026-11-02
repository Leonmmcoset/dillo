AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-DCUR_SRC_DIR="\"$(srcdir)\"" \
	-DCUR_WORKING_DIR='"@BASE_CUR_WORKING_DIR@/test/unit"'

AM_CFLAGS = @FLTK_CFLAGS@
AM_CXXFLAGS = @FLTK_CXXFLAGS@

LDADD = \
	$(top_builddir)/lout/liblout.a

TESTS = \
	containers \
	cookies-script \
	css-media \
	css-variables \
	disposition \
	identity \
	i18n \
	liang \
	notsosimplevector \
	prefsrc \
	shapes \
	unicode_test

if ENABLE_JAVASCRIPT
TESTS += javascript
javascript_SOURCES = javascript_test.cc $(top_srcdir)/src/jsengine.cc
javascript_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/vendor/quickjs -I$(top_srcdir)/vendor/lexbor/source
javascript_LDADD = $(top_builddir)/vendor/libquickjs.a $(top_builddir)/vendor/liblexbor.a -lm
endif

# Some test are broken, so only build them
check_PROGRAMS = $(TESTS) \
	cookies \
	trie

EXTRA_DIST = \
	hyph-en-us.pat \
	hyph-de.pat

containers_SOURCES = containers.cc
css_media_SOURCES = css_media_test.cc $(top_srcdir)/src/cssmedia.cc
css_media_LDADD = $(top_builddir)/dlib/libDlib.a
css_variables_SOURCES = css_variables_test.cc $(top_srcdir)/src/cssvariables.cc
css_variables_LDADD =
i18n_SOURCES = i18n.c
i18n_LDADD = $(top_builddir)/dlib/libDlib.a
prefsrc_SOURCES = prefsrc_test.cc $(top_srcdir)/src/prefsrc.cc
prefsrc_LDADD = $(top_builddir)/dlib/libDlib.a
containers_LDADD = \
	$(top_builddir)/lout/liblout.a \
	$(top_builddir)/dlib/libDlib.a
disposition_SOURCES = \
	disposition.c
disposition_LDADD = \
	$(top_builddir)/dlib/libDlib.a
notsosimplevector_SOURCES = notsosimplevector.cc
identity_SOURCES = identity.cc
identity_LDADD = \
	$(top_builddir)/lout/liblout.a \
	$(top_builddir)/dlib/libDlib.a
cookies_SOURCES = cookies.c
cookies_script_SOURCES = cookies_script_test.c $(top_srcdir)/dpi/dpiutil.c
cookies_script_LDADD = $(top_builddir)/dpip/libDpip.a $(top_builddir)/dlib/libDlib.a
cookies_LDADD = \
	$(top_builddir)/dpip/libDpip.a \
	$(top_builddir)/dlib/libDlib.a
shapes_SOURCES = shapes.cc
shapes_LDADD = \
	$(top_builddir)/dw/libDw-core.a \
	$(top_builddir)/dlib/libDlib.a \
	$(top_builddir)/lout/liblout.a
unicode_test_SOURCES = unicode_test.cc
unicode_test_LDADD = \
	$(top_builddir)/lout/liblout.a \
	@FLTK_LIBS@ @LIBX11_LIBS@
liang_SOURCES = liang.cc
liang_LDADD = \
	$(top_builddir)/dw/libDw-widgets.a \
	$(top_builddir)/dw/libDw-fltk.a \
	$(top_builddir)/dw/libDw-core.a \
	$(top_builddir)/lout/liblout.a \
	$(top_builddir)/dlib/libDlib.a \
	@FLTK_LIBS@ @LIBX11_LIBS@
trie_SOURCES = trie.cc
trie_LDADD = \
	$(top_builddir)/dw/libDw-widgets.a \
	$(top_builddir)/dw/libDw-fltk.a \
	$(top_builddir)/dw/libDw-core.a \
	$(top_builddir)/lout/liblout.a \
	$(top_builddir)/dlib/libDlib.a \
	@FLTK_LIBS@ @LIBX11_LIBS@
