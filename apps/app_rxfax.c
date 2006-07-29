/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Trivial application to receive a TIFF FAX file
 * 
 * Copyright (C) 2003, Steve Underwood
 *
 * Steve Underwood <steveu@coppice.org>
 *
 * Added database storage, conversion to PDF, HTML email template by Joseph Benden
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <pthread.h>
#include <errno.h>
#include <tiffio.h>

#include <spandsp.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision:$")

#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <asterisk/dsp.h>
#include <asterisk/manager.h>
#include <asterisk/config.h>

#include <spandsp.h>

#include <mysql/mysql.h>
#include <mysql/errmsg.h>

#define ASTERISK_VERSION_NUM 012000
#include "ast_adodb.h"
#include "ast_template.h"

/*
 * TODO:
 *
 * - Asterisk includes a base64 encoder extern int ast_base64encode(char *dst, unsigned char *src, int srclen, int max);
 *
 */

static char *tdesc = "Trivial FAX Receive Application";

static char *app = "RxFAX";

static char *synopsis = "Receive a FAX to a file";

static char *descrip = 
"  RxFAX(filename[|caller][|debug]): Receives a FAX from the channel into the\n"
"given filename. If the file exists it will be overwritten. The file\n"
"should be in TIFF/F format.\n"
"The \"caller\" option makes the application behave as a calling machine,\n"
"rather than the answering machine. The default behaviour is to behave as\n"
"an answering machine.\n"
"Uses LOCALSTATIONID to identify itself to the remote end.\n"
"     LOCALHEADERINFO to generate a header line on each page.\n"
"Sets REMOTESTATIONID to the sender CSID.\n"
"     FAXPAGES to the number of pages received.\n"
"     FAXBITRATE to the transmition rate.\n"
"     FAXRESOLUTION to the resolution.\n"
"Returns -1 when the user hangs up.\n"
"Returns 0 otherwise.\n";
static char *config = "rxfax.conf";
static char *hostname = NULL, *dbname = NULL, *dbuser = NULL, *password = NULL, *dbsock = NULL, *dbtable = NULL;
static char *email_from = NULL;
static int dbport = 0;
static unsigned int timeout = 0;

AST_MUTEX_DEFINE_STATIC(mysql_lock);
AST_MUTEX_DEFINE_STATIC(email_addr_lock);
AST_MUTEX_DEFINE_STATIC(base64_lock);

static MYSQL mysql;


STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

#define MAX_BLOCK_SIZE 240

static void span_message(int level, const char *msg)
{
    int ast_level;
    
    if (level == SPAN_LOG_WARNING)
        ast_level = __LOG_WARNING;
    else if (level == SPAN_LOG_WARNING)
        ast_level = __LOG_WARNING;
    else
        ast_level = __LOG_DEBUG;
    ast_log(ast_level, __FILE__, __LINE__, __PRETTY_FUNCTION__, msg);
}
/*- End of function --------------------------------------------------------*/

char *getFaxUserEmail (char* faxExten);

#define BASEMAXINLINE 256
#define BASELINELEN 72
#define BASEMAXINLINE 256
#define eol "\r\n"

struct baseio {
    int iocp;
    int iolen;
    int linelength;
    int ateof;
    unsigned char iobuf[BASEMAXINLINE];
};

/*
static void t30_flush(t30_state_t *s, int which)
{
    //TODO:
}
*/

