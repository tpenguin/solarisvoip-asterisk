#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for PBX applications
#
# Copyright (C) 1999-2005, Digium
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

APPS=app_dial.so app_playback.so app_voicemail.so app_directory.so app_mp3.so\
     app_system.so app_echo.so app_record.so app_image.so app_url.so app_disa.so \
     app_adsiprog.so app_getcpeid.so app_milliwatt.so \
     app_zapateller.so app_setcallerid.so app_festival.so \
     app_queue.so app_senddtmf.so app_parkandannounce.so \
     app_setcidname.so app_lookupcidname.so app_macro.so \
     app_authenticate.so app_softhangup.so app_lookupblacklist.so \
     app_waitforring.so app_privacy.so app_db.so app_chanisavail.so \
     app_enumlookup.so app_transfer.so app_setcidnum.so app_cdr.so \
     app_hasnewvoicemail.so app_sayunixtime.so app_cut.so app_read.so \
     app_setcdruserfield.so app_random.so app_ices.so app_eval.so \
     app_nbscat.so app_sendtext.so app_exec.so \
     app_groupcount.so app_txtcidname.so app_controlplayback.so \
     app_talkdetect.so app_alarmreceiver.so app_userevent.so app_verbose.so \
     app_test.so app_forkcdr.so app_math.so app_realtime.so \
     app_dumpchan.so app_waitforsilence.so app_while.so app_setrdnis.so \
     app_md5.so app_readfile.so app_chanspy.so app_settransfercapability.so \
     app_dictate.so app_externalivr.so app_directed_pickup.so \
     app_mixmonitor.so app_stack.so app_amd.so

#
# Obsolete things...
#
#APPS+=app_sql_postgres.so
#APPS+=app_sql_odbc.so

#
# Experimental things
#
#APPS+=app_ivrdemo.so
#APPS+=app_skel.so
#APPS+=app_rpt.so

ifndef WITHOUT_ZAPTEL
ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/linux/zaptel.h)$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/zaptel.h)$(wildcard ../../zaptel-solaris/zaptel.h),)
  APPS+=app_zapras.so app_meetme.so app_flash.so app_zapbarge.so app_zapscan.so app_page.so
endif
endif # WITHOUT_ZAPTEL

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/spandsp.h $(CROSS_COMPILE_TARGET)/usr/include/spandsp.h $(CROSS_COMPILE_TARGET)/opt/include/spandsp.h),)
APPS+=app_rxfax.so app_txfax.so
SOLINK+=-lspandsp -L/usr/lib/mysql -lmysqlclient_r
endif

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/osp/osp.h $(CROSS_COMPILE_TARGET)/usr/include/osp/osp.h),)
APPS+=app_osplookup.so
endif

ifeq ($(findstring BSD,${OSARCH}),BSD)
CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/include -L$(CROSS_COMPILE_TARGET)/usr/local/lib
endif

ifeq ($(findstring SunOS,${OSARCH}),SunOS)
CFLAGS+=-I/opt/csw/include -I/opt/include -I../../zaptel-solaris
SOLINK+=-R/opt/csw/lib -L/opt/csw/lib -L/opt/include -R/opt/lib -L../../zaptel-solaris -L/usr/sfw/lib -R/usr/sfw/lib
endif

# On Sun, we will test for CSW curl
ifeq ($(findstring SunOS,${OSARCH}),SunOS)
  ifneq ($(wildcard /opt/csw/bin/curl-config),)
    CURLCONFIG=/opt/csw/bin/curl-config
  endif
  ifneq ($(wildcard /usr/bin/curl-config),)
    CURLCONFIG=/usr/bin/curl-config
  endif
  ifneq (${CURLCONFIG},)
    CURLLIBS=$(shell $(CURLCONFIG) --libs)
    ifneq (${CURLLIBS},)
      APPS+=app_curl.so app_lookupcnam.so
    endif
  endif
else
  CURLLIBS=$(shell $(CROSS_COMPILE_BIN)curl-config --libs)
  ifneq ($(shell if [ 0x`$(CROSS_COMPILE_BIN)curl-config --vernum` -ge 0x70907 ]; then echo "OK" ; fi),)
    ifneq (${CURLLIBS},)
      APPS+=app_curl.so app_lookupcnam.so
    endif
  endif
endif

ifeq (${OSARCH},CYGWIN)
CYGSOLINK=-Wl,--out-implib=lib$@.a -Wl,--export-all-symbols
CYGSOLIB=-L.. -L. -L../res -lasterisk.dll -lres_features.so -lres_adsi.so -lres_monitor.so
else
CFLAGS+=-fPIC
APPS+=app_sms.so
endif

#
# If you have UnixODBC you can use ODBC voicemail
# storage
#
# Uncomment to use ODBC storage
#CFLAGS+=-DUSE_ODBC_STORAGE
# Uncomment for extended ODBC voicemail storage
#CFLAGS+=-DEXTENDED_ODBC_STORAGE
# See doc/README.odbcstorage for more information

all: $(APPS)

clean:
	rm -f *.so *.o .depend

%.so : %.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB}

app_rpt.so : app_rpt.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} -ltonezone

install: all
	for x in $(APPS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done
	rm -f $(DESTDIR)$(MODULES_DIR)/app_datetime.so
	rm -f $(DESTDIR)$(MODULES_DIR)/app_qcall.so

app_curl.so: app_curl.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} $(CURLLIBS)

app_lookupcnam.so: app_lookupcnam.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} $(CURLLIBS)

app_sql_postgres.o: app_sql_postgres.c
	$(CC) -pipe -I$(CROSS_COMPILE_TARGET)/usr/local/pgsql/include -I$(CROSS_COMPILE_TARGET)/usr/include/postgresql $(CFLAGS) -c -o app_sql_postgres.o app_sql_postgres.c

app_sql_postgres.so: app_sql_postgres.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} -L/usr/local/pgsql/lib -lpq

app_sql_odbc.so: app_sql_odbc.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} -lodbc

ifeq (SunOS,$(shell uname))
app_chanspy.so: app_chanspy.o
	$(CC) $(SOLINK) -o $@ $< -lrt
endif


ifneq ($(wildcard .depend),)
 include .depend
endif

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`

env:
	env
