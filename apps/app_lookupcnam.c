/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 */

/*! \file
 *
 * \brief App to set callerid name from iSPService's CNAM Service
 *
 * \author Joseph Benden <joe@thrallingpenguin.com>
 * 
 * \ingroup applications
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/cli.h"
#include "asterisk/module.h"
#include "asterisk/callerid.h"

#define CNAM_URL "http://www.ispservices.com/ndis.php"

static char *tdesc = "Look up CallerID Name from iSP Service's CNAM service";
static char *app = "LookupCNAM";
static char *synopsis = "Look up CallerID Name from iSP Service's CNAM service";
static char *descrip =
        "  LookupCNAM: Looks up the Caller*ID number on the active\n"
        "  channel from the iSP Services CNAM service and sets the\n"
        "  Caller*ID name.  Does nothing if no Caller*ID number was\n"
        "  received on the channel, or if a Caller*ID name was\n"
        "  already present.\n";

static void
perform_lookupcnam (struct ast_channel *chan, const char *username, const char *password);

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

struct MemoryStruct {
	char *memory;
	size_t size;
};

static void *
my_realloc(void *ptr, size_t size)
{
	if(ptr) return realloc(ptr, size);
	return malloc(size);
}

static size_t 
curlMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data)
{
	register int realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *)data;
	
	mem->memory = (char *)my_realloc(mem->memory, mem->size + realsize + 1);
	if(mem->memory) {
		memcpy(&(mem->memory[mem->size]), ptr, realsize);
		mem->size += realsize;
		mem->memory[mem->size] = 0;
	}
	return realsize;
}

static int 
lookupcnam_internal(struct MemoryStruct *chunk, char *url, char *post)
{
	CURL *curl;
	
	curl_global_init(CURL_GLOBAL_ALL);
	if((curl = curl_easy_init()) == 0) {
		return -1;
	}
	
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlMemoryCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)chunk);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "lookupcnam/1.0");
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post);
	
	curl_easy_perform(curl);
	ast_log(LOG_DEBUG, "LookupCNAM: %s\n", chunk->memory);
	
	curl_easy_cleanup(curl);
	return 0;
}

static int
lookupcnam_acf_exec (struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	struct localuser *u;
	char *info, *username, *password;
	
	*buf = '\0';
	
	if(ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "LookupCNAM requires multiple arguments\n");
		return -1;
	}	
	
	LOCAL_USER_ACF_ADD(u);
  
	info = ast_strdupa(data);
	if(!info) {
		ast_log(LOG_ERROR, "Out of memory\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	username = strsep(&info, "|");
	password = info;

	perform_lookupcnam(chan, username, password);

	LOCAL_USER_REMOVE(u);
	return 0;
}

static int
lookupcnam_exec (struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	char *info, *username, *password;

	if(ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "LookupCNAM requires two arguments (username, password)\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	if((info=ast_strdupa(data))) {
		username = strsep(&info, "|");
		password = info;
	} else {
		ast_log(LOG_ERROR, "Out of memory.\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	perform_lookupcnam(chan, username, password);

	LOCAL_USER_REMOVE(u);
	return res;
}

static void
perform_lookupcnam (struct ast_channel *chan, const char *username, const char *password)
{
	struct MemoryStruct chunk = {NULL, 0};
	char buffer[512];

	ast_log(LOG_DEBUG,"Caller*ID Number: %s\n", (chan->cid.cid_num ? chan->cid.cid_num : "not set"));
	ast_log(LOG_DEBUG,"Caller*ID Name  : %s\n", (chan->cid.cid_name ? chan->cid.cid_name : "not set"));
 
	if (chan->cid.cid_num && 
		(!chan->cid.cid_name || ast_isphonenumber(chan->cid.cid_name)==1) ) {
			/* Perform query */
			snprintf(buffer, 512, "user=%s&pass=%s&ph=%s", username, password, chan->cid.cid_num);
			ast_log(LOG_DEBUG,"Sending URL: %s\n", buffer);
			if(!lookupcnam_internal(&chunk, CNAM_URL, buffer)) {
				if(chunk.memory) {
					chunk.memory[chunk.size] = 0;
					if(chunk.memory[chunk.size - 1] == 10)
						chunk.memory[chunk.size - 1] = 0;
					ast_set_callerid(chan, NULL, chunk.memory, NULL);
					if(option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "Changed Caller*ID name to %s\n",
							chunk.memory);
					free(chunk.memory);
				}
			} else {
				ast_log(LOG_ERROR, "Cannot allocate curl structure\n");
			}
	}
}

struct ast_custom_function acf_lookupcnam = {
	.name = "LookupCNAM",
	.synopsis = "Look up CallerID Name from iSP Service's CNAM service",
	.syntax = "LookupCNAM(username|password)",
	.desc =
	"  LookupCNAM: Looks up the Caller*ID number on the active\n"
	"  channel from the iSP Services CNAM service and sets the\n"
	"  Caller*ID name.  Does nothing if no Caller*ID number was\n"
	"  received on the channel, or if a Caller*ID name was\n"
	"  already present.\n",
	.read = lookupcnam_acf_exec
};

int
unload_module (void)
{
	int res;

	res = ast_custom_function_unregister(&acf_lookupcnam);
	res |= ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int
load_module (void)
{
	int res;
	
	res = ast_custom_function_register(&acf_lookupcnam);
	res |= ast_register_application(app, lookupcnam_exec, synopsis, descrip);
	
	return res;
}

char *
description (void)
{
	return tdesc;
}

int
usecount (void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *
key ()
{
	return ASTERISK_GPL_KEY;
}