static void phase_e_handler(t30_state_t *s, void *user_data, int result)
{
    struct ast_channel *chan;
    t30_stats_t t;
    char local_ident[21];
    char far_ident[21];
    char buf[11];
    
    chan = (struct ast_channel *) user_data;
    if (result == T30_ERR_OK)
    {
        t30_get_transfer_statistics(s, &t);
        t30_get_far_ident(s, far_ident);
        t30_get_local_ident(s, local_ident);
        ast_log(LOG_DEBUG, "==============================================================================\n");
        ast_log(LOG_DEBUG, "Fax successfully received.\n");
        ast_log(LOG_DEBUG, "Remote station id: %s\n", far_ident);
        ast_log(LOG_DEBUG, "Local station id:  %s\n", local_ident);
        ast_log(LOG_DEBUG, "Pages transferred: %i\n", t.pages_transferred);
        ast_log(LOG_DEBUG, "Image resolution:  %i x %i\n", t.column_resolution, t.row_resolution);
        ast_log(LOG_DEBUG, "Transfer Rate:     %i\n", t.bit_rate);
        ast_log(LOG_DEBUG, "==============================================================================\n");
        manager_event(EVENT_FLAG_CALL,
                      "FaxReceived", "Channel: %s\nExten: %s\nCallerID: %s\nRemoteStationID: %s\nLocalStationID: %s\nPagesTransferred: %i\nResolution: %i\nTransferRate: %i\nFileName: %s\n",
                      chan->name,
                      chan->exten,
#if (ASTERISK_VERSION_NUM <= 011000)
                      chan->callerid,
#else
                      (chan->cid.cid_num)  ?  chan->cid.cid_num  :  "",
#endif
                      far_ident,
                      local_ident,
                      t.pages_transferred,
                      t.row_resolution,
                      t.bit_rate,
                      s->rx_file);
        pbx_builtin_setvar_helper(chan, "REMOTESTATIONID", far_ident);
        snprintf(buf, sizeof(buf), "%i", t.pages_transferred);
        pbx_builtin_setvar_helper(chan, "FAXPAGES", buf);
        snprintf(buf, sizeof(buf), "%i", t.row_resolution);
        pbx_builtin_setvar_helper(chan, "FAXRESOLUTION", buf);
        snprintf(buf, sizeof(buf), "%i", t.bit_rate);
        pbx_builtin_setvar_helper(chan, "FAXBITRATE", buf);
    }
    else
    {
        ast_log(LOG_DEBUG, "==============================================================================\n");
        ast_log(LOG_DEBUG, "Fax receive not successful - result (%d) %s.\n", result, t30_completion_code_to_str(result));
        ast_log(LOG_DEBUG, "==============================================================================\n");
    }
}
/*- End of function --------------------------------------------------------*/

static void phase_d_handler(t30_state_t *s, void *user_data, int result)
{
    struct ast_channel *chan;
    t30_stats_t t;
    
    chan = (struct ast_channel *) user_data;
    if (result)
    {
        t30_get_transfer_statistics(s, &t);
        ast_log(LOG_DEBUG, "==============================================================================\n");
        ast_log(LOG_DEBUG, "Pages transferred:  %i\n", t.pages_transferred);
        ast_log(LOG_DEBUG, "Image size:         %i x %i\n", t.columns, t.rows);
        ast_log(LOG_DEBUG, "Image resolution    %i x %i\n", t.column_resolution, t.row_resolution);
        ast_log(LOG_DEBUG, "Transfer Rate:      %i\n", t.bit_rate);
        ast_log(LOG_DEBUG, "Bad rows            %i\n", t.bad_rows);
        ast_log(LOG_DEBUG, "Longest bad row run %i\n", t.longest_bad_row_run);
        ast_log(LOG_DEBUG, "Compression type    %i\n", t.encoding);
        ast_log(LOG_DEBUG, "Image size (bytes)  %i\n", t.image_size);
        ast_log(LOG_DEBUG, "==============================================================================\n");
    }
}
/*- End of function --------------------------------------------------------*/

static int
inbuf(struct baseio *bio, FILE *fi)
{
    int l;

    if(bio->ateof)
        return 0;
    if ( (l = fread(bio->iobuf,1,BASEMAXINLINE,fi)) <= 0) {
        if(ferror(fi))
            return -1;
        bio->ateof = 1;
        return 0;
    }
    bio->iolen= l;
    bio->iocp= 0;
    return 1;
}

static int 
inchar(struct baseio *bio, FILE *fi)
{
    if(bio->iocp>=bio->iolen)
        if(!inbuf(bio, fi))
            return EOF;
    return bio->iobuf[bio->iocp++];
}

static int
ochar(struct baseio *bio, int c, FILE *so)
{
    if(bio->linelength>=BASELINELEN) {
        if(fputs(eol,so)==EOF)
            return -1;

        bio->linelength= 0;
    }
    if(putc(((unsigned char)c),so)==EOF)
        return -1;
    bio->linelength++;
    return 1;
}

