/*
 *   cddbd - CD Database Protocol Server
 *
 *   Copyright (C) 1996-1997  Steve Scherf (steve@moonsoft.com)
 *   Portions Copyright (C) 2001-2006  by various authors
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef LINT
static char *const _sites_c_ident_ = "@(#)$Id: xmit.c,v 1.18.2.3 2006/04/18 22:10:24 joerg78 Exp $";
#endif

#include <stdlib.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include "access.h"
#include "cddbd.h"


/* Preprocessor definitions. */

/* Sites file record type. */
#define SRT_CDDBP	0
#define SRT_SMTP	1
#define SRT_INFO	2
#define SRT_GROUP	3


/* Structures. */

typedef struct tfile {
	int tf_dir;
	unsigned int tf_discid;
} tfile_t;


/* Prototypes. */

void cddbd_catchup(char *, char *);
void cddbd_do_catchup(char *, char *);
void cddbd_do_put(csite_t *, char *);
void cddbd_do_remote_cmd(csite_t *, char *, char *);
void cddbd_do_remote_log(csite_t *);
void cddbd_do_transmit(csite_t *);
void cddbd_put(char *, char *);
void cddbd_remote_cmd(char *, char *, char *);
void cddbd_remote_log(char *);
void cddbd_transmit(char *);
void cddbd_write_history(char *, unsigned int);
void getsitemark(void);
void gfree(void *);
void setsitemark(void);
void tfree(void *);

int cddbd_clean_history(void);
int cddbd_do_merge_ulist(char *, int);
int cddbd_oc_history(char *, int);
int cddbd_open_history(char *);
int cddbd_open_ulist(void);
int cddbd_put_file(char *, char *, char *);
int cddbd_read_history(char *, unsigned int *);
int parse_coord(char *, coord_t *);
int grp_comp(void *, void *);
int tfile_comp(void *, void *);


/* Global variables. */

FILE *site_fp = NULL;
FILE *ulist_fp = NULL;

char *histstr = "Start %s\n";
char *uhist;

int hist_opened;
int site_line;

long sitemark = -1;

lhead_t *hist;
lhead_t *group;


csite_t *
getsitenam(char *site, int type)
{
	csite_t *sp;

	endsitegrp();
	setsiteent();

	while((sp = getsiteent(type)) != NULL)
		if(!cddbd_strcasecmp(site, sp->st_name))
			return sp;

	return NULL;
}


