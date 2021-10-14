include $(top_srcdir)/build/vars.build.mk
plugin = builder

geanyplugins_LTLIBRARIES = builder.la

regex-list.c: ../data/regex-list.txt
	cat ../data/regex-list.txt | sort | awk -f ../regex-list.awk > $@

EXTRA_builder_la_DEPENDENCIES = regex-list.c

builder_la_SOURCES = builder.c utils.c
builder_la_CPPFLAGS = $(AM_CPPFLAGS) -DG_LOG_DOMAIN=\"Builder\"
builder_la_CFLAGS = $(AM_CFLAGS) $(GIO_CFLAGS) \
	-I$(top_srcdir)/utils/src
builder_la_LIBADD = $(COMMONLIBS) $(GIO_LIBS) \
	$(top_builddir)/utils/src/libgeanypluginutils.la

AM_CPPCHECKFLAGS = --suppress='deallocDealloc:$(srcdir)/builder.c'
AM_CPPCHECKFLAGS += --suppress='doubleFree:$(srcdir)/builder.c'

include $(top_srcdir)/build/cppcheck.mk