static int base_encode(char *filename, FILE *so)
{
    unsigned char dtable[BASEMAXINLINE];
    int i,hiteof= 0;
    FILE *fi;
    struct baseio bio;

    memset(&bio, 0, sizeof(bio));
    bio.iocp = BASEMAXINLINE;

    if (!(fi = fopen(filename, "rb"))) {
        ast_log(LOG_WARNING, "Failed to open log file: %s: %s\n", filename, strerror(errno));
        return -1;
    }

    for (i= 0;i<9;i++){
        dtable[i]= 'A'+i;
        dtable[i+9]= 'J'+i;
        dtable[26+i]= 'a'+i;
        dtable[26+i+9]= 'j'+i;
    }
    for (i= 0;i<8;i++){
        dtable[i+18]= 'S'+i;
        dtable[26+i+18]= 's'+i;
    }
    for (i= 0;i<10;i++){
        dtable[52+i]= '0'+i;
    }
    dtable[62]= '+';
    dtable[63]= '/';
    while (!hiteof) {
        unsigned char igroup[3],ogroup[4];
        int c,n;

        igroup[0] = igroup[1] = igroup[2]= 0;

        for (n= 0;n<3;n++) {
            if ((c = inchar(&bio, fi)) == EOF) {
                hiteof = 1;
                break;
            }

            igroup[n] = (unsigned char)c;
        }

        if (n > 0) {
            ogroup[0] = dtable[igroup[0]>>2];
            ogroup[1] = dtable[((igroup[0]&3)<<4)|(igroup[1]>>4)];
            ogroup[2] = dtable[((igroup[1]&0xF)<<2)|(igroup[2]>>6)];
            ogroup[3] = dtable[igroup[2]&0x3F];

            if (n<3) {
                ogroup[3]= '=';

                if (n<2)
                    ogroup[2]= '=';
            }

            for (i= 0;i<4;i++)
                ochar(&bio, ogroup[i], so);
        }
    }

    if (fputs(eol,so)==EOF)
        return 0;

    fclose(fi);
    return 1;
}

char *getFaxUserEmail (char* faxExten) {
    FILE *configin;
    char inbuf[256];
    char orig[256];
    char tmpin[AST_CONFIG_MAX_PATH];
    char *user, *faxemail, *trim;

	if(!faxExten) return NULL;
	
	ast_mutex_lock(&email_addr_lock);	
    snprintf((char *)tmpin, sizeof(tmpin)-1, "%s/fax.conf",(char *)ast_config_AST_CONFIG_DIR);
    configin = fopen((char *)tmpin,"r");
                
    if(!configin) {
        if (configin)
            fclose(configin);
        else
            ast_log(LOG_WARNING, "Warning: Unable to open '%s' for reading: %s\n", tmpin, strerror(errno));
		ast_mutex_unlock(&email_addr_lock);
        return NULL;
    }
    while (!feof(configin)) {
        /* Read in the line */
        fgets(inbuf, sizeof(inbuf), configin);
        if (!feof(configin)) {
            /* Make a backup of it */
            memcpy(orig, inbuf, sizeof(orig));
            /* Strip trailing \n and comment */
            inbuf[strlen(inbuf) - 1] = '\0';
            user = strchr(inbuf, ';');
            if (user)
                *user = '\0';
            user=inbuf;
            while(*user < 33)
                user++;
            faxemail = strchr(user, '=');
            if (faxemail > user) {
                trim = faxemail - 1;
                while(*trim && *trim < 33) {
                    *trim = '\0';
                    trim--;
                }
            }
            if (faxemail) {
                *faxemail = '\0';
                faxemail++;
                if (*faxemail == '>')
                    faxemail++;
                while(*faxemail && *faxemail < 33)
                    faxemail++;
            }
            if (user && faxemail && *user && *faxemail && !strcmp(user, faxExten)) {
                /* Return Email */
                fclose(configin);
				ast_mutex_unlock(&email_addr_lock);
                return faxemail;
            } 
        }
    }
    fclose(configin);
	ast_mutex_unlock(&email_addr_lock);
    return NULL;
}

