# contrib/pg_scheduler/Makefile

MODULE_big = pg_scheduler
OBJS = pg_scheduler.o

EXTENSION = pg_scheduler
DATA = pg_scheduler--1.0.sql

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = pg_scheduler
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