csite_t *
getsiteent(int type)
{
	int i;
	int ind;
	int srt;
	arg_t args;
	char *s;
	char buf[CDDBBUFSIZ];
	static csite_t site;

	if(site_fp == NULL) {
		setsiteent();

		if(site_fp == NULL)
			return NULL;
	}

	while(fgets(args.buf, sizeof(args.buf), site_fp) != NULL) {
		site_line++;

		/* Skip comments and blanks. */
		if(args.buf[0] == '#' || is_blank(args.buf, 0))
			continue;

		cddbd_parse_args(&args, 0);

		if(args.nargs < 2) {
			cddbd_log(LOG_ERR, "Syntax error in %s on line %d.",
			    sitefile, site_line);

			continue;
		}

		cddbd_bzero((char *)&site, sizeof(site));
		strncpy(site.st_name, args.arg[0], sizeof(site.st_name));
		site.st_name[sizeof(site.st_name) - 1] = '\0';
		site.st_port = 0;

		if(!cddbd_strcasecmp(args.arg[1], "cddbp"))
			srt = SRT_CDDBP;
		else if(!cddbd_strcasecmp(args.arg[1], "smtp"))
			srt = SRT_SMTP;
		else if(!cddbd_strcasecmp(args.arg[1], "info"))
			srt = SRT_INFO;
		else if(!cddbd_strcasecmp(args.arg[1], "group"))
			srt = SRT_GROUP;
		else {
			cddbd_log(LOG_ERR,
			    "Unknown site record type \"%s\" in %s on line %d.",
			    args.arg[1], sitefile, site_line);

			continue;
		}

		/* If we have a group filter, make sure the site is included. */
		if(type != SITE_GROUP && group != 0 &&
		    list_find(group, (void *)site.st_name) == 0)
			continue;

		switch(type) {
		case SITE_XMIT:
			if(srt == SRT_CDDBP)
				site.st_proto = ST_PROTO_CDDBP;
			else if(srt == SRT_SMTP)
				site.st_proto = ST_PROTO_SMTP;
			else
				continue;

			break;

		case SITE_LOG:
			if(srt == SRT_CDDBP)
				site.st_proto = ST_PROTO_CDDBP;
			else
				continue;

			break;

		case SITE_INFO:
			if(srt == SRT_INFO)
				site.st_proto = ST_PROTO_NONE;
			else
				continue;

			break;

		case SITE_GROUP:
			if(srt == SRT_GROUP)
				site.st_proto = ST_PROTO_GROUP;
			else
				continue;

			break;

		default:
			cddbd_log(LOG_ERR,
			    "Internal error: unknown site type: %s.", type);
			continue;
		}
			

		switch(site.st_proto) {
		case ST_PROTO_CDDBP:
		case ST_PROTO_SMTP:
			if(args.nargs > 5) {
				cddbd_log(LOG_ERR,
				    "Syntax error in %s on line %d.",
				    sitefile, site_line);
				continue;
			}

			site.st_port = 0;
			site.st_flags = 0;

			/* Get the port #. */
			switch(site.st_proto) {
			case ST_PROTO_CDDBP:
				if(args.nargs > 2) {
					if(strcmp(args.arg[2], "-")) {
						site.st_port =
						    atoi(args.arg[2]);
					}

					if(site.st_port < 0) {
						cddbd_log(LOG_ERR,
						    "Invalid port in %s on "
						    "line %d.", sitefile,
						    site_line);

						continue;
					}
				}

				break;

			case ST_PROTO_SMTP:
				if(args.nargs < 3) {
					cddbd_log(LOG_ERR,
					    "Missing address in %s on line %d.",
					    sitefile, site_line);

					continue;
				}

				strncpy(site.st_addr, args.arg[2],
				    sizeof(site.st_addr));

				break;

			default:
				break;
			}

			/* Get the flags. */
			if(args.nargs > 3 && strcmp(args.arg[3], "-")) {
				for(s = args.arg[3]; *s != '\0'; s++) {
					switch(*s) {
					case 't':
						site.st_flags |= ST_FLAG_NOXMIT;
						break;

					case 'm':
						site.st_flags |= ST_FLAG_OMIME;
						break;
						
					case 'i':
						site.st_flags |= ST_FLAG_ONLYISO;
						break;
					
					case 'f':
					site.st_flags |= ST_FLAG_OFIELDS;
						break;

					default:
						break;
					}
				}

				if(*s != '\0') {
					cddbd_log(LOG_ERR,
					    "Invalid flag '%c' in %s on line "
					    "%d.", *s, sitefile, site_line);

					continue;
				}
			}

			/* Get the password label. */
			if(args.nargs > 4) {
				site.st_flags |= ST_FLAG_PWDLBL;
				cddbd_snprintf(site.st_pwdlbl,
				    sizeof(site.st_pwdlbl), "%s", args.arg[4]);
			}

			break;

		case ST_PROTO_GROUP:
			if(args.nargs < 3) {
				cddbd_log(LOG_ERR,
				    "Syntax error in %s on line %d.",
				    sitefile, site_line);
				continue;
			}

			if(group == 0) {
				cddbd_log(LOG_ERR, "Internal error: no group"
				    "list in group search.");
				quit(1);
			}

			/* This isn't the group we're looking for. */
			if(strcmp((char *)group->lh_data, site.st_name))
				continue;

			for(i = 2; i < args.nargs; i++) {
				if(list_find(group, (void *)args.arg[i]) != 0) {
					cddbd_log(LOG_ERR, "Site \"%s\" "
					    "multiply defined in group \"%s\""
					    " in %s on line %d.", args.arg[i],
					    (void *)group->lh_data, sitefile,
					    site_line);
					quit(1);
				}

				s = strdup(args.arg[i]);
				if(s == NULL) {
					cddbd_log(LOG_ERR, "Failed to allocate"
					    "group list string.");
					quit(1);
				}

				if(list_add_cur(group, (void *)s) == 0) {
					cddbd_log(LOG_ERR, "Failed to allocate"
					    "group list entry.");
					quit(1);
				}
			}

			break;

		case ST_PROTO_NONE:
			if(args.nargs < 8) {
				cddbd_log(LOG_ERR,
				    "Syntax error in %s on line %d.",
				    sitefile, site_line);
				continue;
			}

			if((ind = get_serv_index(args.arg[2])) < 0) {
				cddbd_log(LOG_ERR,
				    "Invalid service %s in %s on line %d.",
				    args.arg[2], sitefile, site_line);

				continue;
			}

			site.st_proto = ind;

			strncpy(site.st_pname, args.arg[2],
			    (sizeof(site.st_pname) - 1));
			site.st_pname[strlen(site.st_pname)] = '\0';

			if(cddbd_strcasecmp(args.arg[3], "-")) {
				site.st_port = atoi(args.arg[3]);

				if(site.st_port <= 0) {
					cddbd_log(LOG_ERR,
					    "Invalid port in %s on line %d.",
					    sitefile, site_line);

					continue;
				}
			}
			else
				site.st_port = ntohs(get_serv_port(0, ind));

			strncpy(site.st_addr, args.arg[4],
			    (sizeof(site.st_addr) - 1));
			site.st_addr[strlen(site.st_addr)] = '\0';

			strncpy(buf, args.arg[5], (sizeof(buf) - 1));
			buf[strlen(buf)] = '\0';

			if(!parse_coord(buf, &site.st_lat)) {
				cddbd_log(LOG_ERR,
				    "Malformed coordinate in %s on line %d.",
				    sitefile, site_line);
				continue;
			}

			strncpy(buf, args.arg[6], (sizeof(buf) - 1));
			buf[strlen(buf)] = '\0';

			if(!parse_coord(buf, &site.st_long)) {
				cddbd_log(LOG_ERR,
				    "Malformed coordinate in %s on line %d.",
				    sitefile, site_line);
				continue;
			}

			site.st_desc[0] = '\0';

			for(i = 7; i < args.nargs; i++) {
				if(strlen(site.st_desc) + strlen(args.arg[i])
				    + 2 > sizeof(site.st_desc))
					break;

				strcat(site.st_desc, args.arg[i]);

				if(i != (args.nargs - 1))
					strcat(site.st_desc, " ");
			}

			break;

		default:
			cddbd_log(LOG_ERR,
			    "Internal error: impossible default case.");
			break;
		}

		return(&site);
	}

	return NULL;
}


void
endsiteent(void)
{
	if(site_fp != NULL) {
		fclose(site_fp);
		site_fp = NULL;
	}
}


void
setsiteent(void)
{
	if(site_fp == NULL) {
		if(sitefile[0] == '\0')
			return;
		if((site_fp = fopen(sitefile, "r")) == NULL)
			return;
	}
	else
		rewind(site_fp);

	site_line = 0;
}


int
setsitegrp(char *grp)
{
	int found;

	endsitegrp();
	setsiteent();

	group = list_init(grp, grp_comp, gfree, 0);
	if(group == 0) {
		cddbd_log(LOG_ERR, "Failed to allocate list head.");
		quit(1);
	}

	found = 0;

	while(getsiteent(SITE_GROUP) != NULL)
		found++;

	if(!found)
		endsitegrp();

	setsiteent();

	return found;
}


void
setsitemark(void)
{
	if((site_fp != NULL) && (sitemark != -1))
		(void)fseek(site_fp, sitemark, SEEK_SET);
}


void
getsitemark(void)
{
	if(site_fp == NULL)
		return;

	sitemark = ftell(site_fp);
}


void
endsitegrp(void)
{
	if(group != 0) {
		list_free(group);
		group = 0;
	}
}


void
gfree(void *p)
{
	free(p);
}


int
grp_comp(void *g1, void *g2)
{
	return(strcmp((char *)g1, (char *)g2));
}


void
tfree(void *p)
{
	free(p);
}