static int faxToDatabase (struct ast_channel *fxchan, char* attach, char* attachType, t30_state_t *s)
{
    FILE *p = NULL;
    char local_ident[21];
    char far_ident[21];
    t30_stats_t tstats;
    char *sqlcmd = NULL, *buf = NULL, *escapebuf = NULL;
    char *mime_type;
    size_t len = 0;
    unsigned long faxID = 0, data_id = 0;
    int res = -1;

    /* Gather the structures containing the stats about the fax */
    t30_get_transfer_statistics(s, &tstats);
    t30_get_far_ident(s, far_ident);
    t30_get_local_ident(s, local_ident);

    if (!strcmp(attachType, "pdf")) {
        mime_type = "application/x-pdf";
    } else if (!strcmp(attachType, "tif")) {
        mime_type = "image/x-tif";
    } else {
        mime_type = "octet-stream";
    }
		
	ast_mutex_lock(&mysql_lock);

	mysql_init(&mysql);
	if (timeout && mysql_options(&mysql, MYSQL_OPT_CONNECT_TIMEOUT, (char *)&timeout)!=0) {
		ast_log(LOG_ERROR, "mysql_options returned: (%d) %s\n", mysql_errno(&mysql), mysql_error(&mysql));
	}
	if (!mysql_real_connect(&mysql, hostname, dbuser, password, dbname, dbport, dbsock, 0)) {
		ast_log(LOG_ERROR, "cannot connect to database server %s. Fax will not be logged.\n", hostname);
		goto fax_cleanup;
	} else {
		/* unspool */ ;
	}

    /* Insert into the fax table */
	faxID = ast_db_genid(&mysql, dbtable);
	if (!faxID) {
		ast_log(LOG_ERROR, "Unable to create a database id.\n");
		goto fax_cleanup;
	}
	sqlcmd = malloc(65536);
	if (!sqlcmd) {
		ast_log(LOG_ERROR, "Unable to allocate memory.\n");
		goto fax_cleanup;
	}
	escapebuf = malloc((65536 * 2) + 1);
	if (!escapebuf) {
		ast_log(LOG_ERROR, "Unable to allocate memory for escapebuf.\n");
		goto fax_cleanup;
	}
	buf = malloc(32768);
	if (!buf) {
		ast_log(LOG_ERROR, "Unable to allocate memory for buf.\n");
		goto fax_cleanup;
	}
    snprintf(sqlcmd, 65536, "INSERT INTO %s (id,site_id,date_orig,clid,src,dst,pages,image_size,image_resolution,transfer_rate,bad_rows,image_bytes,mime_type) VALUES (%lu,%d,%ld,'%s','%s','%s',%d,'%d x %d','%d x %d',%d,%d,%d,'%s')", dbtable, 
		faxID, 1, time(NULL),
#if (ASTERISK_VERSION_NUM <= 011000)
		(fxchan->callerid ? fxchan->callerid : "Unknown Caller"),
#else
		(fxchan->cid.cid_num ? fxchan->cid.cid_num : ""),
#endif
        far_ident,
        local_ident,
        tstats.pages_transferred,
        tstats.columns, tstats.rows,
        tstats.column_resolution, tstats.row_resolution,
        tstats.bit_rate,
        tstats.bad_rows,
        tstats.image_size,
        mime_type);
    if (mysql_real_query(&mysql, sqlcmd, strlen(sqlcmd))) {
        ast_log(LOG_ERROR, "Failed to insert into database: (%d) %s\n", mysql_errno(&mysql), mysql_error(&mysql));
        mysql_close(&mysql);
        goto fax_cleanup;
    }

    if ((p = fopen(attach, "rb"))) {
        while (!feof(p)) {
            len = fread(buf, 1, 32768, p);
            mysql_escape_string(escapebuf, buf, len);
			sprintf(sqlcmd, "%s_data", dbtable);
			data_id = ast_db_genid(&mysql, sqlcmd);
            snprintf(sqlcmd, 65536, "INSERT INTO %s_data (id,site_id,fax_id,faxdata) VALUES (%lu,%d,%lu,'%s')",
							dbtable, data_id, 1, faxID, escapebuf);
            if (mysql_real_query(&mysql, sqlcmd, strlen(sqlcmd))) {
                ast_log(LOG_ERROR, "Failed to insert into database: (%d) %s\n", mysql_errno(&mysql), mysql_error(&mysql));
                mysql_close(&mysql);
                goto fax_cleanup;
            }
        }
        fclose(p);
    }
	res = 0;
    mysql_close(&mysql);

fax_cleanup:
	if(buf)
		free(buf);
	if(sqlcmd)
		free(sqlcmd);
	if(escapebuf)
		free(escapebuf);
	ast_mutex_unlock(&mysql_lock);
	return res;
}

