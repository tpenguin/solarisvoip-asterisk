/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006 Sun Microsystems
 *
 * Written by Tony Yu Zhao <tony.yu.zhao@gmail.com>
 * Patterned after res_config_odbc.c
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 *
 */ 
/*! \file
 *
 * \brief asterisk database plugin for portable configuration engine (only static config currently)
 *
 * extconfig.conf
 * <conf filename> => astdb, asterisk
 *
 * Asterisk Database Storage Format 
 *	  Family               Key            Value
 *    ---------------------------------------------------------
 *    filename             category.var   value1;value2;value3                            	  
 *
 * Works with multiple values of the same variable and #includes
 * Order enforced for allow/disallow, Example: /sip.conf/general.allow !all,gsm,!ulaw,... 
 *
 * Limitations:
 * 1. Only static database configuration available currently
 * 2. Should work for all configuration files except extensions.conf (Useful for iax.conf, sip.conf, and voicemail.conf)
 * 3. Order not enforced except for allow, disallow 
 *
 */
 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>

#include <asterisk/channel.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/astdb.h>
#include <asterisk/cli.h>
#include <asterisk/utils.h>
#include <asterisk/lock.h>
#include <asterisk/module.h>

#include "../db1-ast/include/db.h"
#include "asterisk.h"

AST_MUTEX_DEFINE_STATIC(astdb_lock);
static char *res_config_astdb_desc = "Asterisk Database RealTime Configuration Driver";
#define RES_CONFIG_ASTDB_CONF "res_astdb.conf"

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static struct ast_variable *realtime_astdb(const char *database, const char *table, va_list ap)
{
   /* Not implemented */
   return NULL;
}   
static struct ast_config *realtime_multi_astdb(const char *database, const char *table, va_list ap)
{
   /* Not implemented */
   return NULL;
}   
static int update_astdb(const char *database, const char *table, const char *keyfield, const char *lookup, va_list ap)
{
   /* Not implemented */
   return NULL;
}
static struct ast_config *config_astdb(const char *database, const char *table, const char *file, struct ast_config *cfg)
{
	struct ast_variable *new_v;
	struct ast_category *cur_cat;
   char *category, *key, *value, *temp;
	struct ast_db_entry *db_tree;
	struct ast_db_entry *entry;
	char *delim_val;
   
	/* Lets connect to the local Asterisk Database and execute it. */
	ast_mutex_lock(&astdb_lock);
   db_tree = ast_db_gettree(file, NULL);
	
	for (entry = db_tree; entry; entry = entry->next) {
		/* tokenize the format stored in the database to something Asterisk expects to load */
		temp = strtok(entry->key, "/");
	  	temp = strtok(NULL, "/");
	  	category = strtok(temp, ".");
	  	key =  strtok(NULL, ".");
	  	value = entry->data;
	  	     		       
		ast_verbose(VERBOSE_PREFIX_3 "%-25s  %-25s %-25s %-25s\n", file, category, key, value);
		/* Add the category */ 
	   if(ast_category_get(cfg, category) == NULL ) {
		   cur_cat = ast_category_new(category);
		   if (!cur_cat) {
			   ast_log(LOG_WARNING, "Out of memory!\n");
				ast_verbose(VERBOSE_PREFIX_3 "Out of memory!\n");
				break;
			}
			ast_category_append(cfg, cur_cat);
			ast_verbose(VERBOSE_PREFIX_3 "Asterisk Database RealTime: category %s added\n", category);
		} else {
		   cur_cat = ast_category_get(cfg, category); 
		   ast_verbose(VERBOSE_PREFIX_3 "Asterisk Database RealTime: category %s was previously added\n", category);
		}
			  
		/* Now we add the values */   
	   /* If value is #include */ 	  	        
	   if (!strcmp(key, "#include")) {
		   if (!ast_config_internal_load(value, cfg)) {
				ast_mutex_unlock(&astdb_lock);
				return NULL;
			}
			continue;
		} 
			  	
	  	/* If value if a ';' delimited list we need to break it up and add them separately (ex. one can have multiple allows) */
	  	for ( delim_val = strtok(value,";"); delim_val != NULL; delim_val = strtok(NULL, ";")) {
	  	   if (!strcmp(key, "allow")) {
	  	     /*allow = all;!gsm;!ulaw;gsm (order dependent) */
	  	     if(delim_val[0] == '!') {
	  	        new_v = ast_variable_new("disallow", delim_val + sizeof(char));   
	  	     } else {
	  	        new_v = ast_variable_new("allow", delim_val);  
	  	     }
	  	   } else {
	  	   //can add more order dependent cases in the future
	  	     new_v = ast_variable_new(key, delim_val);
		   }
		   ast_variable_append(cur_cat, new_v);
		   ast_verbose(VERBOSE_PREFIX_3 "Asterisk Database RealTime: added %s %s %s %s\n", file, category, key, delim_val); 
		}   
	}
	ast_mutex_unlock(&astdb_lock);

	return cfg;
}

static struct ast_config_engine astdb_engine = {
	.name = "astdb",
	.load_func = config_astdb,
	.realtime_func = realtime_astdb,
	.realtime_multi_func = realtime_multi_astdb,
	.update_func = update_astdb
};

int load_module (void)
{
	ast_mutex_lock(&astdb_lock);

	ast_config_engine_register(&astdb_engine);
	if(option_verbose) {
		ast_verbose("Asterisk Database RealTime driver loaded.\n");
	}

	ast_mutex_unlock(&astdb_lock);

	return 0;
}

int unload_module (void)
{
	/* Aquire control before doing anything to the module itself. */
	ast_mutex_lock(&astdb_lock);

	ast_config_engine_deregister(&astdb_engine);
	if(option_verbose) {
		ast_verbose("Asterisk Database RealTime unloaded.\n");
	}

	STANDARD_HANGUP_LOCALUSERS;

	/* Unlock so something else can destroy the lock. */
	ast_mutex_unlock(&astdb_lock);

	return 0;
}

int reload (void)
{
	/* Aquire control before doing anything to the module itself. */
	ast_mutex_lock(&astdb_lock);

	ast_verbose(VERBOSE_PREFIX_2 "Asterisk Database RealTime reloaded.\n");

	/* Done reloading. Release lock so others can now use driver. */
	ast_mutex_unlock(&astdb_lock);

	return 0;
}

char *description (void)
{
	return res_config_astdb_desc;
}

int usecount (void)
{
	/* Try and get a lock. If unsuccessful, than that means another thread is using the astdb object. */
	if(ast_mutex_trylock(&astdb_lock)) {
		ast_log(LOG_DEBUG, "Asterisk Database RealTime: Module usage count is 1.\n");
		return 1;
	}
	ast_mutex_unlock(&astdb_lock);
	return 0;
}

char *key ()
{
	return ASTERISK_GPL_KEY;
}


