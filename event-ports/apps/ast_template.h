/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * General MySQL database routines
 *
 * (C) 2005 Thralling Penguin LLC
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 *
 */

#include <sys/types.h>
#include <asterisk/config.h>
#include <asterisk/options.h>
#include <asterisk/channel.h>
#include <asterisk/cdr.h>
#include <asterisk/module.h>
#include <asterisk/logger.h>
#include <asterisk/cli.h>

#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

char *ast_read_template(const char *filename);


char *ast_read_template(const char *filename)
{
	FILE *fp = NULL;
	struct stat sbuf;
	char *buf = NULL;
	int len, numbytes;

	if (stat(filename, &sbuf) == -1) {
		ast_log(LOG_ERROR, "Could not stat() template file: %s\n", filename);
		return 0;
	}

	if ((fp=fopen(filename, "r"))) {
		len = sbuf.st_size + 1;
		buf = malloc(len);
		if ((numbytes=fread(buf, 1, len, fp)) < 0) {
			ast_log(LOG_ERROR, "Read %d bytes from template but could not complete the read request: %s\n", numbytes, filename);
		}
		fclose(fp);			
	} else {
		ast_log(LOG_ERROR, "Unable to open template filename: %s\n", filename);
	}
	return buf;
}