static int faxToMail (struct ast_channel *fxchan, char* attach, char* attachType, t30_state_t *s)
{
	FILE *p;
	char *fxemail;
	char bound[256];
	t30_stats_t tstats;
	char local_ident[21];
	char far_ident[21];
	char *template = NULL;
	char bufPages[8];
	char bufImageSize[16];
	int len;
	char *outbuffer = NULL;
	char template_name[AST_CONFIG_MAX_PATH];

	/* Gather the structures containing the stats about the fax */
	t30_get_transfer_statistics(s, &tstats);
	t30_get_far_ident(s, far_ident);
	t30_get_local_ident(s, local_ident);
	
	/* Load template file */
    snprintf((char *)template_name, sizeof(template_name)-1, "%s/fax_template.html",(char *)ast_config_AST_CONFIG_DIR);
	template = ast_read_template(template_name);
	if (!template) {
		ast_log(LOG_ERROR, "Unable to read template in rxfax.\n");
		return -1;
	}
	
	p = popen("/usr/sbin/sendmail -t", "w");
	if (p) {
		fprintf(p, "From: %s\n", email_from);
		fxemail = getFaxUserEmail(local_ident);
		if (!fxemail) {
			ast_log(LOG_WARNING, "Couldn't find %s in fax.conf, sending to root@localhost\n", local_ident);
			fxemail = "root@localhost";
		}
		fprintf(p,"To: %s\n",fxemail);
#if (ASTERISK_VERSION_NUM <= 011000)
    	fprintf(p, "Subject: New Fax From %s.\n",(fxchan->callerid ? fxchan->callerid : "Unknown"));
#else
		fprintf(p, "Subject: New Fax From %s.\n",(fxchan->cid.cid_num ? fxchan->cid.cid_num : "Unknown"));
#endif
	    fprintf(p, "MIME-Version: 1.0\n");
		snprintf(bound, sizeof(bound), "Boundary=%dfaxrec", (int)getpid());
		fprintf(p, "Content-Type: MULTIPART/MIXED; BOUNDARY=\"%s\"\n\n\n",bound);
		fprintf(p, "--%s\n",bound);

		fprintf(p, "Content-Type: text/html; charset=US-ASCII\n\n");

	/* perform variable substitution */
#if (ASTERISK_VERSION_NUM <= 011000)
		pbx_builtin_setvar_helper(fxchan, "FAX_FROM", (fxchan->callerid ? fxchan->callerid : "Unknown"));
#else
		pbx_builtin_setvar_helper(fxchan, "FAX_FROM", (fxchan->cid.cid_num ? fxchan->cid.cid_num : "Unknown"));
#endif
		pbx_builtin_setvar_helper(fxchan, "FAX_TO", local_ident);
		snprintf(bufPages, 8, "%i", tstats.pages_transferred);
		pbx_builtin_setvar_helper(fxchan, "FAX_PAGES", bufPages);
		snprintf(bufImageSize, 16, "%i x %i", tstats.columns, tstats.rows);
		pbx_builtin_setvar_helper(fxchan, "FAX_IMAGE_SIZE", bufImageSize);
		len = strlen(template)*3 + 200;
		if (!(outbuffer=malloc(len))) {
			ast_log(LOG_WARNING, "Cannot allocate workspace for variable substitution.\n");
			if (template)
				free(template);
			return -1;
		}
		memset(outbuffer, 0, len);
		pbx_substitute_variables_helper(fxchan, template, outbuffer, len);

		/* output template */
		fprintf(p, "%s", outbuffer);
		fflush(p);
		
		if (outbuffer)
			free(outbuffer);

		fprintf(p, "\n--%s\n",bound);
		fprintf(p, "Content-Type: image/x-%s; name=\"fax.%s\"\n", attachType, attachType);
		fprintf(p, "Content-Transfer-Encoding: base64\n");
		fprintf(p, "Content-Description: Fax2Email attachment.\n");
		fprintf(p, "Content-Disposition: attachment; filename=\"fax.%s\"\n\n", attachType);
		/* TODO: Is this thread-safe? */
		ast_mutex_lock(&base64_lock);
		base_encode(attach, p);
		ast_mutex_unlock(&base64_lock);
		fprintf(p, "\n\n--%s--\n.\n",bound);
		pclose(p);
	}
	if (template)
		free(template);
	return 0;
}

