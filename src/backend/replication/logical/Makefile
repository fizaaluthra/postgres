#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for src/backend/replication/logical
#
# IDENTIFICATION
#    src/backend/replication/logical/Makefile
#
#-------------------------------------------------------------------------

subdir = src/backend/replication/logical
top_builddir = ../../../..
include $(top_builddir)/src/Makefile.global

override CPPFLAGS := -I$(srcdir) $(CPPFLAGS)

OBJS = \
<<<<<<< HEAD
	applyparallelworker.o \
	conflict.o \
=======
	yb_decode.o \
	yb_virtual_wal_client.o \
	\
>>>>>>> 939dce21892 (yb changes)
	decode.o \
	launcher.o \
	logical.o \
	logicalctl.o \
	logicalfuncs.o \
	message.o \
	origin.o \
	proto.o \
	relation.o \
	reorderbuffer.o \
	sequencesync.o \
	slotsync.o \
	snapbuild.o \
	syncutils.o \
	tablesync.o \
	worker.o

include $(top_srcdir)/src/backend/common.mk