int
tfile_comp(void *t1, void *t2)
{
	int x;

	x = ((tfile_t *)t1)->tf_dir - ((tfile_t *)t2)->tf_dir;

	if(x != 0)
		return x;

	if(((tfile_t *)t1)->tf_discid > ((tfile_t *)t2)->tf_discid)
		return 1;

	if(((tfile_t *)t1)->tf_discid < ((tfile_t *)t2)->tf_discid)
		return -1;

	return 0;
}


void
cddbd_rmt_op(int op, char *site, char *arg, char *arg2)
{
	int type;

	/* T2D: check arguments */
	switch(op) {
	case RMT_OP_LOG:
		type = SITE_LOG;
		break;

	default:
		type = SITE_XMIT;
		break;
	}

	if(strcmp(site, "all") && !setsitegrp(site) &&
	    (getsitenam(site, type) == NULL)) {
		cddbd_log(LOG_ERR,
		    "Site \"%s\" is not defined in %s.", site, sitefile);
		return;
	}

	switch(op) {
	case RMT_OP_LOG:
		cddbd_remote_log(site);
		break;

	case RMT_OP_CMD:
		cddbd_remote_cmd(site, arg, arg2);
		break;

	case RMT_OP_CATCHUP:
		(void)cddbd_clean_history();
		endsiteent();
		cddbd_catchup(site, arg);

		break;

	case RMT_OP_PUT:
		cddbd_put(site, arg);
		break;

	case RMT_OP_TRANSMIT:
		(void)cddbd_clean_history();
		endsiteent();
		cddbd_transmit(site);

		break;

	default:
		cddbd_log(LOG_ERR,
		    "Internal server error: unknown remote op: %d.", op);
		quit(QUIT_ERR);
	}
}


void
cddbd_transmit(char *site)
{
	pid_t f;
	int xcnt;
	csite_t *sp;

	if(strcmp(site, "all") && !setsitegrp(site)) {
		if((sp = getsitenam(site, SITE_XMIT)) != NULL)
			cddbd_do_transmit(sp);
	}
	else {
		setsiteent();

		for(xcnt = 0;;) {
			if((sp = getsiteent(SITE_XMIT)) == NULL)
				break;

			if(max_xmits > 1) {
				if(xcnt >= max_xmits) {
					while(wait(0) < 0 && errno == EINTR)
						continue;
					xcnt--;
				}

				f = cddbd_fork();

				if(f < 0) {
					cddbd_log(LOG_ERR | LOG_XMIT,
					    "Can't fork child for xmit (%d).",
					    errno);

					return;
				}

				/* The child does the transmit. */
				if(f == 0) {
					cddbd_do_transmit(sp);
					quit(QUIT_OK);
				}

				xcnt++;
			}
			else
				cddbd_do_transmit(sp);
		}

		endsitegrp();
		endsiteent();

		while(xcnt > 0) {
			while(wait(0) < 0 && errno == EINTR)
				continue;
			xcnt--;
		}
	}
}


