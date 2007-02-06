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

#include <mysql/mysql.h>
#include <mysql/errmsg.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

unsigned long ast_db_genid(MYSQL *mysql, const char *table);

unsigned long ast_db_genid(MYSQL *mysql, const char *table)
{
	unsigned long ret = 0;
	char buf[512];

	sprintf(buf, "update %s_id set id=LAST_INSERT_ID(id+1)", table);
	if (mysql_query(mysql, buf)) {
		sprintf(buf, "create table %s_id (id int not null)", table);
		if (mysql_query(mysql, buf)) {
			ast_log(LOG_ERROR, "(%d) %s\n", mysql_errno(mysql), mysql_error(mysql));
		}
		sprintf(buf, "insert into %s_id values (%d)", table, 0);
		if (mysql_query(mysql, buf)) {
			ast_log(LOG_ERROR, "(%d) %s\n", mysql_errno(mysql), mysql_error(mysql));
		}
		sprintf(buf, "update %s_id set id=LAST_INSERT_ID(id+1)", table);
		if (mysql_query(mysql, buf)) {
			ast_log(LOG_ERROR, "(%d) %s\n", mysql_errno(mysql), mysql_error(mysql));
		}
	}
	ret = mysql_insert_id(mysql);

	return ret;
}