static int rxfax_exec(struct ast_channel *chan, void *data)
{
    int res = 0;
    char template_file[256];
    char target_file[256];
    char pdf_file[256];
    char cmd[1024];
    char *s;
    char *t;
    char *v;
    char *x;
    int option;
    int len;
    int i;
    t30_state_t fax;
    int calling_party;
    int verbose;
    int samples;

    struct stat statBuf;

    struct localuser *u;
    struct ast_frame *inf = NULL;
    struct ast_frame outf;

    int original_read_fmt;
    int original_write_fmt;
    
    uint8_t __buf[sizeof(uint16_t)*MAX_BLOCK_SIZE + 2*AST_FRIENDLY_OFFSET];
    uint8_t *buf = __buf + AST_FRIENDLY_OFFSET;

    if (chan == NULL)
    {
        ast_log(LOG_WARNING, "Fax receive channel is NULL. Giving up.\n");
        return -1;
    }

    span_set_message_handler(span_message);

    /* The next few lines of code parse out the filename and header from the input string */
    if (data == NULL)
    {
        /* No data implies no filename or anything is present */
        ast_log(LOG_WARNING, "Rxfax requires an argument (filename)\n");
        return -1;
    }
    
    calling_party = FALSE;
    verbose = FALSE;
    target_file[0] = '\0';

    for (option = 0, v = s = data;  v;  option++, s++)
    {
        t = s;
        v = strchr(s, '|');
        s = (v)  ?  v  :  s + strlen(s);
        strncpy((char *) buf, t, s - t);
        buf[s - t] = '\0';
        if (option == 0)
        {
            /* The first option is always the file name */
            len = s - t;
            if (len > 255)
                len = 255;
            strncpy(target_file, t, len);
            target_file[len] = '\0';
            /* Allow the use of %d in the file name for a wild card of sorts, to
               create a new file with the specified name scheme */
            if ((x = strchr(target_file, '%'))  &&  x[1] == 'd')
            {
                strcpy(template_file, target_file);
                i = 0;
                do
                {
                    snprintf(target_file, 256, template_file, 1);
                    i++;
                }
                while (ast_fileexists(target_file, "", chan->language) != -1);
            }
        }
        else if (strncmp("caller", t, s - t) == 0)
        {
            calling_party = TRUE;
        }
        else if (strncmp("debug", t, s - t) == 0)
        {
            verbose = TRUE;
        }
    }

    /* Done parsing */

    LOCAL_USER_ADD(u);

    if (chan->_state != AST_STATE_UP)
    {
        /* Shouldn't need this, but checking to see if channel is already answered
         * Theoretically asterisk should already have answered before running the app */
        res = ast_answer(chan);
    }
    
    if (!res)
    {
        original_read_fmt = chan->readformat;
        if (original_read_fmt != AST_FORMAT_SLINEAR)
        {
            res = ast_set_read_format(chan, AST_FORMAT_SLINEAR);
            if (res < 0)
            {
                ast_log(LOG_WARNING, "Unable to set to linear read mode, giving up\n");
                return -1;
            }
        }
        original_write_fmt = chan->writeformat;
        if (original_write_fmt != AST_FORMAT_SLINEAR)
        {
            res = ast_set_write_format(chan, AST_FORMAT_SLINEAR);
            if (res < 0)
            {
                ast_log(LOG_WARNING, "Unable to set to linear write mode, giving up\n");
                res = ast_set_read_format(chan, original_read_fmt);
                if (res)
                    ast_log(LOG_WARNING, "Unable to restore read format on '%s'\n", chan->name);
                return -1;
            }
        }
        fax_init(&fax, calling_party, NULL);
        if (verbose)
            fax.logging.level = SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW;
        x = pbx_builtin_getvar_helper(chan, "LOCALSTATIONID");
        if (x  &&  x[0])
            t30_set_local_ident(&fax, x);
        x = pbx_builtin_getvar_helper(chan, "LOCALHEADERINFO");
        if (x  &&  x[0])
            t30_set_header_info(&fax, x);
        t30_set_rx_file(&fax, target_file, -1);
        //t30_set_phase_b_handler(&fax, phase_b_handler, chan);
        t30_set_phase_d_handler(&fax, phase_d_handler, chan);
        t30_set_phase_e_handler(&fax, phase_e_handler, chan);
        while (ast_waitfor(chan, -1) > -1)
        {
            inf = ast_read(chan);
            if (inf == NULL)
            {
                res = -1;
                break;
            }
            if (inf->frametype == AST_FRAME_VOICE)
            {
                if (fax_rx(&fax, inf->data, inf->samples))
                    break;
                samples = (inf->samples <= MAX_BLOCK_SIZE)  ?  inf->samples  :  MAX_BLOCK_SIZE;
                len = fax_tx(&fax, (int16_t *) &buf[AST_FRIENDLY_OFFSET], samples);
                if (len)
                {
                    memset(&outf, 0, sizeof(outf));
                    outf.frametype = AST_FRAME_VOICE;
                    outf.subclass = AST_FORMAT_SLINEAR;
                    outf.datalen = len*sizeof(int16_t);
                    outf.samples = len;
                    outf.data = &buf[AST_FRIENDLY_OFFSET];
                    outf.offset = AST_FRIENDLY_OFFSET;
                    outf.src = "RxFAX";
                    if (ast_write(chan, &outf) < 0)
                    {
                        ast_log(LOG_WARNING, "Unable to write frame to channel; %s\n", strerror(errno));
                        break;
                    }
                }
            }
            ast_frfree(inf);
        }
        if (inf == NULL)
        {
            ast_log(LOG_DEBUG, "Got hangup\n");
            res = -1;
        }
        if (original_read_fmt && original_read_fmt != AST_FORMAT_SLINEAR)
        {
            res = ast_set_read_format(chan, original_read_fmt);
            if (res)
                ast_log(LOG_WARNING, "Unable to restore read format on '%s'\n", chan->name);
        }
        if (original_write_fmt && original_write_fmt != AST_FORMAT_SLINEAR)
        {
            res = ast_set_write_format(chan, original_write_fmt);
            if (res)
                ast_log(LOG_WARNING, "Unable to restore write format on '%s'\n", chan->name);
        }
        //fax_release(&fax);
    }
    else
    {
        ast_log(LOG_WARNING, "Could not answer channel '%s'\n", chan->name);
    }
    LOCAL_USER_REMOVE(u);

    if (stat(target_file, &statBuf) == 0 && statBuf.st_size) {
        /* Try to convert the TIFF to a PDF */
        strncpy(pdf_file, target_file, strlen(target_file) - 3);
        pdf_file[ (strlen(target_file) - 3) ] = '\0';
        strcat(pdf_file, "pdf");
        snprintf(cmd, 1024, "fax2pdf '%s' '%s'", target_file, pdf_file);
        ast_log(LOG_DEBUG, "Executing: %s\n", cmd);
        system(cmd);
        if (stat(pdf_file, &statBuf) == 0 && statBuf.st_size) {
            faxToMail(chan, pdf_file, "pdf", &fax);
            faxToDatabase(chan, pdf_file, "pdf", &fax);
            if (unlink(pdf_file)) {
                ast_log(LOG_WARNING, "Unable to unlink file '%s'\n", target_file);
            }
        } else {
            ast_log(LOG_WARNING, "TIF to PDF generation failed, sending the TIF image.\n");
            faxToMail(chan, target_file, "tif", &fax);
            faxToDatabase(chan, target_file, "tif", &fax);
        }
        fax_release(&fax);
        if (unlink(target_file)) {
            ast_log(LOG_WARNING, "Unable to unlink file '%s'\n", target_file);
        }
    } else {
        ast_log(LOG_WARNING, "A fax was not received.\n");
    }
    return res;
}
/*- End of function --------------------------------------------------------*/