void
cddbd_do_transmit(csite_t *sp)
{
	int bps;
	int xmit;
	int fail;
	int tcnt;
	int files;
	int use_proto = 1;
	int db_enc=-1;
	int flags= MF_NONE;
	int dbflags;
	db_t *db;
	time_t xtime;
	FILE *fp;
	char *tmail;
	FILE *tfp= (FILE *) 0;
	lhead_t *li;
	ct_key_t *key;
	struct stat sbuf;
	unsigned int discid;
	char dir[CDDBBUFSIZ];
	char file[CDDBBUFSIZ];
	char subj[CDDBBUFSIZ];
	char errstr[CDDBBUFSIZ];
	char xmit_email[CDDBBUFSIZ];
	
	
	if(sp->st_flags & ST_FLAG_NOXMIT) {
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Remote host %s is not configured for transmits.",
		    sp->st_name);
		return;
	}

	if(!cddbd_open_history(sp->st_name)) {
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Failed to open history file %s.", servfile[SF_HIST]);
		return;
	}

	files = list_count(hist);

	if(files > 0) {
		cddbd_log(LOG_INFO, "Beginning file transmission to %s.",
		    sp->st_name);
	}
	else {
		if(verbose)
			cddbd_log(LOG_INFO, "Remote host %s is up to date.",
			    sp->st_name);
		return;
	}

	key = 0;

	switch(sp->st_proto) {
	case ST_PROTO_CDDBP:
		if(!cddbp_open(sp)) {
			cddbd_log(LOG_ERR | LOG_XMIT,
			    "Failed to open %s for file transmission.",
			    sp->st_name);

			if(!cddbd_close_history()) {
				cddbd_log(LOG_ERR | LOG_XMIT,
				    "Failed to update the history for: %s.",
				    sp->st_name);
			}

			return;
		}
		if (!(use_proto=cddbp_setproto())) {
			cddbd_log(LOG_ERR | LOG_XMIT,
			    "Failed to determine and set protocol level on %s for file transmission.",
			    sp->st_name);

			if(!cddbd_close_history()) {
				cddbd_log(LOG_ERR | LOG_XMIT,
				    "Failed to update the history for: %s.",
				    sp->st_name);
			}
			return;
		}

		break;

	case ST_PROTO_SMTP:
		if(sp->st_flags & ST_FLAG_PWDLBL) {
			if((key = getpasswd(sp->st_pwdlbl)) == NULL) {
				cddbd_log(LOG_ERR | LOG_XMIT, "No password "
				    "label \"%s\" found for site: %s",
				    sp->st_pwdlbl, sp->st_name);

				return;
			}
		}
		/* This is for supporting older sites. */
		if(sp->st_flags & ST_FLAG_OMIME)
			flags = MF_ENC;
		else
			flags = MF_MULTI;

		if(!smtp_open()) {
			cddbd_log(LOG_ERR | LOG_XMIT,
			    "Failed to open SMTP for file transmission to %s.",
			    sp->st_name);

			if(!cddbd_close_history()) {
				cddbd_log(LOG_ERR | LOG_XMIT,
				    "Failed to update the history for: %s.",
				    sp->st_name);
			}

			return;
		}

		cddbd_snprintf(xmit_email, sizeof(xmit_email), "%s@%s",
		    sp->st_addr, sp->st_name);

		break;

	default:
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Internal error: unknown transmit protocol (%d).",
		    sp->st_proto);

		if(!cddbd_close_history()) {
			cddbd_log(LOG_ERR | LOG_XMIT,
			    "Failed to update the history for: %s.",
			    sp->st_name);
		}

		quit(QUIT_ERR);
	}

	li = list_init(0, 0, 0, 0);
	if(li == 0) {
		cddbd_log(LOG_ERR | LOG_XMIT, "Can't malloc linked list.");

		if(!cddbd_close_history()) {
			cddbd_log(LOG_ERR | LOG_XMIT,
			    "Failed to update the history for: %s.",
			    sp->st_name);
		}

		quit(QUIT_ERR);
	}

	xmit = 0;
	fail = 0;
	tcnt = 0;
	xtime = time(0);

	while(cddbd_read_history(dir, &discid)) {
		cddbd_snprintf(file, sizeof(file), "%s/%s/%08x", cddbdir,
		    dir, discid);

		if(stat(file, &sbuf) != 0) {
			cddbd_log(LOG_ERR | LOG_XMIT,
			    "Can't stat DB file: %s (%d).", file, errno);
			continue;
		}

		/* Don't transmit the same file twice. */
		if(list_find(li, ino_t_as_ptr (sbuf.st_ino)) != 0) {
			files--;
			continue;
		}

		if(list_add_cur(li, ino_t_as_ptr (sbuf.st_ino)) == 0) {
			cddbd_log(LOG_ERR, "Can't malloc linked list entry.");

			if(!cddbd_close_history()) {
				cddbd_log(LOG_ERR | LOG_XMIT,
				    "Failed to update the history for: %s.",
				    sp->st_name);
			}

			quit(QUIT_ERR);
		}

#ifdef DB_WINDOWS_FORMAT
		dbflags = file_df_flags | DF_WIN;
		if((fp = db_prepare_unix_read(file)) != NULL) {
#else
		dbflags = file_df_flags;
		if((fp = fopen(file, "r")) == NULL) {
#endif
			cddbd_log(LOG_ERR | LOG_XMIT,
			    "Transmission failed for %s %08x: No such CD entry in database.", dir, discid);
			continue;
		}

		db = db_read(fp, errstr, dbflags);
		fclose(fp);

		if(db == 0) {
			switch(db_errno) {
			case DE_INVALID:
				cddbd_log(LOG_ERR | LOG_XMIT,
				    "Invalid DB file: %s: %s.",
				    file, errstr);

				break;

			case DE_FILE:
			default:
				cddbd_log(LOG_ERR | LOG_XMIT,
				    "Can't read DB file: %s: %s (%d).",
				    file, errstr, errno);

				break;
			}

			continue;
		}

		switch(sp->st_proto) {
		case ST_PROTO_CDDBP:
			switch(cddbp_transmit(db, dir, discid, use_proto)) {
			case -1:
				/* DB file was corrupt. */
				break;

			case 0:
				/* Something wrong with server. */
				fail++;
				break;

			default:
				/* File transferred. */
				xmit++;
				tcnt += sbuf.st_size;
				break;
			}

			break;

		case ST_PROTO_SMTP:

			cddbd_snprintf(subj, sizeof(subj), "cddb %s %08x",
			    dir, discid);
			
			/**************************************************/
			/* check whether the remote server supports utf-8 */
			/* (this is set via a flag in the sites-file),    */
			/* convert charset accordingly and set db_enc     */
			/* variable (so the email can get the correct     */
			/* charset-header later                           */
			/**************************************************/
			if(sp->st_flags & ST_FLAG_ONLYISO) {
				convert_db_charset_proto(db, UNICODE_LEVEL-1);
				if (db->db_flags & DB_ENC_ASCII)
					db_enc=CC_US_ASCII;
				else if (db->db_flags & DB_ENC_LATIN1)
					db_enc=CC_ISO_8859;
				else db_enc=-1;
			}
			else {
				convert_db_charset_proto(db, UNICODE_LEVEL);
				db_enc=CC_UTF_8;
			}
			
			/**********************************************/
			/* set use_proto depending on whether or not  */
			/* we may send the DYEAR and DGENRE lines     */
			/* (this is set via a flag in the sites file) */
			/**********************************************/
			if(sp->st_flags & ST_FLAG_OFIELDS)
				use_proto=4;
			else
				use_proto=MAX_PROTO_LEVEL;
			
			tmail = cddbd_mktemp();
			
			/*********************************************/
			/* Write the database entry to a tmp-file    */
			/* so we can send it via email               */
			/* (smtp_transmits needs the entry in a file */
			/*********************************************/
			if((tfp = fopen(tmail, "w+")) == NULL) {
				cddbd_log(LOG_ERR | LOG_XMIT,
		    	"Can't open mail tmp file %s (%d)", tmail, errno);
		    	fail++;
		    	break;
			}
			
#ifdef DB_WINDOWS_FORMAT
			if(db_write(tfp, db, use_proto, 0) == 0) {
#else
			if(db_write(tfp, db, use_proto) == 0) {
#endif
				cddbd_log(LOG_ERR | LOG_XMIT,
				    "Can't write mail tmp file: %s", tmail);
				fclose(tfp);
				unlink(tmail);
				break;
			}
			/* Put the pointer back to the start of the file for reading. */
			rewind(tfp);
			
			/* try to send the transmission email */
			if(!smtp_transmit(tfp, db_enc, subj, xmit_email, xmit_email,
			    admin_email, 0, flags, (int) sbuf.st_size, key))
				fail++;
			else {
				xmit++;
				tcnt += sbuf.st_size;

				/* Be nice, wait an arbitrary amount of time. */
				if(email_time > 0)
					cddbd_delay(email_time);
			}
			if (tfp != (FILE *) 0) fclose(tfp);
			cddbd_freetemp(tmail);
			
			break;

		default:
			cddbd_log(LOG_ERR | LOG_XMIT,
			    "Internal error: unknown transmit protocol (%d).",
			    sp->st_proto);

			if(!cddbd_close_history()) {
				cddbd_log(LOG_ERR | LOG_XMIT,
				    "Failed to update the history for: %s.",
				    sp->st_name);
			}

			quit(QUIT_ERR);
		}

		db_free(db);

		if(fail) {
			cddbd_log(LOG_ERR | LOG_XMIT,
			    "Failed to transmit DB file: %s/%08x.",
			    dir, discid);

			/* Put the entry back in the history list. */
			cddbd_write_history(dir, discid);

			break;
		}
	}

	list_free(li);

	xtime = time(0) - xtime;
	if(xtime == 0)
		xtime = 1;

	if(fail) {
		cddbd_log(LOG_ERR | LOG_XMIT, "Aborting transmit to %s.",
		    sp->st_name);
	}
	else
		cddbd_log(LOG_INFO, "Completed transmit to %s.", sp->st_name);

	bps = tcnt / xtime;

	cddbd_log(LOG_INFO,
	    "Transmitted %d of %d files, %d bytes in %d sec (%d.%dK/sec).",
	    xmit, files, tcnt, xtime, (int)(bps / 1024.0),
	    (int)((bps % 1024) / 1024.0 * 10));

	switch(sp->st_proto) {
	case ST_PROTO_CDDBP:
		cddbp_close();
		break;

	case ST_PROTO_SMTP:
		smtp_close();
		break;

	default:
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Internal error: unknown transmit protocol (%d).",
		    sp->st_proto);

		if(!cddbd_close_history()) {
			cddbd_log(LOG_ERR | LOG_XMIT,
			    "Failed to update the history for: %s.",
			    sp->st_name);
		}

		quit(QUIT_ERR);
	}

	if(!cddbd_close_history()) {
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Failed to update the history for: %s.", sp->st_name);
	}
}


