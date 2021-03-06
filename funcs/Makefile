#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for dialplan functions
#
# Copyright (C) 2005, Digium
#
# Kevin P. Fleming <kpfleming@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

FUNCS=pbx_functions.so

BUILTINS=func_md5.o \
         func_math.o \
         func_groupcount.o \
         func_strings.o \
         func_cdr.o \
         func_logic.o \
         func_env.o \
         func_db.o \
         func_timeout.o \
         func_language.o \
         func_moh.o

STANDALONE_FUNCS=$(filter-out $(BUILTINS),$(patsubst %.c,%.o,$(wildcard func*.c)))

FUNCS+=$(STANDALONE_FUNCS:.o=.so)

FUNC_SOURCES=$(BUILTINS:.o=.c)

FUNC_STRUCTS=$(shell grep 'struct ast_custom_function' $(FUNC_SOURCES) | awk '{print $$3};')

ifeq (${OSARCH},CYGWIN)
CYGSOLINK=-Wl,--out-implib=lib$@.a -Wl,--export-all-symbols
CYGSOLIB=-L.. -L. -lasterisk.dll
else
CFLAGS+=-fPIC
endif

ifeq ($(findstring BSD,${OSARCH}),BSD)
  CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/include -L$(CROSS_COMPILE_TARGET)/usr/local/lib
endif

all: $(FUNCS)

clean:
	rm -f *.so *.o .depend pbx_functions.h

%.so : %.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB}

#$(BUILTINS) : CFLAGS += -DBUILTIN_FUNC

pbx_functions.h: $(FUNC_SOURCES)
	@echo "/* Automatically generated - do not edit */" > $@
	@for f in $(FUNC_SOURCES); do echo "#include \"$$f\"" >> $@; done
	@echo "static struct ast_custom_function *builtins[] = {" >> $@
	@for f in $(FUNC_STRUCTS); do echo "&$$f," >> $@; done
	@echo "};" >> $@

pbx_functions.o: pbx_functions.h

pbx_functions.so: pbx_functions.o #$(BUILTINS)
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB}

install: all
	for x in $(FUNCS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

ifneq ($(wildcard .depend),)
  include .depend
endif

depend: .depend

.depend: pbx_functions.h
	../build_tools/mkdep $(CFLAGS) `ls *.c`

env:
	env