static int my_unload_module(void)
{
	if (hostname)
		free(hostname);
	if (dbuser)
		free(dbuser);
	if (dbname)
		free(dbname);
	if (password)
		free(password);
	if (dbtable)
		free(dbtable);
	if (email_from)
		free(email_from);
	return 0;
}

static int my_load_module(void)
{
	struct ast_config *cfg;
	struct ast_variable *var;
	char *tmp;
	
	cfg = ast_config_load(config);
	if (!cfg) {
		ast_log(LOG_WARNING,
			"Unable to load configuration for rxfax: %s\n", config);
		return -1;
	}
	var = ast_variable_browse(cfg, "global");
	if (!var) {
		return -1;
	}
	
	if ((tmp=ast_variable_retrieve(cfg, "global", "dbhost"))) {
		hostname = strdup(tmp);
	} else {
		hostname = NULL;
	}
	if ((tmp=ast_variable_retrieve(cfg, "global", "dbuser"))) {
		dbuser = strdup(tmp);
	} else {
		dbuser = NULL;
	}
	if ((tmp=ast_variable_retrieve(cfg, "global", "dbpass"))) {
		password = strdup(tmp);
	} else {
		password = NULL;
	}
	if ((tmp=ast_variable_retrieve(cfg, "global", "dbname"))) {
		dbname = strdup(tmp);
	} else {
		dbname = NULL;
	}
	if ((tmp=ast_variable_retrieve(cfg, "global", "dbtable"))) {
		dbtable = strdup(tmp);
	} else {
		dbtable = NULL;
	}
	if ((tmp=ast_variable_retrieve(cfg, "global", "email_from"))) {
		email_from = strdup(tmp);
	} else {
		email_from = NULL;
	}
	if ((tmp=ast_variable_retrieve(cfg, "global", "dbport"))) {
		if (sscanf(tmp,"%d", &dbport) < 1) {
			dbport = 0;
		}
	}
	if ((tmp=ast_variable_retrieve(cfg, "global", "timeout"))) {
		if (sscanf(tmp,"%d", &timeout) < 1) {
			timeout = 0;
		}
	}
	ast_config_destroy(cfg);
	
	ast_log(LOG_DEBUG, "got hostname of %s\n", hostname);
	ast_log(LOG_DEBUG, "got port of %d\n", dbport);
	ast_log(LOG_DEBUG, "got a timeout of %d\n", timeout);
	ast_log(LOG_DEBUG, "got user of %s\n", dbuser);
	ast_log(LOG_DEBUG, "got dbname of %s\n", dbname);
	ast_log(LOG_DEBUG, "got password of %s\n", password);
	ast_log(LOG_DEBUG, "got table of %s\n", dbtable);
	ast_log(LOG_DEBUG, "got email_from of %s\n", email_from);
	return 0;
}

int unload_module(void)
{
	my_unload_module();
    STANDARD_HANGUP_LOCALUSERS;
    return ast_unregister_application(app);
}
/*- End of function --------------------------------------------------------*/

int load_module(void)
{
	my_load_module();
    return ast_register_application(app, rxfax_exec, synopsis, descrip);
}

char *description(void)
{
    return tdesc;
}
/*- End of function --------------------------------------------------------*/

int usecount(void)
{
    int res;
    STANDARD_USECOUNT(res);
    return res;
}
/*- End of function --------------------------------------------------------*/

char *key(void)
{
    return ASTERISK_GPL_KEY;
}
/*- End of function --------------------------------------------------------*/
/*- End of file ------------------------------------------------------------*/