void
cddbd_remote_log(char *site)
{
	int first;
	csite_t *sp;

	if(strcmp(site, "all") && !setsitegrp(site)) {
		if((sp = getsitenam(site, SITE_LOG)) != NULL &&
		    sp->st_proto == ST_PROTO_CDDBP)
			cddbd_do_remote_log(sp);

		return;
	}

	for(first = 1;; first = 0) {
		if((sp = getsiteent(SITE_LOG)) == NULL)
			break;

		if(sp->st_proto != ST_PROTO_CDDBP)
			continue;

		if(!first)
			printf("\n\n");

		cddbd_do_remote_log(sp);
	}

	endsitegrp();
	endsiteent();
}


void
cddbd_do_remote_log(csite_t *sp)
{
	printf("Log statistics for %s:\n\n", sp->st_name);

	if(!cddbp_open(sp)) {
		printf("Unknown host %s or host unavailable.\n", sp->st_name);
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Failed to open %s for log acquisition.", sp->st_name);

		return;
	}

	if(!cddbp_log_stats())
		printf("Unable to get log stats.\n");

	cddbp_close();
}




void
cddbd_remote_cmd(char *site, char *rcmd, char *plev)
{
	int first;
	csite_t *sp;

	if(strcmp(site, "all") && !setsitegrp(site)) {
		if((sp = getsitenam(site, SITE_XMIT)) != NULL)
			cddbd_do_remote_cmd(sp, rcmd, plev);

		return;
	}

	for(first = 1;; first = 0) {
		if((sp = getsiteent(SITE_XMIT)) == NULL)
			break;

		if(!first)
			printf("\n\n");

		cddbd_do_remote_cmd(sp, rcmd, plev);
	}

	endsitegrp();
	endsiteent();
}


void
cddbd_do_remote_cmd(csite_t *sp, char *rarg, char *plev)
{
	int lev;
	char xmit_email[CDDBBUFSIZ];

	if(verbose)
		printf("Issuing command to %s:\n\n", sp->st_name);

	if(strcmp(plev, "default"))
		lev = atoi(plev);
	else
		lev = DEF_PROTO_LEVEL;

	switch(sp->st_proto) {
	case ST_PROTO_CDDBP:
		if(!cddbp_open(sp)) {
			printf("Unknown host %s or host unavailable.\n",
			    sp->st_name);
			return;
		}

		if(!cddbp_command(rarg, lev))
			printf("Unable to issue command.\n");

		cddbp_close();

		break;

	case ST_PROTO_SMTP:
		if(!smtp_open()) {
			printf("Unknown host %s or host unavailable.\n",
			    sp->st_name);
			return;
		}

		cddbd_snprintf(xmit_email, sizeof(xmit_email), "%s@%s",
		    sp->st_addr, sp->st_name);

		if(!smtp_command(sp, rarg, lev, xmit_email, admin_email))
			printf("Unable to issue command.\n");

		smtp_close();

		printf("Email command sent to %s, return addr %s.\n",
		    xmit_email, admin_email);

		break;

	default:
		printf("Unsupported command protocol.\n");
		break;
	}
}


void
cddbd_put(char *site, char *file)
{
	csite_t *sp;

	if(strcmp(site, "all") && !setsitegrp(site)) {
		if((sp = getsitenam(site, SITE_XMIT)) != NULL &&
		    sp->st_proto == ST_PROTO_CDDBP)
			cddbd_do_put(sp, file);

		return;
	}

	for(;;) {
		if((sp = getsiteent(SITE_XMIT)) == NULL)
			break;

		if(sp->st_proto != ST_PROTO_CDDBP)
			continue;

		cddbd_do_put(sp, file);
	}

	endsitegrp();
	endsiteent();
}


void
cddbd_do_put(csite_t *sp, char *file)
{
	if(!cddbp_open(sp)) {
		cddbd_log(LOG_ERR | LOG_XMIT, "Failed to open %s for putting.",
		    sp->st_name);

		return;
	}

	(void)cddbd_put_file(sp->st_name, workdir, file);
	cddbp_close();
}


