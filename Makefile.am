
EXTRA_DIST = \
	README.txt \
	AUTHORS \
	COPYING

dist_man_MANS = \
	datop.8

AM_CFLAGS = -fPIC -O2 -g -Wall -W -Wformat-security \
			-Wno-stringop-truncation -Wno-stringop-overflow \
			-D_FORTIFY_SOURCE=2 -fno-common -std=gnu99
ACLOCAL_AMFLAGS = -I m4

noinst_LTLIBRARIES = libdatop.la
libdatop_la_SOURCES = \
	src/include/os/map.h \
	src/include/os/os_cmd.h \
	src/include/os/os_page.h \
	src/include/os/os_perf.h \
	src/include/os/os_util.h \
	src/include/os/os_win.h \
	src/include/damon.h \
	src/include/pfwrapper.h \
	src/include/plat.h \
	src/include/cmd.h \
	src/include/disp.h \
	src/include/page.h \
	src/include/perf.h \
	src/include/proc.h \
	src/include/reg.h \
	src/include/types.h \
	src/include/ui_perf_map.h \
	src/include/util.h \
	src/include/win.h \
	src/common/os_cmd.c \
	src/common/os_page.c \
	src/common/os_perf.c \
	src/common/os_util.c \
	src/common/os_win.c \
	src/damon.c \
	src/proc_map.c \
	src/pfwrapper.c \
	src/cmd.c \
	src/disp.c \
	src/page.c \
	src/perf.c \
	src/proc.c \
	src/reg.c \
	src/ui_perf_map.c \
	src/util.c \
	src/win.c

bin_PROGRAMS = datop

datop_CFLAGS = $(NCURSES_CFLAGS)
datop_LDADD = $(NCURSES_LIBS) libdatop.la
datop_SOURCES = src/datop.c

distclean-local:
	rm -rf .deps
	rm -rf test
	rm -rf aarch64/.deps
	rm -rf autom4te.cache