int
cddbd_put_file(char *site, char *dir, char *file)
{
	int ret;
	FILE *fp;
	DIR *dirp;
	struct dirent *dp;
	struct stat sbuf;
	char pfile[CDDBBUFSIZ];
	char *bfile;

	cddbd_snprintf(pfile, sizeof(pfile), "%s/%s", dir, file);

	if(stat(pfile, &sbuf)) {
		cddbd_log(LOG_ERR | LOG_XMIT, "Can't stat file: %s (%d).",
		    pfile, errno);

		return 0;
	}

	if(S_ISDIR(sbuf.st_mode)) {
		if((dirp = opendir(pfile)) == NULL) {
			cddbd_log(LOG_ERR | LOG_XMIT,
			    "Can't open directory: %s (%d).", pfile, errno);
		}

		while((dp = readdir(dirp)) != NULL) {
			if(is_parent_dir(dp->d_name))
				continue;

			/* Transmit, and stop if there was a server error. */
			if(cddbd_put_file(site, pfile, dp->d_name) == 0)
				break;
		}

		closedir(dirp);

		ret = 1;
	}
	else if(S_ISREG(sbuf.st_mode)) {
		cddbd_log(LOG_INFO, "Putting %s on %s.", pfile, site);

		if((fp = fopen(pfile, "r")) == NULL) {
			cddbd_log(LOG_ERR | LOG_XMIT,
			    "Can't open file for reading: %s (%d)",
			    pfile, errno);

			ret = -1;
		}
		else {
			if((bfile = strrchr(file, '/')) == NULL)
				bfile = file;

			ret = cddbp_put(fp, bfile);
			fclose(fp);
		}
	}
	else {
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Can't put %s on %s: not a regular file", pfile, site);

		ret = -1;
	}

	return ret;
}


void
cddbd_catchup(char *site, char *when)
{
	csite_t *sp;
	csite_t sitebuf;

	if(strcmp(site, "all") && !setsitegrp(site)) {
		cddbd_do_catchup(site, when);
		return;
	}

	/* Catching up uses getsiteent, so be careful here. */
	setsiteent();

	for(;;) {
		if((sp = getsiteent(SITE_XMIT)) == NULL)
			break;

		sitebuf = *sp;
		cddbd_do_catchup(sitebuf.st_name, when);

		if(getsitenam(sitebuf.st_name, SITE_XMIT) == NULL)
			break;
	}

	endsitegrp();
	endsiteent();
}


void
cddbd_do_catchup(char *site, char *when)
{
	int i;
	time_t t;
	DIR *dirp;
	struct dirent *dp;
	csite_t *sp;
	struct stat sbuf;
	char buf[CDDBBUFSIZ];
	char cdir[CDDBBUFSIZ];
	char file[CDDBBUFSIZ];

	if((sp = getsitenam(site, SITE_XMIT)) != NULL &&
	    (sp->st_flags & ST_FLAG_NOXMIT)) {
		cddbd_log(LOG_ERR,
		    "Remote host %s is not configured for transmits.",
		    sp->st_name);
		return;
	}

	/* Don't scan the database if they want to catch everything up. */
	if(!strcmp("now", when)) {
		t = time(0);
		strcpy(buf, make_time2(localtime(&t))); 

		cddbd_log(LOG_INFO,
		    "Resetting transmit history for %s to %s.", site, buf);

		if(!cddbd_open_ulist()) {
			cddbd_log(LOG_ERR, "Failed to update history for %s.",
			    site);
			quit(QUIT_ERR);
		}
	}
	else {
		strcpy(buf, make_time2(date_to_tm(when))); 

		cddbd_log(LOG_INFO,
		    "Resetting transmit history for %s to %s.", site, buf);

		for(i = 0; categlist[i] != 0; i++) {
			cddbd_snprintf(cdir, sizeof(cdir), "%s/%s", cddbdir,
			    categlist[i]);

			if((dirp = opendir(cdir)) == NULL)
				continue;

			while((dp = readdir(dirp)) != NULL) {
				cddbd_snprintf(buf, sizeof(buf), "%s/%s", cdir,
				    dp->d_name);

				/* Make sure this is a database file. */
				if(strlen(dp->d_name) != CDDBDISCIDLEN) {
					if(!is_parent_dir(dp->d_name)) {
						cddbd_log(LOG_INFO,
						    "Non-CDDB file: %s", file);
					}

					continue;
				}

				if(stat(buf, &sbuf)) {
					cddbd_log(LOG_ERR,
					    "Can't stat DB file: %s (%d).",
					    file, errno);

					continue;
				}

				/* If the file is too old, continue. */
				cvt_time(sbuf.st_mtime, buf);
				if(strcmp(when, buf) > 0)
					continue;

				if(!cddbd_write_ulist(categlist[i],
				    dp->d_name)) {
					cddbd_log(LOG_ERR,
					    "Failed to update history for %s.",
					    site);
					quit(QUIT_ERR);
				}
			}

			closedir(dirp);
		}
	}

	if(!cddbd_merge_ulist(site, 0)) {
		cddbd_log(LOG_ERR, "Failed to update history for %s.", site);
		quit(QUIT_ERR);
	}
}


int
cddbd_oc_history(char *name, int oflag)
{
	int dir;
	int insite;
	char *p;
	FILE *fp;
	FILE *tfp;
	link_t *lp;
	tfile_t *tf;
	unsigned int discid;
	char buf[CDDBBUFSIZ];
	char buf2[CDDBBUFSIZ];
	char *thist;

	/* Don't open if we already have, or close if we're not open. */
	if(oflag && hist_opened)
		return 0;

	if(!oflag && !hist_opened)
		return 0;

	(void)cddbd_lock(locks[LK_HIST], 1);

	close(open(servfile[SF_HIST], O_WRONLY | O_CREAT, file_mode));

	(void)cddbd_fix_file(servfile[SF_HIST], file_mode, uid, gid);

	if((fp = fopen(servfile[SF_HIST], "r")) == NULL) {
		cddbd_log(LOG_ERR, "Can't open history file: %s.",
		    servfile[SF_HIST]);
		cddbd_unlock(locks[LK_HIST]);
		return 0;
	}

	thist = cddbd_mktemp();

	if((tfp = fopen(thist, "w")) == NULL) {
		cddbd_log(LOG_ERR, "Can't open tmp history file: %s.",
		    thist);

		fclose(fp);
		cddbd_unlock(locks[LK_HIST]);

		return 0;
	}

	if(oflag) {
		p = strdup(name);
		if(p == NULL) {
			cddbd_log(LOG_ERR | LOG_XMIT,
			    "Can't malloc list string.");
			quit(QUIT_ERR);
		}

		hist = list_init(p, tfile_comp, tfree, tfree);
		if(hist == 0) {
			cddbd_log(LOG_ERR | LOG_XMIT,
			    "Can't malloc linked list.");
			quit(QUIT_ERR);
		}
	}

	insite = 0;

	while(fgets(buf, sizeof(buf), fp) != NULL) {
		if(sscanf(buf, histstr, buf2) == 1) {
			if(!strcmp(name, buf2)) {
				insite = 2;
				continue;
			}
			else {
				if(fputs(buf, tfp) == EOF) {
					cddbd_log(LOG_ERR | LOG_XMIT,
					    "Can't write tmp history file: %s.",
					    thist);
					quit(QUIT_ERR);
				}

				insite = 1;
				continue;
			}
		}
		else

		if(sscanf(buf, "%[^/]/%08x", buf2, &discid) != 2)
			continue;

		dir = categ_index(buf2);
		if(dir < 0)
			continue;

		switch(insite) {
		case 0:
		default:
			break;

		case 1:
			if(fprintf(tfp, "%s/%08x\n", buf2, discid) == EOF) {
				cddbd_log(LOG_ERR | LOG_XMIT,
				    "Can't write tmp history file: %s.",
				    thist);
				quit(QUIT_ERR);
			}

			break;

		case 2:
			cddbd_write_history(buf2, discid);

			break;
		}
	}

	if(!oflag && list_count(hist) > 0) {
		if(fprintf(tfp, histstr, (char *)hist->lh_data) == EOF) {
			cddbd_log(LOG_ERR | LOG_XMIT,
			    "Can't write tmp history file: %s.", thist);
			quit(QUIT_ERR);
		}

		for(list_rewind(hist), list_forw(hist); !list_rewound(hist);
		    list_forw(hist)) {
			lp = list_cur(hist);
			tf = (tfile_t *)lp->l_data;

			if(fprintf(tfp, "%s/%08x\n", categlist[tf->tf_dir],
			    tf->tf_discid) == EOF) {
				cddbd_log(LOG_ERR | LOG_XMIT,
				    "Can't write tmp history file: %s.", thist);
				quit(QUIT_ERR);
			}
		}
	}

	fclose(fp);
	fclose(tfp);

	if(unlink(servfile[SF_HIST]) != 0 && errno != ENOENT) {
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Can't unlink %s (%d).", servfile[SF_HIST], errno);
		quit(QUIT_ERR);
	}

	if(cddbd_link(thist, servfile[SF_HIST]) != 0) {
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Can't link %s to %s (%d).", thist, servfile[SF_HIST],
		    errno);
		quit(QUIT_ERR);
	}

	if(oflag) {
		hist_opened = 1;
		list_rewind(hist);
	}
	else {
		hist_opened = 0;
		list_free(hist);
	}

	cddbd_freetemp(thist);
	cddbd_unlock(locks[LK_HIST]);

	return 1;
}


int
cddbd_read_history(char *dir, unsigned int *discid)
{
	link_t *lp;
	tfile_t *tf;

	if(list_count(hist) == 0)
		return 0;

	lp = list_first(hist);
	tf = (tfile_t *)lp->l_data;
	*discid = tf->tf_discid;
	strcpy(dir, categlist[tf->tf_dir]);

	list_delete(hist, lp);

	return 1;
}


void
cddbd_write_history(char *dir, unsigned int discid)
{
	tfile_t *tf;

	if((tf = (tfile_t *)malloc(sizeof(tfile_t))) == NULL) {
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Can't malloc transmit list entry.");
		quit(QUIT_ERR);
	}

	tf->tf_dir = categ_index(dir);
	tf->tf_discid = discid;

	if(list_find(hist, (void *)tf) != 0) {
		free(tf);
	}
	else if(list_add_back(hist, (void *)tf) == 0) {
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Can't malloc linked list entry.");
		quit(QUIT_ERR);
	}
}


int
cddbd_close_history(void)
{
	if(!hist_opened)
		return 0;

	return(cddbd_oc_history((char *)hist->lh_data, 0));
}


int
cddbd_open_history(char *site)
{
	return(cddbd_oc_history(site, 1));
}


int
cddbd_count_history(char *site)
{
	FILE *fp;
	int cnt;
	int insite;
	char buf[CDDBBUFSIZ];
	char buf2[CDDBBUFSIZ];

	(void)cddbd_lock(locks[LK_HIST], 1);

	if((fp = fopen(servfile[SF_HIST], "r")) == NULL) {
		cddbd_log(LOG_ERR,
		    "Can't open history file: %s.", servfile[SF_HIST]);
		return -1;
	}

	cnt = 0;
	insite = 0;

	while(fgets(buf, sizeof(buf), fp) != NULL) {
		/* If we found a nonexistent site, clear it. */
		if(sscanf(buf, histstr, buf2) == 1) {
			if(!strcmp(site, buf2))
				insite = 1;
			else
				insite = 0;

			continue;
		}

		if(insite)
			cnt++;
	}

	fclose(fp);
	cddbd_unlock(locks[LK_HIST]);

	return cnt;
}


int
cddbd_clean_history(void)
{
	int ret;
	FILE *fp;
	char buf[CDDBBUFSIZ];
	char buf2[CDDBBUFSIZ];
	csite_t *sp;

	/* No history file yet. */
	if(access(servfile[SF_HIST], F_OK))
		return 1;

	(void)cddbd_lock(locks[LK_HIST], 1);

	ret = 1;

	for(fp = NULL; fp == NULL;) {
		if((fp = fopen(servfile[SF_HIST], "r")) == NULL) {
			cddbd_log(LOG_ERR,
			    "Can't open history file: %s.", servfile[SF_HIST]);

			ret = 0;
			break;
		}

		/* Scan the file for nonexistent sites. */
		while(fgets(buf, sizeof(buf), fp) != NULL) {
			/* If we found a nonexistent site, clear it. */
			if(sscanf(buf, histstr, buf2) == 1 &&
			    ((sp = getsitenam(buf2, SITE_XMIT)) == NULL ||
			    (sp->st_flags & ST_FLAG_NOXMIT))) {
				fclose(fp);
				fp = NULL;

				/* This clears the history for the site. */
				if(cddbd_open_history(buf2)) {
					hist_opened = 0;
					list_free(hist);
				}

				break;
			}
		}
	}

	endsiteent();
	cddbd_unlock(locks[LK_HIST]);

	return ret;
}


/* return values: 0..error; 1..ok */
int
cddbd_merge_ulist(char *site, int merge)
{
	int ret;
	csite_t *sp;

	if(ulist_fp == NULL)
		return 1;

	/* Make sure this is not a group. */
	if(setsitegrp(site)) {
		endsitegrp();
		return 0;
	}

	if(strcmp(site, "all")) {
		if((sp = getsitenam(site, SITE_XMIT)) != NULL)
			ret = cddbd_do_merge_ulist(sp->st_name, merge);
		else ret= 0;
		/* Note, BUG: ret would be undefined! */
	}
	else {
		ret = 1;

		setsiteent();

		for(;;) {
			if((sp = getsiteent(SITE_XMIT)) == NULL)
				break;

			if(!cddbd_do_merge_ulist(sp->st_name, merge))
				ret = 0;
		}
	}


	fclose(ulist_fp);
	cddbd_freetemp(uhist);
	ulist_fp = NULL;
	endsiteent();

	return ret;
}


/* return values: 0..error; 1..ok */
int
cddbd_do_merge_ulist(char *site, int merge)
{
	unsigned int discid;
	char buf[CDDBBUFSIZ];
	char dir[CDDBBUFSIZ];

	if(!cddbd_open_history(site))
		return 0;

	/* Clear the history and reopen the file if we're erasing first. */
	if(!merge) {
		hist_opened = 0;
		list_free(hist);

		if(!cddbd_open_history(site))
			return 0;
	}

	rewind(ulist_fp);

	while(fgets(buf, sizeof(buf), ulist_fp) != NULL)
		if(sscanf(buf, "%s%08x", dir, &discid) == 2)
			cddbd_write_history(dir, discid);

	if(!cddbd_close_history()) {
		cddbd_log(LOG_ERR | LOG_UPDATE,
		    "Failed to update history for %s.", site);
		return 0;
	}

	return 1;
}
	

int
cddbd_open_ulist(void)
{
	if(ulist_fp == NULL) {
		uhist = cddbd_mktemp();

		if((ulist_fp = fopen(uhist, "w+")) == NULL) {
			cddbd_log(LOG_ERR | LOG_UPDATE,
			    "Can't open update-history file: %s.", uhist);
			return 0;
		}
	}

	return 1;
}


int
cddbd_write_ulist(char *dir, char *disc)
{
	if(!cddbd_open_ulist())
		return 0;

	if(fprintf(ulist_fp, "%s %s\n", dir, disc) == EOF) {
		cddbd_log(LOG_ERR | LOG_UPDATE,
		    "Can't write update-history file: %s.", uhist);
		return 0;
	}

	return 1;
}


int
parse_coord(char *buf, coord_t *cp)
{
	if(sscanf(buf, "%c%d.%d\n", &cp->co_compass, &cp->co_degrees,
	    &cp->co_minutes) != 3)
		return 0;

	if(cp->co_compass == 'N' || cp->co_compass == 'S') {
		if(cp->co_degrees > 90)
			return 0;
	}
	else if(cp->co_compass == 'W' || cp->co_compass == 'E') {
		if(cp->co_degrees > 180)
			return 0;
	}
	else
		return 0;

	if(cp->co_degrees < 0 || cp->co_minutes > 59 || cp->co_minutes < 0)
		return 0;

	return 1;
}


void
copy_coord(char *buf, coord_t *cp)
{
	cddbd_snprintf(buf, CDDBBUFSIZ, "%c%03d.%02d", cp->co_compass,
	    cp->co_degrees, cp->co_minutes);
}


ct_key_t *
getpasswd(char *lbl)
{
	int len;
	int line;
	FILE *fp;
	ct_key_t *key;
	struct stat sbuf;
	char buf[CDDBBUFSIZ];
	char buf2[CDDBBUFSIZ];

	if(pwdfile[0] == '\0') {
		cddbd_log(LOG_ERR, "No password file defined.");
		return NULL;
	}

	if(stat(pwdfile, &sbuf) != 0) {
		cddbd_log(LOG_ERR, "Can't stat password file: %s (%d)",
		    pwdfile, errno);

		return NULL;
	}

	if(sbuf.st_mode & (S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)) {
		cddbd_log(LOG_ERR,
		    "Insecure permissions (%04o) on password file: %s",
		    (sbuf.st_mode & 0777), pwdfile);

		return NULL;
	}

	if((fp = fopen(pwdfile, "r")) == NULL) {
		cddbd_log(LOG_ERR, "Can't stat password file: %s (%d)",
		    pwdfile, errno);

		return NULL;
	}

	line = 0;
	key = NULL;

	while(fgets(buf, sizeof(buf), fp) != NULL) {
		line++;

		if(buf[0] == '#' || is_blank(buf, 0))
			continue;

		if(sscanf(buf, "%s", buf2) != 1) {
			cddbd_log(LOG_ERR,
			    "Illegal label %s in password file %s on line %d",
			    lbl, pwdfile, line);

			break;
		}

		len = strlen(buf2) - 1;

		if(buf2[len] != ':') {
			cddbd_log(LOG_ERR,
			    "Syntax error in password file %s on line %d",
			    pwdfile, line);
		}

		buf2[len] = '\0';

		if(len > CDDBPLBLSIZ) {
			cddbd_log(LOG_ERR,
			    "Label %s too long in password file %s on line %d",
			    lbl, pwdfile, line);

			break;
		}

		if(strcmp(buf2, lbl))
			continue;

		if(sscanf(buf, "%*s%s", buf2) != 1) {
			cddbd_log(LOG_ERR, "Missing password %s in password "
			    "file %s on line %d", lbl, pwdfile, line);

			break;
		}

		if((key = strtokey(buf2)) == 0) {
			cddbd_log(LOG_ERR, "Illegal password for %s in password"
			    " file %s on line %d", lbl, pwdfile, line);

			key = NULL;
		}

		break;
	}

	fclose(fp);
	return key;
}
