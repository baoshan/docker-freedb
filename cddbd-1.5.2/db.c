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
static char *const _db_c_ident_ = "@(#)$Id: db.c,v 1.41.2.17 2006/04/19 16:49:05 joerg78 Exp $";
#endif

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include "cddbd.h"
#include "patchlevel.h"


/* Prototypes. */

unsigned int db_gen_discid(db_t *);
void asy_decode(char *);

int db_classify(char *, int *, char **);
int db_col(db_t *, db_t *);
int db_cmp(db_t *, db_t *);
int db_merge(db_t *, db_t *);
int db_parse_discid(char **, unsigned int *);
int db_strlen(char *);
int db_strip_list(lhead_t *);
int db_strip_multi(lhead_t *);
int db_sum_discid(int);
int db_write_multi(FILE *, lhead_t *, db_parse_t *);
int db_write_num(FILE *, lhead_t *, db_parse_t *);
int db_write_str(FILE *, lhead_t *, db_parse_t *, int);
int is_blank(char *, int);
int is_numeric(char *);
int is_xnumeric(char *);

void db_cleanup(FILE *, db_t *, int, int);
void db_free_multi(void *);
void db_free_string(void *);

char *db_field(db_t *, int, int);

db_content_t *db_content_check(db_t *, int, int);

#ifdef DB_WINDOWS_FORMAT
int db_merge_to_windb(char *, char *, int);

/* Variables. */
/* for win format - current/last disc id from #FILENAME=... */
unsigned int db_curr_discid;

/* for win format - next disc id from #FILENAME=... */
unsigned int db_next_discid;
#endif

/**
 * The current abolute line position within the file 
 * being currently processed.
 */
unsigned int db_line_counter = 0;

int db_errno;
int hdrlen;
int prclen;
int sublen;
int trklen;

char *hdrstr = " xmcd";
char *lenstr = " Disc length: %hd";
char *offstr = "%d";
char *trkstr = " Track frame offsets:";
char *revstr = " Revision: %d";
char *substr = " Submitted via:";
char *prcstr = " Processed by:";

char *db_errmsg[] = {
	"no error",
	"out of memory",
	"file access error",
	"system call error",
	"internal server error",
	"invalid DB entry"
};


db_parse_t parstab[] = {
	{ "#",	       "comment",   "#",
	    PF_IGNORE },
	{ "DISCID",    "DISCID",    "DISCID=",
	    (PF_NUMERIC | PF_REQUIRED) },
	{ "DTITLE",    "DTITLE",    "DTITLE=",
	    (PF_REQUIRED | PF_CKTIT) },
	{ "DYEAR",    "DYEAR",    "DYEAR=",
	    PF_OPTIONAL | PF_NUMBER },
	{ "DGENRE",    "DGENRE",    "DGENRE=",
	    PF_OPTIONAL },
	{ "TTITLE",    "TTITLE",    "TTITLE%d=",
	    (PF_MULTI | PF_ONEREQ | PF_CKTRK) },
	{ "EXTD",      "EXTD",      "EXTD=",
	    0 },
	{ "EXTT",      "EXTT",      "EXTT%d=",
	    (PF_MULTI | PF_OSTRIP) },
	{ "PLAYORDER", "PLAYORDER", "PLAYORDER=",
	    PF_STRIP }
};


lhead_t *
list_init(void *data, int (*comp_func)(void *, void *),
    void (*free_func)(void *), void (*hfree_func)(void *))
{
	lhead_t *lh;

	lh = (lhead_t *)malloc(sizeof(lhead_t));
	if(lh == 0)
		return 0;

	lh->lh_count = 0;
	lh->lh_data = data;
	lh->lh_comp = comp_func;
	lh->lh_free = free_func;
	lh->lh_hfree = hfree_func;

	lh->lh_cur = (link_t *)lh;
	lh->lh_list.l_forw = (link_t *)lh;
	lh->lh_list.l_back = (link_t *)lh;

	return lh;
}


void
list_free(lhead_t *lh)
{
	list_rewind(lh);
	list_forw(lh);

	while(!list_empty(lh))
		list_delete(lh, list_cur(lh));

	if(lh->lh_hfree != 0)
		(*lh->lh_hfree)(lh->lh_data);

	free(lh);
}


link_t *
list_find(lhead_t *lh, void *data)
{
	int cmp;
	link_t *lp;

	for(list_rewind(lh), list_forw(lh); !list_rewound(lh); list_forw(lh)) {
		lp = list_cur(lh);

		if(lh->lh_comp != 0) {
			cmp = (*lh->lh_comp)(lp->l_data, data);
			if(cmp == 0)
				return lp;
			else if(cmp > 0)
				return 0;
		}
		else if(ptr_as_uint (lp->l_data) == ptr_as_uint (data))
			return lp;
		else if(ptr_as_uint (lp->l_data) > ptr_as_uint (data))
			return 0;
	}

	return 0;
}


void
list_delete(lhead_t *lh, link_t *lp)
{
	if(lh->lh_free != 0)
		(*lh->lh_free)(lp->l_data);

	lp->l_forw->l_back = lp->l_back;
	lp->l_back->l_forw = lp->l_forw;

	lh->lh_count--;
	if(lh->lh_cur == lp)
		lh->lh_cur = lp->l_forw;

	free(lp);
}


link_t *
list_add_back(lhead_t *lh, void *data)
{
	link_t *lp;

	lp = (link_t *)malloc(sizeof(link_t));
	if(lp == 0)
		return 0;

	lp->l_data = data;

	lp->l_forw = &lh->lh_list;
	lp->l_back = lh->lh_list.l_back;
	lp->l_back->l_forw = lp;
	lp->l_forw->l_back = lp;

	lh->lh_count++;

	return lp;
}


link_t *
list_add_forw(lhead_t *lh, void *data)
{
	link_t *lp;

	lp = (link_t *)malloc(sizeof(link_t));
	if(lp == 0)
		return 0;

	lp->l_data = data;

	lp->l_forw = lh->lh_list.l_forw;
	lp->l_back = &lh->lh_list;
	lp->l_back->l_forw = lp;
	lp->l_forw->l_back = lp;

	lh->lh_count++;

	return lp;
}


link_t *
list_add_cur(lhead_t *lh, void *data)
{
	link_t *lp;

	lp = (link_t *)malloc(sizeof(link_t));
	if(lp == 0)
		return 0;

	lp->l_data = data;

	/* NOTE: lh->lh_cur is initially a pointer to lh which is really a lhead_t! */
	lp->l_forw = lh->lh_cur;
	lp->l_back = lh->lh_cur->l_back;
	lp->l_back->l_forw = lp;
	lp->l_forw->l_back = lp;

	lh->lh_count++;

	return lp;
}


/** 
 * Counts number of records in each CDDB category.
 * 
 * @return vector of record counts in each category
 */
int * cddbd_count(void)
{
	int i;
	int cnt;
#ifdef DB_WINDOWS_FORMAT
	char file[CDDBBUFSIZ];
	FILE *fp = NULL;
#endif
	DIR *dirp;
	struct dirent *dp;
	char cdir[CDDBBUFSIZ];
	static int counts[CDDBMAXDBDIR + 1];

	counts[0] = 0;

	for(i = 0; categlist[i] != 0; i++) {
		cddbd_snprintf(cdir, sizeof(cdir), "%s/%s", cddbdir,
		    categlist[i]);

		if((dirp = opendir(cdir)) == NULL)
			continue;

		cnt = 0;

		while((dp = readdir(dirp)) != NULL)
#ifdef DB_WINDOWS_FORMAT
			if(strlen(dp->d_name) == WINFILEIDLEN &&
			    !is_parent_dir(dp->d_name)) {
				cddbd_snprintf(file, sizeof(cdir), "%s/%s", cdir,
				               dp->d_name);
				if((fp = fopen(file, "r")) == NULL) {
					cddbd_log(LOG_ERR, 
					          "Can't open DB file <%s>",
					          file);
					continue;
				}
				while(!db_skip(fp, NULL))
				    cnt++;
				fclose(fp);
				fp = NULL;
			}
#else
			if(strlen(dp->d_name) == CDDBDISCIDLEN &&
			    !is_parent_dir(dp->d_name))
				cnt++;
#endif

		counts[0] += cnt;
		counts[i + 1] = cnt;

		closedir(dirp);
	}

	return counts;
}


/** 
 * Updates the database from the post directory
 * 
 * Goes through all \<post_dir\>/\<category\>/\<file\> (parses to db, closes),
 * tries to get corresponding \<cddb\>/\<category\>/\<file\> (parses as db2,
 * closes), compares them which is newer (based on revision) and updates/merges
 * appropriately db, then - based on the status - it writes the record to:
 *  - \<dup_dir\>/\<category\>/\<file\> if it is a duplicit record and
 *    configured so
 *  - \<cddb\>/\<category\>/\<file\> - i.e. updates the live database
 *
 * Note that for Windows format it is solved similarly as in #db_post
 * (i.e. write in Unix format and the using #db_merge_to_windb merge into
 * the Windows DB files).
 */
void cddbd_update(void)
{
	int i;
	int x;
	int copy;
	int merge;
	int dup;
	int bad;
	int duped;
	int noread;
	int updated;
	int failed;
	int entries;
	int dowrite;
	char cdir[CDDBBUFSIZ];
	char pdir[CDDBBUFSIZ];
	char ddir[CDDBBUFSIZ];
	char file[CDDBBUFSIZ];
	char file2[CDDBBUFSIZ];
	char file3[CDDBBUFSIZ];
	char errstr[CDDBBUFSIZ];
	char errstr2[CDDBBUFSIZ];
	struct stat sbuf;
	DIR *dirp;
	struct dirent *dp;
	FILE *fp;
	db_t *db;
	db_t *db2;
	int  db_utf8;
	int  db2_utf8;
	email_hdr_t eh;

	/* No update necessary. */
	if(!strcmp(cddbdir, postdir))
		return;

	if(!cddbd_lock(locks[LK_UPDATE], 0)) {
		cddbd_log(LOG_INFO, "Update or database check already in progress.");
		quit(QUIT_RETRY);
	}

	cddbd_log(LOG_INFO, "Updating the database:");

	bad = 0;
	duped = 0;
	noread = 0;
	updated = 0;
	failed = 0;
	entries = 0;
	db = 0;
	db2 = 0;
	db_utf8 = 0;
	db2_utf8 = 0;

	/* loop through all categories, treating each one as directory */
	for(i = 0; categlist[i] != 0; i++) {

		/* database directory */
		cddbd_snprintf(cdir, sizeof(cdir), "%s/%s", cddbdir,
		    categlist[i]);

		/* post directory */
		cddbd_snprintf(pdir, sizeof(pdir), "%s/%s", postdir,
		    categlist[i]);

		/* set duplicate directory if permitted */
		if(dup_ok)
			cddbd_snprintf(ddir, sizeof(ddir), "%s/%s", dupdir,
			    categlist[i]);

		/* if this post dir/category is non existent, next... */
		if((dirp = opendir(pdir)) == NULL)
			continue;

		/* OK lets get busy */
		cddbd_log(LOG_INFO, "Updating %s.", cdir);

		db = 0;
		db2 = 0;

		while((dp = readdir(dirp)) != NULL) {
			copy = 0;
			dup = 0;
			merge = 0;

			/* clean db, db2 from prev looping */
			if(db != 0) {
				db_free(db);
				db = 0;
			}
			if(db2 != 0) {
				db_free(db2);
				db2 = 0;
			}

			cddbd_snprintf(file, sizeof(file), "%s/%s",
				pdir, dp->d_name);

			/* Make sure this is a database file. */
			if(strlen(dp->d_name) != CDDBDISCIDLEN) {
				if(!is_parent_dir(dp->d_name)) {
					if(stat(file, &sbuf)) {
						cddbd_log(LOG_ERR, "Warning: "
						    "can't stat %s", file);
						continue;
					}

#ifdef DB_WINDOWS_FORMAT
					/* A safety measure in case of
					   stray Windows format files. */
					if(strlen(dp->d_name) != WINFILEIDLEN)
						if(S_ISDIR(sbuf.st_mode))
							rmdir(file);
						else
							unlink(file);
#endif

					bad++;

					cddbd_log(LOG_INFO | LOG_UPDATE,
					    "Non-CDDB file: %s", file);
				}

				continue;
			}

			if(stat(file, &sbuf)) {
				cddbd_log(LOG_ERR, "Warning: "
				          "can't stat %s", file);
				continue;
			}
			if(sbuf.st_size == 0) { /* skip empty files */
				cddbd_log(LOG_INFO, 
				          "Info: Will skip zero size file: %s", 
				          file);
				continue;
			}

			/* got a db entry in a post dir/category */
			entries++;

			/* get the db (database formatted entry) on the "posted entry" */
			if((fp = fopen(file, "r")) == NULL) {
				cddbd_log(LOG_INFO | LOG_UPDATE,
				    "Can't read CDDB file: %s", file);

				continue;
			}

			db = db_read(fp, errstr, file_df_flags);
			fclose(fp);

			if(db == 0) {
				switch(db_errno) {
				case DE_INVALID:
					cddbd_log(LOG_ERR | LOG_UPDATE,
					    "Invalid DB file: %s: %s",
					    file, errstr);

					bad++;
					unlink(file);
					break;

				case DE_FILE:
				default:
					cddbd_log(LOG_ERR | LOG_UPDATE,
					    "Can't read %s: %s (%d)",
					    file, errstr, errno);

					noread++;
					break;
				}

				continue;
			}

			/* OK, now see if we have an entry already with this CDID */
			cddbd_snprintf(file2, sizeof(file2), "%s/%s", cdir, dp->d_name);

			/* get the db (database formatted entry) on the "existing entry" */
#ifdef DB_WINDOWS_FORMAT
			if((fp = db_prepare_unix_read(file2)) != NULL) {
				db2 = db_read(fp, errstr, file_df_flags | DF_WIN);
#else
			if((fp = fopen(file2, "r")) != NULL) {
				db2 = db_read(fp, errstr, file_df_flags);
#endif
				fclose(fp);

				if(db2 == 0) {
					if(db_errno != DE_INVALID) {
						cddbd_log(LOG_ERR | LOG_UPDATE,
						    "Can't read %s: %s (%d)",
						    file2, errstr, errno);

						continue;
					}
				}
			}

			/*******************************************************/
			/* zeke - OK, process the posted entry based on policy */
			/*-----------------------------------------------------*/
			/* db  = posted entry                                  */
			/* db2 = database entry (if exists)                    */
			/*******************************************************/

			/* Pick up extended info & plug into email header,    */
			/*  in case we need to send an email to the submitter */
			eh.eh_flags = -1;
			if(db->db_eh.eh_charset != -1 &&
				db->db_eh.eh_encoding != -1) {
				eh.eh_flags = 0;
				eh.eh_class = EC_SUBMIT;
				eh.eh_charset = db->db_eh.eh_charset;
				eh.eh_encoding = db->db_eh.eh_encoding;
				strcpy(eh.eh_to, db->db_eh.eh_to);
				strcpy(eh.eh_rcpt, db->db_eh.eh_rcpt);
				strcpy(eh.eh_host, db->db_eh.eh_host);
			}

			switch(dup_policy) {
				case DUP_NEVER:
					/* copy when no existing entry, else a dup */
					if(db2 == 0)
						copy++;
					else
						dup++;
					break;

				case DUP_ALWAYS:
					/* always copy, existing entry is replaced */
					copy++;
					break;

				case DUP_COMPARE:
					/* copy / merge based on if new entry is better */
					if(db2 == 0)
						/* no existing, just copy */
						copy++;
					else {
						/* if disc collision, treat as dup */
						if (db_col(db, db2)) {
							cddbd_snprintf(errstr2, CDDBBUFSIZ,"Discid collision in category %s", categlist[i]);
							dup++;
						}
						/* else compare the revisions of the entries */
						else {
							/* compare the revisions */
							x = db_cmp(db, db2);

							if(x == 0)		/* same revision, make the best of both */
								merge++;
							else if(x > 0)	/* new one is more current */
								copy++;
							else {			/* new one is older, or mis-matched */
								cddbd_snprintf(errstr2, CDDBBUFSIZ,
								  "Existing entry found with higher revision (%d) than submitted (%d)", 
								  db2->db_rev, db->db_rev);
								dup++;
							}
						}
					}
					break;

				default:
					cddbd_log(LOG_ERR | LOG_UPDATE,
						"Unknown dup policy: %d", dup_policy);

					quit(QUIT_ERR);
			} /* switch(dup_policy) */

			failed++;

			if(copy || merge) {
				/*------------------------------------------------------------------*/
				/* zeke - after this process - "db", not "db2", is the entry        */
				/*        to keep. Remember "db" is pointing to the memory          */
				/*        structure for the posted entry info, and that "db2"       */
				/*        is pointing to the memory structure for the database      */
				/*        entry info - if it exists.  The outcome of this sequence  */
				/*        will write the "db" entry info into "file2" on the disk.  */
				/*------------------------------------------------------------------*/
				/*        db  = posted entry      db2 = database entry (if exists)  */
				/*------------------------------------------------------------------*/
				if(db2 != 0) {
					/* db->db_eh.eh_charset has charset submission value */
					/* CC_US_ASCII or CC_ISO_8859 or CC_UTF_8 -- 0, 1, 2 */
					int cs = db->db_eh.eh_charset;
	
					/* zeke - convert both entries to Latin1 if possible */
					if ((db->db_flags & DB_ENC_UTF8) && 
					    !(db->db_flags & DB_ENC_ASCII) &&
					    !db_utf8_latin1_exact(db))
						if (db_looks_like_utf8(db))
							db_latin1_utf8(db);
	
					if ((db2->db_flags & DB_ENC_UTF8) && 
					    !(db2->db_flags & DB_ENC_ASCII) && 
					    !db_utf8_latin1_exact(db2))
						if (db_looks_like_utf8(db2))
							db_latin1_utf8(db2);
					
					/* cs != -1 --> we have the charset info from the email header */
					if (cs != -1) {
						/* if db2 entry is utf8 && !ascii && cs != utf8 === reject */
						if ((db2->db_flags & DB_ENC_UTF8) &&
							!(db2->db_flags & DB_ENC_ASCII) &&
							(cs != CC_UTF_8)) {
							/* toss the posted entry now */
							db_free(db);
							db = db2;
							db2 = 0;

							cddbd_snprintf(errstr2, CDDBBUFSIZ, 
							  "Submission is not utf8, existing database entry is utf8");

							/* the entry is a dup, therefore we must 
							  correct the respective variables */
							copy = 0;
							merge = 0;
							dup++;
						}
					} /* (cs != -1) */
					if (!dup) { /* we only need to merge, if we have no dup */
						/* to merge, convert entries to UTF8 as needed */
						if (db->db_flags & DB_ENC_LATIN1) {
							db_latin1_utf8(db);
						}
						if (db2->db_flags & DB_ENC_LATIN1) {
							db_latin1_utf8(db2);
						}
		
						/* merge posted info "db" into database info "db2" */
						if (merge) {
							(void)db_merge(db2, db);  /* E <= P */
							db_free(db);
							db = db2;
							db2 = 0;
						}
						/* copy - merge database info "db2" into posted info "db" */
						else {
							(void)db_merge(db, db2);  /* P <= E */
						}
					} /* !dup */
				} /* db2 != 0 */
	
	
				/* Convert to appropriate charset for saving. */
				if(db->db_flags & DB_ENC_ASCII)
					;
				else if(db->db_flags & DB_ENC_UTF8)
					switch (file_charset) {
					case FC_ONLY_ISO:
						db_utf8_latin1(db);
						break;
					case FC_PREFER_ISO:
						if (!db_utf8_latin1_exact(db))
							if (db_looks_like_utf8(db))
								db_latin1_utf8(db);
						break;
					default:
						break;
					}
				else if(db->db_flags & DB_ENC_LATIN1)
					switch (file_charset) {
					default:
					case FC_ONLY_ISO:
						break;
					case FC_PREFER_ISO:
						if (db_looks_like_utf8(db))
							db_latin1_utf8(db);
						break;
					case FC_PREFER_UTF:
					case FC_ONLY_UTF:
						db_latin1_utf8(db);
						break;
					}
			}
			
			dowrite = 0;

			/* update the entry based on findings */
			if(copy || merge) {
				dowrite++;
			}
			else if(dup) {
				/* zeke - dup entry, now email a rejection      */
				/* re-open posted file to send as body of email */
				if(eh.eh_flags != -1 && eh.eh_rcpt[0] != 0) {
					fp = fopen(file, "r");
					return_mail(fp, &eh, MF_FAIL, errstr2);
					fclose(fp);
				}
				if(dup_ok) { /* If a dupdir is specified, i.e. we want to keep dups */
					/* Check for dupdir, and create it as needed. */
					if(stat(dupdir, &sbuf)) {
						if(mkdir(dupdir, (mode_t)db_dir_mode)) {
							cddbd_log(LOG_ERR | LOG_UPDATE,
							    "Failed to create dup dir %s (%d).",
							    dupdir, errno);
							quit(QUIT_ERR);
						}

						(void)cddbd_fix_file(dupdir,
						    db_dir_mode, db_uid, db_gid);
					}
					else if(!S_ISDIR(sbuf.st_mode)) {
						cddbd_log(LOG_ERR | LOG_UPDATE,
						    "%s is not a directory.",
						    dupdir);
						quit(QUIT_ERR);
					}

					/* Check for category dir, and create it as needed. */
					if(stat(ddir, &sbuf)) {
						if(mkdir(ddir, (mode_t)db_dir_mode)) {
							cddbd_log(LOG_ERR | LOG_UPDATE,
							    "Failed to create category dir %s (%d).",
							    file3, errno);
							quit(QUIT_ERR);
						}

						(void)cddbd_fix_file(ddir, db_dir_mode,
						    db_uid, db_gid);
					}
					else if(!S_ISDIR(sbuf.st_mode)) {
						cddbd_log(LOG_ERR | LOG_UPDATE,
						    "%s is not a directory.", ddir);
						quit(QUIT_ERR);
					}

					/******************************************************/
					/* zeke - in case of "dup", "file2" now is going to   */
					/*        be placed in the dup dir. This replaces the */
					/*        "file2" handle created above that pointed   */
					/*        to the live "db" entry, then "dowrite++"    */
					/*        will allow this file to be written to dup   */
					/*        dir and not over the live db entry.         */
					/******************************************************/

					cddbd_snprintf(file2, sizeof(file2), "%s/%s",
					    ddir, dp->d_name);

					/* we continue processing to allow dup entry to */
					/* be created (dup_ok) in the dup directory     */
					dowrite++;
				}
				else {
					unlink(file);
					duped++;
				}
			}

			/* green light to write an entry to the disk */
			if(!dowrite)
				continue;			
			
			/* Write the new file. */
			if((fp = fopen(file2, "w")) == NULL) {
				cddbd_log(LOG_ERR | LOG_UPDATE,
				    "Can't write CDDB file: %s", file2);
				continue;
			}

#ifdef DB_WINDOWS_FORMAT
			if(db_write(fp, db, MAX_PROTO_LEVEL, DISCID_EMPTY) == 0) {
#else
			if(db_write(fp, db, MAX_PROTO_LEVEL) == 0) {
#endif
				cddbd_log(LOG_ERR | LOG_UPDATE,
				    "Can't write CDDB file: %s", file2);
				fclose(fp);
				unlink(file2);
			}

			/* Note if we can't set stats, but continue. */
			(void)cddbd_fix_file(file2, db_file_mode, db_uid, db_gid);

			if(copy || merge) {
#ifndef DB_WINDOWS_FORMAT
				db_link(db, cdir, dp->d_name, 1);
#endif

				if(!cddbd_write_ulist(categlist[i],
				    dp->d_name)) {
					cddbd_log(LOG_ERR | LOG_UPDATE,
					    "Warning: couldn't update the"
					    " history for %s/%08x",
					    categlist[i], dp->d_name);
				}

				if(verbose)
					cddbd_log(LOG_INFO,
					    "Updated %s.", file2);
				updated++;
			}
			else {
				if(verbose)
					cddbd_log(LOG_INFO,
					    "Duped %s.", file2);
				duped++;
			}

			failed--;
			fclose(fp);

			/* Remove the old file. */
			unlink(file);
		}

#ifdef DB_WINDOWS_FORMAT
		/* Merge the UNIX format entries
		   in the current category. */
		if(db_merge_to_windb(cdir, cdir, 1)) {
			cddbd_log(LOG_ERR | LOG_UPDATE,
			    "Failed to merge in %s.",
			    cdir);
			quit(QUIT_ERR);
		}
#endif

		closedir(dirp);
	}

	if(db != 0) {
		db_free(db);
		db = 0;
	}
	if(db2 != 0) {
		db_free(db2);
		db2 = 0;
	}

	if(!cddbd_merge_ulist("all", 1)) {
		cddbd_log(LOG_ERR | LOG_UPDATE,
		    "Warning: couldn't merge the xmit history update.");
	}

	cddbd_unlock(locks[LK_UPDATE]);

	cddbd_log(LOG_INFO,
		"Processed %d database entries.",
		entries);

	cddbd_log(LOG_INFO,
	    "Updated %d, %d duplicate, %d bad, %d unreadable.",
	    updated, duped, bad, noread);

	cddbd_log(LOG_INFO, "Done updating the database.");
}


/** 
 * Links matching entries.
 * 
 * Called by #main when used option -e
 */
void cddbd_match(void)
{
	int i;
	int bad;
	int links;
	int count;
	int noread;
	int entries;
	int found;
	int first;
	int cross;
	int match;
	int matched;
	db_t *db;
	FILE *fp;
	DIR *dirp;
	struct stat sbuf;
	struct stat sbuf2;
	struct dirent *dp;
	unsigned int discid;
	char file[CDDBBUFSIZ];
	char file2[CDDBBUFSIZ];
	char cdir[CDDBBUFSIZ];
	char buf[CDDBBUFSIZ];
	char errstr[CDDBBUFSIZ];
	link_t *lp;
	lhead_t *fuz;
	lhead_t **fuz_ino;
	lhead_t **fuz_categ;
	fmatch_t *fm;

#ifdef DB_WINDOWS_FORMAT
	cddbd_log(LOG_INFO, "Checking the database for matching entries does not make");
	cddbd_log(LOG_INFO, "sense for the Windows format due to the multirecord format.");
	cddbd_log(LOG_INFO, "I.e. there are no links. Exiting.");
	quit(1);
#endif

	cddbd_log(LOG_INFO, "Checking the database for matching entries.");

	bad = 0;
	links = 0;
	count = 0;
	cross = 0;
	match = 0;
	matched = 0;
	noread = 0;
	entries = 0;

	if((fuz_categ = (lhead_t **)malloc(categcnt * sizeof(lhead_t))) == 0) {
		cddbd_log(LOG_ERR, "Can't allocate list heads.");
		quit(1);
	}

	for(i = 0; categlist[i] != 0; i++) {
		fuz_categ[i] = list_init(0, 0, 0, 0);

		if(fuz_categ[i] == 0) {
			cddbd_log(LOG_ERR, "Can allocate list head.");
			quit(1);
		}
	}

	if((fuz_ino = (lhead_t **)malloc(categcnt * sizeof(lhead_t))) == 0) {
		cddbd_log(LOG_ERR, "Can't allocate list heads.");
		quit(1);
	}

	for(i = 0; categlist[i] != 0; i++) {
		fuz_ino[i] = list_init(0, 0, 0, 0);

		if(fuz_ino[i] == 0) {
			cddbd_log(LOG_ERR, "Can't malloc linked list.");
			quit(QUIT_ERR);
		}
	}

	for(i = 0; categlist[i] != 0; i++) {
		cddbd_snprintf(cdir, sizeof(cdir), "%s/%s", cddbdir, categlist[i]);

		if((dirp = opendir(cdir)) == NULL) { /* not used for win format */
			cddbd_log(LOG_ERR, "Can't open %s for reading.", cdir);
			quit(QUIT_ERR);
		}

		while((dp = readdir(dirp)) != NULL) { /* not used for win format */
			cddbd_snprintf(file, sizeof(file), "%s/%s", cdir, dp->d_name);

			if(stat(file, &sbuf)) {
				cddbd_log(LOG_ERR, 
				    "Warning: can't stat file: %s", file);
				noread++;
				continue;
			}

			if(!S_ISREG(sbuf.st_mode)) {
				if(!is_parent_dir(dp->d_name)) {
					cddbd_log(LOG_ERR, 
					    "Warning: ignoring non-file: %s",
					    file);
				}

				continue;
			}

			/* Make sure this is a database file. */
			if(strlen(dp->d_name) != CDDBDISCIDLEN) {
				cddbd_log(LOG_ERR, 
				    "Warning: ignoring non-DB entry: %s", file);
				continue;
			}

			entries++;

			sscanf(dp->d_name, "%08x", &discid);

			if(list_add_cur(fuz_categ[i], uint_as_ptr (discid)) == 0) {
				cddbd_log(LOG_ERR,
				    "Can't malloc linked list entry.");
				quit(QUIT_ERR);
			}

			if(sbuf.st_nlink > 1 && list_find(fuz_ino[i],
			    ino_t_as_ptr (sbuf.st_ino)) != 0) {
				links++;
				continue;
			}

			if(list_add_cur(fuz_ino[i],
			    ino_t_as_ptr (sbuf.st_ino)) == 0) {
				cddbd_log(LOG_ERR,
				    "Can't malloc linked list entry.");
				quit(QUIT_ERR);
			}

			if((fp = fopen(file, "r")) == NULL) {
				cddbd_log(LOG_ERR, 
				    "Warning: can't read CDDB file: %s", file);
				noread++;
				continue;
			}

			db = db_read(fp, errstr, file_df_flags);
			fclose(fp);

			if(db == 0) {
				switch(db_errno) {
				case DE_INVALID:
					cddbd_log(LOG_ERR, 
					    "Warning: invalid DB file: %s: %s",
					    file, errstr);

					bad++;
					break;

				case DE_FILE:
				default:
					cddbd_log(LOG_ERR,
					    "Warning: Can't read %s: %s (%d)",
					    file, errstr, errno);

					noread++;
					break;

				}

				continue;
			}

			/* Count the database entry. */
			count++;

			if((fuz = fuzzy_search(db->db_trks,
			    db->db_offset, db->db_disclen)) == 0) {
				db_free(db);
				continue;
			}

			found = 0;
			first = 1;

			for(list_rewind(fuz), list_forw(fuz);
			    !list_rewound(fuz); list_forw(fuz)) {
				lp = list_cur(fuz);
				fm = (fmatch_t *)lp->l_data;

				/* We've already checked this DB entry. */
				if(list_find(fuz_categ[fm->fm_catind],
				    uint_as_ptr (fm->fm_discid)))
					continue;

				db_strcpy(db, DP_DTITLE, 0, buf, sizeof(buf));

				if(fm->fm_catind != i) {
					if(first) {
						first = 0;

						if(match) {
							cddbd_log(LOG_INFO,
							    "----------------");
						}

						cddbd_log(LOG_INFO, "--> "
						    "%s/%08x: %s",
						    categlist[i], discid, buf);
					}

					cddbd_log(LOG_INFO,
					    "*   %s/%08x: %s",
					    categlist[fm->fm_catind],
					    fm->fm_discid, fm->fm_dtitle);

					match++;
					cross++;
					continue;
				}

				cddbd_snprintf(file2, sizeof(file2), "%s/%08x",
				    cdir, fm->fm_discid);

				if(stat(file2, &sbuf2)) {
					cddbd_log(LOG_ERR, "Warning: can't "
					    "stat file: %s", file2);
					continue;
				}

				/* This entry is a link to the current one. */
				if(list_find(db->db_phase[DP_DISCID],
				    uint_as_ptr (discid)) && (sbuf.st_ino ==
				    sbuf2.st_ino))
					continue;

				match++;
				found++;

				if(first) {
					first = 0;

					if(match) {
						cddbd_log(LOG_INFO,
						    "----------------");
					}

					cddbd_log(LOG_INFO,
					    "--> %s/%08x: %s",
					    categlist[i], discid, buf);
				}

				cddbd_log(LOG_INFO, "    %s/%08x: %s",
				    categlist[fm->fm_catind],
				    fm->fm_discid, fm->fm_dtitle);
			}

			if(found)
				matched++;

			list_free(fuz);
			db_free(db);
		}

		closedir(dirp);
	}

	for(i = 0; categlist[i] != 0; i++) {
		list_free(fuz_ino[i]);
		list_free(fuz_categ[i]);
	}

	if(count == 0) {
		cddbd_log(LOG_ERR, "No valid database entries.");
		quit(QUIT_ERR);
	}

	cddbd_log(LOG_INFO,
	    "Found %d matches and %d cross-categorizations for %d database "
	    "entries of %d.",
	    match, cross, matched, entries);

	cddbd_log(LOG_INFO,
	    "Ignored %d files: %d links, %d invalid, %d unreadable.",
	    (entries - count), links, bad, noread);

	cddbd_log(LOG_INFO, "Done checking the database.");
}


/* ARGSUSED */
void
cddbd_check_db(int check_level, int fix_level)
{
	int i;
	int bad;
	int post;
	int flags;
	int links;
	int count;
	int noread;
	int entries;
	int islink;
	db_t *db;
	FILE *fp;
#ifdef DB_WINDOWS_FORMAT
	unsigned int start, stop, curr;
#endif
	DIR *dirp;
	struct dirent *dp;
	lhead_t *lh;
	struct stat sbuf;
	unsigned int discid;
	char file[CDDBBUFSIZ];
	char file2[CDDBBUFSIZ];
	char errstr[CDDBBUFSIZ];

	if(!cddbd_lock(locks[LK_UPDATE], 0)) {
		cddbd_log(LOG_INFO, "Update or database check already in progress.");
		quit(QUIT_RETRY);
	}

	cddbd_log(LOG_INFO, "Checking the database.");

	bad = 0;
	links = 0;
	islink = 0;
	count = 0;
	noread = 0;
	entries = 0;
	fp = NULL;

	for(i = 0; categlist[i] != 0; i++) {
		cddbd_snprintf(file2, sizeof(file2), "%s/%s", cddbdir,
		    categlist[i]);

		cddbd_log(LOG_INFO, "Scanning %s.", file2);

		if((dirp = opendir(file2)) == NULL) {
			cddbd_log(LOG_ERR, "Can't open %s for reading.", file2);
			quit(QUIT_ERR);
		}

		lh = list_init(0, 0, 0, 0);
		if(lh == 0) {
			cddbd_log(LOG_ERR, "Can't malloc linked list.");
			quit(QUIT_ERR);
		}

		while((dp = readdir(dirp)) != NULL) {
			cddbd_snprintf(file, sizeof(file), "%s/%s", file2,
			    dp->d_name);

			if(stat(file, &sbuf)) {
				cddbd_log(LOG_ERR, 
				    "Warning: can't stat file: %s", file);
				noread++;
				continue;
			}

			if(!S_ISREG(sbuf.st_mode)) {
				if(!is_parent_dir(dp->d_name)) {
					cddbd_log(LOG_ERR, 
					    "Warning: ignoring non-file: %s",
					    file);
				}

				continue;
			}

			/* Make sure this is a database file. */
#ifdef DB_WINDOWS_FORMAT
			if(strlen(dp->d_name) != WINFILEIDLEN && 
			   strlen(dp->d_name) != CDDBDISCIDLEN) {
#else
			if(strlen(dp->d_name) != CDDBDISCIDLEN) {
#endif
				if(!is_parent_dir(dp->d_name)) {
					if(fix_level >= FL_REMOVE) {
						unlink(file);
						cddbd_log(LOG_INFO,
						    "Removing non-CDDB file: "
						    "%s", file);
					}
					else {
						cddbd_log(LOG_ERR,
						    "Warning: ignoring non-CDDB"
						    "file: %s", file);
					}
				}

				continue;
			}

#ifdef DB_WINDOWS_FORMAT
			start = 0;
			stop = 0;
			if (strlen(dp->d_name) == CDDBDISCIDLEN) {
				/* make sure the file is a valid standard format database file*/
				if((fp = fopen(file, "r")) == NULL) {
					cddbd_log(LOG_ERR, 
					    "Warning: can't read possible CDDB file: %s", file);
					noread++;
					continue;
				}
				db = db_read(fp, errstr, file_df_flags);
				fclose(fp);

				if(db == 0) {
					switch(db_errno) {
						case DE_INVALID:
							if(fix_level >= FL_REMOVE) {
								unlink(file);
								cddbd_log(LOG_INFO, 
								    "Removing invalid DB file:"
								    " %s: %s", file, errstr);
							}
							else {
								cddbd_log(LOG_ERR, 
								    "Warning: invalid DB file:"
								    " %s: %s", file, errstr);
							}

							bad++;
							break;

						case DE_FILE:
						default:
							cddbd_log(LOG_ERR,
							    "Warning: Can't read %s: %s (%d)",
							    file, errstr, errno);

							noread++;
							break;

					}
					continue;
				}
				else {
					db_free(db);
					cddbd_log(LOG_INFO,
					          "Ignoring standard format CDDB file: "
					          "%s", file);
					continue;
				}
			}
			else {
				if(sscanf(dp->d_name, "%02xto%02x", &start, &stop) != 2) {

					if(fix_level >= FL_REMOVE) {
						unlink(file);
						cddbd_log(LOG_INFO,
						          "Removing non-CDDB file: "
						          "%s", file);
					}
					else {
						cddbd_log(LOG_ERR,
						          "Warning: ignoring non-CDDB"
								"file: %s", file);
					}
					continue;
				}
			}
#endif

			islink = 0;
#ifndef DB_WINDOWS_FORMAT
			entries++;
#endif

			if(sbuf.st_nlink > 1) {
				if(list_find(lh, ino_t_as_ptr (sbuf.st_ino)) != 0) {
					links++;
					islink++;
					continue;
				}
				else if(list_add_cur(lh, ino_t_as_ptr (sbuf.st_ino)) == 0) {
					cddbd_log(LOG_ERR,
					    "Can't malloc linked list entry.");
					quit(QUIT_ERR);
				}
			}

			/* Check the file permissions. */
			if((sbuf.st_mode & 0777) != db_file_mode) {
				if(fix_level >= FL_REPAIR) {
					if(chmod(file, (mode_t)db_file_mode)) {
						cddbd_log(LOG_ERR | LOG_UPDATE,
						    "Warning: Couldn't change "
						    "perms on %s.", file);
					}
					else
						cddbd_log(LOG_INFO,
						    "Set file mode on DB file:"
						    " %s", file);
				}
				else
					cddbd_log(LOG_ERR, "Warning: incorrect"
					    " mode %04o on DB file: %s", 
					    (sbuf.st_mode & 0777), file);
			}


			/* Check the file ownership. */
			if(sbuf.st_uid != db_uid || sbuf.st_gid != db_gid) {
				if(fix_level >= FL_REPAIR) {
					if(chown(file, db_uid, db_gid)) {
						cddbd_log(LOG_ERR | LOG_UPDATE,
						    "Warning: Couldn't change "
						    "owner on DB file: %s",
						    file);
					}
					else
						cddbd_log(LOG_INFO,
						    "Set owner/grp on DB file:"
						    " %s", file);
				}
				else
					cddbd_log(LOG_ERR, "Warning: incorrect "
					    "owner/group on DB file: %s", file);
			}

			if(sbuf.st_size == 0) { /* check empty files */
				if(fix_level >= FL_REMOVE) {
					unlink(file);
					cddbd_log(LOG_INFO,
					          "Removing empty CDDB file: "
					          "%s", file);
				}
				else
					cddbd_log(LOG_INFO,
					          "Warning: ignoring empty CDDB file:"
					          " %s", file);
				continue;
			}

			if((fp = fopen(file, "r")) == NULL) {
				cddbd_log(LOG_ERR, 
				    "Warning: can't read CDDB file: %s", file);
				noread++;
				continue;
			}

			flags = DF_SUBMITTER | DF_CK_SUBMIT | file_df_flags;
#ifdef DB_WINDOWS_FORMAT
			/* initialise the multirecord read */
			db_line_counter = 0;
			if(db_skip(fp, NULL)) {
				cddbd_log(LOG_ERR, 
				          "Can't skip record separator in %s", file);
				fclose(fp);
				fp = NULL;
				continue;
			}

			do { /* cycle through the multirecord file */
				entries++;
				db = db_read(fp, errstr, flags | DF_WIN);
#else
			db = db_read(fp, errstr, flags);
			fclose(fp);
#endif

			if(db == 0) {
				switch(db_errno) {
				case DE_INVALID:
#ifdef DB_WINDOWS_FORMAT
					cddbd_log(LOG_ERR, 
						    "Warning: invalid DB entry %s in file:"
						    " %s: %s", db_curr_discid, file, errstr);
#else
					if(fix_level >= FL_REMOVE) {
						unlink(file);
						cddbd_log(LOG_INFO, 
						    "Removing invalid DB file:"
						    " %s: %s", file, errstr);
					}
					else {
						cddbd_log(LOG_ERR, 
						    "Warning: invalid DB file:"
						    " %s: %s", file, errstr);
					}
#endif

					bad++;
					break;

				case DE_FILE:
				default:
					cddbd_log(LOG_ERR,
					    "Warning: Can't read %s: %s (%d)",
					    file, errstr, errno);

					noread++;
					break;

				}

				continue;
			}

#ifdef DB_WINDOWS_FORMAT
			curr = (db_curr_discid >> 24) & 0xff;
			discid = db_curr_discid;
			if(curr<start || curr>stop) {
				cddbd_log(LOG_ERR | LOG_UPDATE,
				          "Warning: The record %08x does not belong to this"
				          " CDDB file %s (record near line %u)",
				          db_curr_discid, file, db_line_counter);
				cddbd_log(LOG_ERR | LOG_UPDATE,
				          "Warning: CDDBD will not be able find this record."
				          " Fix it manually and rerun the check.");
			}
#else
			sscanf(dp->d_name, "%08x", &discid);
#endif

			if(fix_level >= FL_REPAIR) {
				post = 0;

				/* Make sure discid is in the DB entry. */
				if(!list_find(db->db_phase[DP_DISCID],
				    uint_as_ptr (discid))) {
					/* Add the discid to the entry. */
					if(!list_add_cur(
					    db->db_phase[DP_DISCID],
					    uint_as_ptr (discid))) {
						cddbd_log(LOG_ERR, 
						    "Can't malloc list entry");

						quit(QUIT_ERR);
					}

					cddbd_log(LOG_INFO,
					    "Added %s %08x to DB entry: %s",
					    parstab[DP_DISCID].dp_name, uint_as_ptr (discid),
					    file);

					post++;
				}
				else {
#ifndef DB_WINDOWS_FORMAT
					/* Make any missing links. */
					db_link(db, file2, dp->d_name, 1);
#endif
				}

				/* Write it out if we added a rev string. */
				if(!(db->db_flags & DB_REVISION)) {
					cddbd_log(LOG_INFO,
					    "Added revision to DB entry: %s",
					    file);

					post++;
				}

				/* Write it out if we added a proc string. */

				/* if(!(db->db_flags & DB_PROCESSOR)) post++; */

				if(strip_ext && (db->db_flags & DB_STRIP)) {
					cddbd_log(LOG_ERR, 
					    "Stripping optional fields"
					    " in DB entry: %s", file);

					if(!db_strip(db)) {
						cddbd_log(LOG_ERR, 
						    "Failed to allocate memory"
						    " to strip entry.");
						quit(QUIT_ERR);
					}

					post++;
				}

				if(post && !db_post(db, file2, discid, errstr)) {
					cddbd_log(LOG_ERR,
					    "Warning: can't fix DB entry: %s",
					    file);
				}
			}
			else {
				/* Make sure discid is in the DB entry. */
				if(!list_find(db->db_phase[DP_DISCID],
				    uint_as_ptr (discid))) {
					cddbd_log(LOG_ERR,
					    "Warning: DB entry %s missing %s",
					    parstab[DP_DISCID].dp_name,
					    dp->d_name);
				}

				if(verbose && !(db->db_flags & DB_REVISION)) {
					cddbd_log(LOG_ERR,
					    "Warning: DB entry missing rev: %s",
					    file);
				}

				if(verbose && strip_ext &&
				    (db->db_flags & DB_STRIP)) {
					cddbd_log(LOG_ERR, 
					    "Warning: strippable optional "
					    "fields in DB entry: %s", file);
				}

#ifndef DB_WINDOWS_FORMAT
				/* Verify the links. */
				db_link(db, file2, dp->d_name, 0);
#endif
			}

			/* Count the database entry. */
			if(!islink)
				count++;

			db_free(db);

#ifdef DB_WINDOWS_FORMAT
			/* while it is not the last one in the file and no error occurred */
			} while(fp != NULL && !feof(fp) && !ferror(fp) ); 
			/* end of cycle through the multirecord file */
			fclose(fp);
			fp = NULL;
#endif
		}

		list_free(lh);
		closedir(dirp);
	}

	if(count == 0) {
		cddbd_log(LOG_ERR, "No valid database entries.");
		quit(QUIT_ERR);
	}

	cddbd_unlock(locks[LK_UPDATE]);

	cddbd_log(LOG_INFO, "Checked %d database entries.", entries);
	cddbd_log(LOG_INFO,
	    "Found %d normal files, %d links, %d invalid, %d unreadable.",
	    count, links, bad, noread);
	cddbd_log(LOG_INFO, "Done checking the database.");
}

/** 
 * Skips to the given record within the opened file in the DB_WINDOWS_FORMAT
 * 
 * The file remains opened just after the corresponding #FILENAME= line (i.e.
 * it is ready to be read in the Unix way). Also the following global variables
 * are set:
 *  - #db_curr_discid - set to the record we are ready to read
 *  - #db_next_discid - set to the record we are ready to read
 * 
 * @param fp     opened file
 * @param discid disc ID we want to skip to within the file. If NULL just 
 *               skip to the next record.
 * 
 * @return 0 when OK
 */
#ifdef DB_WINDOWS_FORMAT
int db_skip(FILE *fp, char *discid) {
	char buf[CDDBBUFSIZ];
	unsigned int tmpdiscid;

	if(debug && verbose)
		cddbd_log(LOG_INFO, "Skipping to <%s>", discid);

	if (discid != NULL && sscanf(discid, "%08x", &tmpdiscid) != 1) {
		cddbd_log(LOG_ERR,
		          "Can't parse skip discid from <%s>",
		          discid);
		return 1;
	}

	for(;;) {
		if(fgets(buf, sizeof(buf), fp) == NULL)
			return 1;
		db_line_counter++;

		if(strncmp(buf, STARTTAG, STARTTAGLEN) == 0) {
			db_next_discid = 0;
			if(discid == NULL) { /* just parse the discid */
				if(sscanf(buf+STARTTAGLEN, "%08x", &db_next_discid)
				   != 1) {
					cddbd_log(LOG_ERR,
					          "Can't parse discid from <%s>",
					          buf+STARTTAGLEN);
					continue;
				}
				db_curr_discid = db_next_discid;
				return 0; /* we are done, next record was found */
			}
			else {
				if(strncmp(buf+STARTTAGLEN, discid, 8) == 0) {
					db_next_discid = tmpdiscid;
					db_curr_discid = tmpdiscid;
					return 0;
				}
			}
		}
	}
}
#endif /* DB_WINDOWS_FORMAT */

/** 
 * Seeks to the DB record in a UNIX fashion while in the DB_WINDOWS_FORMAT
 *
 * I.e. opens the corresponding file and seeks just after the tag
 * FILENAME=discid (the rest of the procesing is the same as for the UNIX 
 * format).
 * 
 * @param filename record filename in "UNIX" format to be opened. E.g.
 *                 "/usr/local/cddb/blues/01234567"
 * 
 * @return NULL if no such record, valid pointer otherwise
 */
#ifdef DB_WINDOWS_FORMAT
FILE * db_prepare_unix_read(char *filename) {
	FILE *fp = NULL;
	char tmpBuff[250];
	char tmpBuffRange[250];
	int filenameLen;
	char * dirNameEnd;
	DIR *dirp;
	struct dirent *dp;
	unsigned int start, end, curr;

	filenameLen = strlen(filename);
	if(filenameLen >= 250) {
		cddbd_log(LOG_ERR,
		          "Filename <%s> too long for db_prepare_unix_read.", 
		          filename);
		return NULL;
	}

#ifdef DB_WINDOWS_FORMAT_USE_RANGES
	/* initialise the buffer */
	tmpBuff[0] = '\0';

	/* make a copy since we don't know what opendir/readdir will do to it */
	strcpy(tmpBuffRange, filename);

	/* get the "highest" byte of the discid */
	tmpBuffRange[filenameLen-6] = '\0'; /* does not matter for dirname */
	curr = strtol(tmpBuffRange+filenameLen-8, NULL, 16);

	/* First open the genre directory ... */
	/* there are some problems with dirname in Cygwin so doing it manually */
	dirNameEnd = tmpBuffRange+filenameLen-9;
	if(*dirNameEnd != '/') {
		cddbd_log(LOG_ERR,
		          "Wrong directory/filename %s.", 
		          tmpBuffRange);
		return NULL;
	}
	*dirNameEnd = '\0';

	if((dirp = opendir(tmpBuffRange)) == NULL) {
		cddbd_log(LOG_ERR,
		          "Can't open the genre directory %s for reading.", 
		          tmpBuffRange);
		return NULL;
	}

	/* ... and then try to find the corresponding file */
	while((dp = readdir(dirp)) != NULL) {
		if(strlen(dp->d_name) != WINFILEIDLEN)
			continue;

		if(sscanf(dp->d_name, "%02xto%02x", &start, &end) != 2) {
			cddbd_log(LOG_ERR,
			          "Can't parse db filename %s - ignoring", 
			          dp->d_name);
			continue;
		}

		if(start<=curr && curr<=end) { /* this is our file */
			strcpy(tmpBuff, tmpBuffRange);
			strcat(tmpBuff, "/");
			strcat(tmpBuff, dp->d_name);
			break;
		}
	}

	closedir(dirp);

	if(dp == NULL) { /* the range file was not found */
		cddbd_log(LOG_ERR,
		          "Could not find range file for %s (inconsistent DB?).",
		          filename);
		return NULL;
	}
#else
	strcpy(tmpBuff, filename);
	tmpBuff[filenameLen-6] = 't';
	tmpBuff[filenameLen-5] = 'o';
	tmpBuff[filenameLen-4] = tmpBuff[filenameLen-8];
	tmpBuff[filenameLen-3] = tmpBuff[filenameLen-7];
	tmpBuff[filenameLen-2] = '\0';
#endif

	cddbd_log(LOG_ACCESS | LOG_INFO, "Trying to open <%s>", tmpBuff);

	db_line_counter = 0;
	if((fp = fopen(tmpBuff, "r")) != NULL) {
		cddbd_log(LOG_ACCESS | LOG_INFO, "Successfully opened <%s>", tmpBuff);
		if(db_skip(fp, filename+filenameLen-8)) {
			fclose(fp);
			fp = NULL;
		}
	}

	return fp;
}
#endif /* DB_WINDOWS_FORMAT */


/** 
 * Reads a DB record from the given file
 * 
 * @param fp      already opened file from which we will read
 * @param errstr  string used for error logging
 * @param flags   flags
 * 
 * @return parsed database record (allocated here)
 */
db_t *db_read(FILE *fp, char *errstr, int flags) {
	int i;
	int j;
	int n;
	int fc;
	int off;
	int loff;
	int line;	/* the current line in the DB record being read */
	int pcnt;
	int trks;
	int phase;
	int class;
	int optional;			/* used to look for optional fields */
	unsigned int discid;
	unsigned int tmpdiscid;
	db_t *db;
	lhead_t *lh;
	lhead_t *lh2;
	link_t *lp;
	void (*func)();
	db_parse_t *dp;
	db_content_t *dc;
	char *p;
	char *p2;
	char buf[CDDBBUFSIZ];
	char buf2[CDDBBUFSIZ];

	/* clear global error flag... */
	db_errno = 0;
	
	db = (db_t *)malloc(sizeof(db_t));
	if(db == 0) {
		cddbd_snprintf(errstr, CDDBBUFSIZ,
		    "can't get memory for DB entry");

		db_cleanup(fp, db, DE_NOMEM, flags);
		return 0;
	}

	/* zeke - make sure no junk in entry passed back */
	db->db_flags = 0;
	db->db_rev = 0;
	db->db_trks = 0;
	db->db_disclen = 0;
	db->db_eh.eh_charset = -1;
	db->db_eh.eh_encoding = -1;
	db->db_eh.eh_to[0] = 0;
	db->db_eh.eh_rcpt[0] = 0;
	db->db_eh.eh_host[0] = 0;

	/* zeke - based on passed in flags, set db->db_flags */
	db->db_flags = DB_ENC_ASCII;

	if (flags & DF_ENC_LATIN1)
	  db->db_flags |= DB_ENC_LATIN1;

	if (flags & DF_ENC_UTF8)
	  db->db_flags |= DB_ENC_UTF8;

	/* init phase table */
	for(i = 0; i < DP_NPHASE; i++)
		db->db_phase[i] = 0;

	pcnt = 0;
	line = 0;
	phase = 0;

	/* process the db entry... */
	for(;;) {
		if(flags & DF_STDIN) {
			if(cddbd_gets(buf, sizeof(buf)) == NULL) {
				break;
			}
		}
#ifdef DB_WINDOWS_FORMAT
		else {
			if(fgets(buf, sizeof(buf), fp) == NULL) {
				if(flags & DF_WIN) {
					db_curr_discid = db_next_discid;
					db_next_discid = 0;
				}
				break;
			}
			if(flags & DF_WIN)
				db_line_counter++;
		}

		if(flags & DF_WIN) {
			/* check for next record within the file */
			if(strncmp(buf, STARTTAG, STARTTAGLEN) == 0) {
				tmpdiscid = db_next_discid;
				if(sscanf(buf+STARTTAGLEN, "%08x", &db_next_discid) != 1) {
					cddbd_log(LOG_ERR,
					          "Can't parse discid from <%s>",
					          buf);
					/* we couldn't parse the discid, so let db_skip
					   find the next valid entry */
					db_skip(fp, NULL);
				}
				db_curr_discid = tmpdiscid;
				break;
	 		}
		}
#else
		else {
			if(fgets(buf, sizeof(buf), fp) == NULL)
				break;
		}
#endif

		line++;
		dp = &parstab[phase];

		if(line > max_lines) {

			if(flags & DF_WIN)
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "too many lines in input, max %d (reached at line %u, %d relative)", 
				    max_lines, db_line_counter, line);
			else
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "too many lines in input, max %d", max_lines);

			db_cleanup(fp, db, DE_INVALID, flags);
			return 0;
		}

		if(db_strlen(buf) > DBLINESIZ) {
			if(flags & DF_WIN)
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "input too long on line %u (%d within the record)", 
				    db_line_counter, line);
			else
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "input too long on line %d", line);

			db_cleanup(fp, db, DE_INVALID, flags);
			return 0;
		}

		/* zeke - each line tested for the correct charset, based */
		/*        on allowable in db_flags (set up above). if not */
		/*        acceptable, remove charset supp from db_flags.  */
		if((db->db_flags & DB_ENC_ASCII) && !charset_is_valid_ascii(buf))
			db->db_flags &= ~DB_ENC_ASCII;

		if((db->db_flags & DB_ENC_LATIN1) && !charset_is_valid_latin1(buf))
			db->db_flags &= ~DB_ENC_LATIN1;

		if((db->db_flags & DB_ENC_UTF8) && !charset_is_valid_utf8(buf))
			db->db_flags &= ~DB_ENC_UTF8;
		
		/* must have at least one standing, or hosed up line & quit! */
		if(!(db->db_flags & (DB_ENC_ASCII | DB_ENC_LATIN1 | DB_ENC_UTF8))) {
			if(flags & DF_WIN)
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "garbage character on line %u (%d within the record)", 
				    db_line_counter, line);
			else
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "garbage character on line %d", line);

			db_cleanup(fp, db, DE_INVALID, flags);
			return 0;
		}

		if(is_dot(buf)) {
			if(!(flags & DF_STDIN)) {
				if(flags & DF_WIN)
					cddbd_snprintf(errstr, CDDBBUFSIZ,
					    "illegal input on line %u (%d within the record)", 
					    db_line_counter,line);
				else
					cddbd_snprintf(errstr, CDDBBUFSIZ,
					    "illegal input on line %d", line);

				db_cleanup(fp, db, DE_INVALID, flags);
				return 0;
			}
			else if(phase < (DP_NPHASE - 1)) {
				if(flags & DF_WIN)
					cddbd_snprintf(errstr, CDDBBUFSIZ,
					    "unexpected end on line %u (%d within the record)", 
					    db_line_counter, line);
				else
					cddbd_snprintf(errstr, CDDBBUFSIZ,
					    "unexpected end on line %d", line);

				db_cleanup(fp, db, DE_INVALID, flags);
				return 0;
			}

			break;
		}

		if(is_blank(buf, 0)) {
			if(phase == (DP_NPHASE - 1) && (!(flags & DF_STDIN)))
				break;

			if(phase == 0 && db->db_phase[phase] == 0)
				continue;

			if(flags & DF_WIN)
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "blank line in input on line %u (%d within the record)", 
				    db_line_counter, line);
			else
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "blank line in input on line %d", line);

			db_cleanup(fp, db, DE_INVALID, flags);
			return 0;
		}

		/* pick-up email info added in posting process */
		sscanf(buf, "## Cs: %d", &db->db_eh.eh_charset);
		sscanf(buf, "## En: %d", &db->db_eh.eh_encoding);
		sscanf(buf, "## To: %s", &db->db_eh.eh_to);
		sscanf(buf, "## Rc: %s", &db->db_eh.eh_rcpt);
		sscanf(buf, "## Ho: %s", &db->db_eh.eh_host);

		/* zeke - look for valid marker on line, else bad line entry... */
		class = db_classify(buf, &n, &p);
		if(class < 0) {
			if(phase == (DP_NPHASE - 1) && (!(flags & DF_STDIN)))
				break;

			/* Skip junk at beginning of email. */
			if(phase == 0 && db->db_phase[phase] == 0 &&
			    (flags & DF_MAIL))
				continue;

			if(flags & DF_WIN)
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "unrecognized input on line %u (%d within the record)", 
				    db_line_counter, line);
			else
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "unrecognized input on line %d", line);

			db_cleanup(fp, db, DE_INVALID, flags);
			return 0;
		}

		/* If email and we found the entry, start counting again. */
		if(phase == 0 && db->db_phase[phase] == 0 && (flags & DF_MAIL))
			line = 1;

		if(n >= CDDBMAXTRK) {
			if(flags & DF_WIN)
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "invalid numerical argument in %s on line %u (%d within the record)", 
				    dp->dp_name, db_line_counter, line);
			else
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "invalid numerical argument in %s on line %d",
				    dp->dp_name, line);

			db_cleanup(fp, db, DE_INVALID, flags);
			return 0;
		}

		if(class < phase) {
			if(parstab[class].dp_flags & PF_IGNORE)
				continue;

			/* Can't go backwards. */
			if(flags & DF_WIN)
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "missing expected %s on line %u (%d within the record)", 
				    dp->dp_name, db_line_counter, line);
			else
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "missing expected %s on line %d",
				    dp->dp_name, line);

			db_cleanup(fp, db, DE_INVALID, flags);
			return 0;
		}
		else if(class > phase) {
			/* Are we skipping a phase? */
			if(db->db_phase[phase] == 0 || class > (phase + 1)) {

				/* verify if all skipped entries are optional */
				optional = 1;
				for(i=phase+1; (i<class) && optional; i++) {
					if (!(parstab[i].dp_flags & PF_OPTIONAL)) optional = 0;
				}
				
				/* all skipped entries were optional ? */
				if ((optional) && (!(db->db_phase[phase] == 0))) {
					phase = class - 1;	/* skip all optional fields */
				}
				else {
					if(parstab[class].dp_flags & PF_IGNORE)
						continue;

					if(flags & DF_WIN)
						cddbd_snprintf(errstr, CDDBBUFSIZ,
						    "missing expected %s on line %u (%d within the record)", 
						    parstab[phase + 1].dp_name, db_line_counter, line);
					else
						cddbd_snprintf(errstr, CDDBBUFSIZ,
						    "missing expected %s on line %d",
						    parstab[phase + 1].dp_name, line);

					db_cleanup(fp, db, DE_INVALID, flags);
					return 0;
				}
			}

			phase++;
			dp = &parstab[phase];
		}

		if(!(dp->dp_flags & PF_MULTI) && n >= 0) {
			if(flags & DF_WIN)
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "unexpected track # `%d'in %s on line %u (%d within the record)", 
				    n, dp->dp_name, db_line_counter, line);
			else
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "unexpected track # `%d'in %s on line %d",
				    n, dp->dp_name, line);

			db_cleanup(fp, db, DE_INVALID, flags);
			return 0;
		}

		/* Get a list head for this phase. */
		if(db->db_phase[phase] == 0) {
			if((dp->dp_flags & PF_MULTI))
				func = db_free_multi;
			else if((dp->dp_flags & PF_NUMERIC))
				func = 0;
			else
				func = db_free_string;

			db->db_phase[phase] = list_init(0, 0, func, 0);

			if(db->db_phase[phase] == 0) {
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "can't malloc list head");
				db_cleanup(fp, db, DE_NOMEM, flags);
				return 0;
			}

			pcnt = 0;
		}

		lh = db->db_phase[phase];

		/* Deal with phases that have multiple subphases. */
		if(dp->dp_flags & PF_MULTI) {
			/* Can't go backwards. */
			if(n < pcnt) {
				if(flags & DF_WIN)
					cddbd_snprintf(errstr, CDDBBUFSIZ,
					    "unexpected track # `%d' in %s on line %u (%d within the record)", 
					    n, dp->dp_name, db_line_counter, line);
				else
					cddbd_snprintf(errstr, CDDBBUFSIZ,
					    "unexpected track # `%d' in %s on line %d",
					    n, dp->dp_name, line);

				db_cleanup(fp, db, DE_INVALID, flags);
				return 0;
			}
			else if(n > pcnt) {
				pcnt++;

				/* Can't skip a subphase. */
				if(list_count(lh) < pcnt || n > pcnt) {
					if(flags & DF_WIN)
						cddbd_snprintf(errstr, CDDBBUFSIZ,
						    "unexpected track # `%d' in %s on "
						    "line %u (%d within the record)", 
						    n, dp->dp_name, db_line_counter, line);
					else
						cddbd_snprintf(errstr, CDDBBUFSIZ,
						    "unexpected track # `%d' in %s on "
						    "line %d", n, dp->dp_name, line);

					db_cleanup(fp, db, DE_INVALID, flags);
					return 0;
				}
			}

			/* Initialize list head for subphase. */
			if(list_count(lh) == pcnt) {
				lh2 = list_init(0, 0, db_free_string, 0);
				if(lh2 == 0) {
					cddbd_snprintf(errstr, CDDBBUFSIZ,
					    "can't malloc list head");
					db_cleanup(fp, db, DE_NOMEM, flags);
					return 0;
				}

				/* Add subphase to the list. */
				if(list_add_back(lh, (void *)lh2) == 0) {
					cddbd_snprintf(errstr, CDDBBUFSIZ,
					    "can't malloc list entry");
					db_cleanup(fp, db, DE_NOMEM, flags);
					return 0;
				}

				lh = lh2;
			}
			else {
				list_rewind(lh);
				lh = (lhead_t *)list_last(lh)->l_data;
			}
		}

		/* Note if strippable fields are not emtpy. */
		if(dp->dp_flags & PF_OSTRIP && !is_blank(p, 0))
			db->db_flags |= DB_STRIP;

		if(dp->dp_flags & PF_NUMBER) {
			for (i=0;i<strlen(p)-1;i++) {
				if(!isdigit(p[i]) && p[i] != '\r') {
					cddbd_snprintf(errstr, CDDBBUFSIZ,
					    "numeric value in %s expected", dp->dp_name);
					db_cleanup(fp, db, DE_INVALID, flags);
					return 0;
				}
			}
		}

		if(dp->dp_flags & PF_NUMERIC) {
			do {
				if(db_parse_discid(&p, &discid) != 0) {
					if(flags & DF_WIN)
	 					cddbd_snprintf(errstr, CDDBBUFSIZ,
						    "bad disc ID on line %u (%d within the record)", 
						     db_line_counter, line);
					else
						cddbd_snprintf(errstr, CDDBBUFSIZ,
						    "bad disc ID on line %d", line);

					db_cleanup(fp, db, DE_INVALID, flags);
					return 0;
				}

				if(list_find(lh, uint_as_ptr (discid)) != 0) {
					if(flags & DF_WIN)
	 					cddbd_snprintf(errstr, CDDBBUFSIZ,
					        "duplicate disc ID on line %u (%d within the record)", 
					        db_line_counter, line);
					else
						cddbd_snprintf(errstr, CDDBBUFSIZ,
						    "duplicate disc ID on line %d",
						    line);

					db_cleanup(fp, db, DE_INVALID, flags);
					return 0;
				}

				if(list_add_cur(lh, uint_as_ptr (discid)) == 0) {
					cddbd_snprintf(errstr, CDDBBUFSIZ,
					    "can't malloc list entry");
					db_cleanup(fp, db, DE_NOMEM, flags);
					return 0;
				}
			} while(*p != '\0');
		}
		else {
			/* Strip data from certain entries. */
			if(dp->dp_flags & PF_STRIP) {
				/* Empty out the list. */
				if(list_count(lh) > 0) {
					list_rewind(lh);
					list_forw(lh);

					while(!list_empty(lh))
						list_delete(lh, list_cur(lh));
				}

				p = "\n";
			}

			p = (char *)strdup(p);
			if(p == 0) {
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "can't malloc list data");
				db_cleanup(fp, db, DE_NOMEM, flags);
				return 0;
			}

			/* Put string in the list. */
			if(list_add_back(lh, (void *)p) == 0) {
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "can't malloc list entry");
				db_cleanup(fp, db, DE_NOMEM, flags);
				return 0;
			}
		}
	}

	/* Make sure we went through all the phases. */
	if(phase != (DP_NPHASE - 1)) {
		if(flags & DF_WIN)
			cddbd_snprintf(errstr, CDDBBUFSIZ, 
			    "unexpected end on line %u (%d within the record)", 
			    db_line_counter, line);
		else
			cddbd_snprintf(errstr, CDDBBUFSIZ, "unexpected end on line %d",
			    line);

		db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
		return 0;
	}

	p2 = (char *)strdup("\n");
	if(db->db_phase[DP_DYEAR]==0) {
		db->db_phase[DP_DYEAR] = list_init(0, 0, db_free_string, 0);
		if(list_add_back(db->db_phase[DP_DYEAR], (void *)p2) == 0) {
			cddbd_snprintf(errstr, CDDBBUFSIZ,
			    "can't malloc list entry");
			free(p2);   
			db_cleanup(fp, db, DE_NOMEM, (flags | DF_DONE));
			return 0;
		}
	}

	p2 = (char *)strdup("\n");
	if(db->db_phase[DP_DGENRE]==0) {
		db->db_phase[DP_DGENRE] = list_init(0, 0, db_free_string, 0);
		if(list_add_back(db->db_phase[DP_DGENRE], (void *)p2) == 0) {
			cddbd_snprintf(errstr, CDDBBUFSIZ,
			    "can't malloc list entry");
			free(p2);
			db_cleanup(fp, db, DE_NOMEM, (flags | DF_DONE));
			return 0;
		}
	}
	


	/* Check the comments. */

	/* Check for the header. */
	lh = db->db_phase[DP_COMMENT];
	list_rewind(lh);
	list_forw(lh);
	lp = list_cur(lh);

	if(strncmp((char *)lp->l_data, hdrstr, hdrlen)) {
		cddbd_snprintf(errstr, CDDBBUFSIZ, "missing header");
		db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
		return 0;
	}

	/* Look for the revision. */
	list_rewind(lh);
	list_back(lh);

	for(; !list_rewound(lh); list_back(lh)) {
		lp = list_cur(lh);
		if(sscanf((char *)lp->l_data, revstr, &db->db_rev) == 1)
			break;
	}

	/* We have a revision. */
	if(!list_rewound(lh)) {
		/* Make sure the revision is positive. */
		if(db->db_rev < 0) {
			cddbd_snprintf(errstr, CDDBBUFSIZ,
			    "revision in comments less 0 (%d)", db->db_rev);
			db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
			return 0;
		}

		db->db_flags |= DB_REVISION;

		/* Delete extra revisions. */
		for(list_back(lh); !list_rewound(lh);) {
			lp = list_cur(lh);
			list_back(lh);

			if(sscanf((char *)lp->l_data, revstr, &n) == 1) {
				list_delete(lh, lp);
				db->db_flags &= ~DB_REVISION;
			}
		}
	}
	else {
		cddbd_snprintf(buf, sizeof(buf), revstr, CDDBMINREV);
		strcat(buf, "\n");

		p = (char *)strdup(buf);
		if(p == 0) {
			cddbd_snprintf(errstr, CDDBBUFSIZ,
			    "can't malloc list data");
			db_cleanup(fp, db, DE_NOMEM, (flags | DF_DONE));
			return 0;
		}

		/* Put string in the list. */
		if(list_add_back(lh, (void *)p) == 0) {
			cddbd_snprintf(errstr, CDDBBUFSIZ,
			    "can't malloc list entry");
			db_cleanup(fp, db, DE_NOMEM, (flags | DF_DONE));
			return 0;
		}
	}

	/* Look for the submitter. */
	list_rewind(lh);
	list_back(lh);

	for(; !list_rewound(lh); list_back(lh)) {
		lp = list_cur(lh);
		if(!strncmp((char *)lp->l_data, substr, sublen))
			break;
	}

	/* Do we have a submitter? */
	if(list_rewound(lh)) {
		/* No submitter, this may be an error. */
		if(flags & DF_SUBMITTER) {
			cddbd_snprintf(errstr, CDDBBUFSIZ,
			    "missing submitter in comments");
			db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
			return 0;
		}

		if((flags & DF_CK_SUBMIT) &&
		    ck_client_perms("", "", IF_SUBMIT) != CP_ALLOW) {
			cddbd_snprintf(errstr, CDDBBUFSIZ,
			    "missing submitter in comments");
			db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
			return 0;
		}
	}
	else {
		/* Make sure the submitter is valid. */
		if(sscanf(&((char *)lp->l_data)[sublen], "%s%s", buf, buf2)
		    != 2) {
			cddbd_snprintf(errstr, CDDBBUFSIZ,
			    "invalid submitter definition");
			db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
			return 0;
		}

		if(flags & DF_CK_SUBMIT &&
		    ck_client_perms(buf, buf2, IF_SUBMIT) != CP_ALLOW) {
			cddbd_snprintf(errstr, CDDBBUFSIZ,
			    "unauthorized client/revision: %s %s", buf, buf2);
			db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
			return 0;
		}

		db->db_flags |= DB_SUBMITTER;

		/* Delete extra submitters. */
		for(list_back(lh); !list_rewound(lh);) {
			lp = list_cur(lh);
			list_back(lh);

			if(!strncmp((char *)lp->l_data, substr, sublen)) {
				list_delete(lh, lp);
				db->db_flags &= ~DB_SUBMITTER;
			}
		}
	}

	/* Look for the last software to process this entry. */
	list_rewind(lh);
	list_forw(lh);

	for(; !list_rewound(lh); list_forw(lh)) {
		lp = list_cur(lh);
		if(!strncmp((char *)lp->l_data, prcstr, prclen))
			break;
	}

	/* Do we have a processor? */
	if(!list_rewound(lh)) {
		db->db_flags |= DB_PROCESSOR;

		list_rewind(lh);

		/* Delete all processors. */
		for(list_forw(lh); !list_rewound(lh);) {
			lp = list_cur(lh);
			list_forw(lh);

			if(!strncmp((char *)lp->l_data, prcstr, prclen)) {
				list_delete(lh, lp);
				db->db_flags &= ~DB_PROCESSOR;
			}
		}
	}

	cddbd_snprintf(buf, sizeof(buf), "%s ", prcstr);
	cddbd_snprintf(buf2, sizeof(buf2), verstr2, VERSION, PATCHLEVEL);
	strcat(buf, buf2);
	strcat(buf, "\n");

	p = (char *)strdup(buf);
	if(p == 0) {
		cddbd_snprintf(errstr, CDDBBUFSIZ, "can't malloc list data");
		db_cleanup(fp, db, DE_NOMEM, (flags | DF_DONE));
		return 0;
	}

	/* Put it after the revision. */
	list_rewind(lh);
	list_forw(lh);

	for(; !list_rewound(lh); list_forw(lh)) {
		lp = list_cur(lh);
		if(sscanf((char *)lp->l_data, revstr, &db->db_rev) == 1)
			break;
	}

	/* No rev, put it before the terminating blanks. */
	if(list_rewound(lh)) {
		for(list_back(lh); !list_rewound(lh); list_back(lh))
			if(!is_blank((char *)list_cur(lh)->l_data, 0))
				break;
	}

	list_forw(lh);

	/* Put string in the list. */
	if(list_add_cur(lh, (void *)p) == 0) {
		cddbd_snprintf(errstr, CDDBBUFSIZ, "can't malloc list entry");
		db_cleanup(fp, db, DE_NOMEM, (flags | DF_DONE));
		return 0;
	}

	/* Make sure the last line is blank. */
	list_rewind(lh);

	if(!is_blank((char *)list_back(lh)->l_data, 0)) {
		p = (char *)strdup("\n");
		if(p == 0) {
			cddbd_snprintf(errstr, CDDBBUFSIZ,
			    "can't malloc list data");
			db_cleanup(fp, db, DE_NOMEM, (flags | DF_DONE));
			return 0;
		}

		/* Put string in the list. */
		if(list_add_back(lh, (void *)p) == 0) {
			cddbd_snprintf(errstr, CDDBBUFSIZ,
			    "can't malloc list entry");
			db_cleanup(fp, db, DE_NOMEM, (flags | DF_DONE));
			return 0;
		}
	}

	/* Are there track offsets in the comments? */
	list_rewind(lh);
	list_forw(lh);

	for(list_forw(lh); !list_rewound(lh); list_forw(lh)) {
		lp = list_cur(lh);
		if(!strncmp((char *)lp->l_data, trkstr, trklen))
			break;
	}

	/* Fail if not. */
	if(list_rewound(lh)) {
		cddbd_snprintf(errstr, CDDBBUFSIZ,
		    "missing TOC information in comments");
		db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
		return 0;
	}

	/* Make sure there is an offset list. */
	list_forw(lh);
	lp = list_cur(lh);
	if(sscanf((char *)lp->l_data, offstr, &off) != 1) {
		cddbd_snprintf(errstr, CDDBBUFSIZ,
		    "non-TOC content found between TOC marker and "
		    "TOC information or no TOC information in comments");
		db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
		return 0;
	}
	list_back(lh);

	loff = -1;
	trks = 0;

	for(list_forw(lh); !list_rewound(lh); list_forw(lh), trks++) {
		lp = list_cur(lh);

		if(sscanf((char *)lp->l_data, offstr, &off) != 1)
		    break;

		/* Too many tracks. */
		if(trks >= CDDBMAXTRK) {
			cddbd_snprintf(errstr, CDDBBUFSIZ,
			    "too many track offsets in comments (%d), max %d",
			    trks, CDDBMAXTRK);
			db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
			return 0;
		}

		/* No negative or zero offsets. */
		if(off <= 0) {
			cddbd_snprintf(errstr, CDDBBUFSIZ,
			    "negative or zero offset (%d) in comments", off);
			db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
			return 0;
		}

		/* Offsets must be monotonically increasing. */
		if(off <= loff) {
			cddbd_snprintf(errstr, CDDBBUFSIZ,
			    "offset (%d) less than or equal to previous "
			    "offset (%d) in comments", off, loff);
			db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
			return 0;
		}

		db->db_offset[trks] = off;
		loff = off;
	}

	db->db_flags |= DB_OFFSET;

	/* Count the number of tracks. */
	db->db_trks = (char)trks;
	
	/* Look for the disclen. */
	list_rewind(lh);
	list_forw(lh);
	list_forw(lh);

	for(; !list_rewound(lh); list_forw(lh)) {
		lp = list_cur(lh);
		if(sscanf((char *)lp->l_data, lenstr, &db->db_disclen) == 1)
			break;
	}

	/* We have no disclen. */
	if(list_rewound(lh)) {
		cddbd_snprintf(errstr, CDDBBUFSIZ,
		    "missing disc length in comments");
		db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
		return 0;
	}

	/* Delete extra revisions. */
	for(list_forw(lh); !list_rewound(lh);) {
		lp = list_cur(lh);
		if(sscanf((char *)lp->l_data, lenstr, &n) == 1)
			list_delete(lh, lp);
		else
			list_forw(lh);
	}

	/* Make sure the disclen is positive. */
	if(db->db_disclen <= 0) {
		cddbd_snprintf(errstr, CDDBBUFSIZ,
		    "disc length in comments less than or equal to 0 (%d)",
		    db->db_disclen);
		db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
		return 0;
	}

	/* Make sure the disclen isn't too short. */
	if((loff / CDDBFRAMEPERSEC) > db->db_disclen) {
		cddbd_snprintf(errstr, CDDBBUFSIZ,
		    "disc length in comments is too short (%d)",
		    db->db_disclen);
		db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
		return 0;
	}

	db->db_flags |= DB_DISCLEN;

	/* Ensure the discid is in the DB entry. */
	discid = db_gen_discid(db);

	if(list_find(db->db_phase[DP_DISCID], uint_as_ptr (discid)) == 0) {
		cddbd_snprintf(errstr, CDDBBUFSIZ,
		    "disc ID generated from track offsets (%08x) not found "
		    "in %s", discid, parstab[DP_DISCID].dp_name);
		db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
		return 0;

	}

	/* Ensure the discids are legal. */
	lh = db->db_phase[DP_DISCID];
	list_rewind(lh);
	list_forw(lh);

	for(; !list_rewound(lh); list_forw(lh)) {
		lp = list_cur(lh);
		discid = ptr_to_uint32 (lp->l_data);

		if((discid & 0xFF) != db->db_trks) {
			cddbd_snprintf(errstr, CDDBBUFSIZ,
			    "invalid disc ID %08x in %s",
			    discid, parstab[DP_DISCID].dp_name);
			db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
			return 0;
		}
	}

	/* Check the fields for various things. */
	for(i = 0; i < DP_NPHASE; i++) {
		lh = db->db_phase[i];
		dp = &parstab[i];

		/* Make sure multi fields match the track count. */
		if((dp->dp_flags & PF_MULTI) && db->db_trks != lh->lh_count) {
			cddbd_snprintf(errstr, CDDBBUFSIZ,
			    "# of %s fields (%d) does not match # of trks (%d)",
			    dp->dp_name, lh->lh_count, db->db_trks);
			db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
			return 0;
		}

		if(dp->dp_flags & PF_MULTI)
			fc = lh->lh_count;
		else
			fc = 1;

		/* Make sure the field contents aren't bogus. */
		for(j = 0, n = 0; j < fc; j++) {
			/* Check for invalid stuff. */
			dc = db_content_check(db, i, j);

			if(dc->dc_flags & CF_INVALID) {
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "invalid %s", db_field(db, i, j));
				db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
				return 0;
			}

			if((dp->dp_flags & PF_REQUIRED) &&
			    (dc->dc_flags & CF_BLANK)) {
				cddbd_snprintf(errstr, CDDBBUFSIZ,
				    "empty %s", db_field(db, i, j));
				db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
				return 0;
			}

			if(!(dc->dc_flags & CF_BLANK))
				n++;
		}

		/* Check fields that must have at least one nonempty entry. */
		if((dp->dp_flags & PF_ONEREQ) && n == 0 && db->db_trks > 1) {
			cddbd_snprintf(errstr, CDDBBUFSIZ,
			    "no valid %s in entry", dp->dp_name);
			db_cleanup(fp, db, DE_INVALID, (flags | DF_DONE));
			return 0;
		}
		else if(n != i)
			db->db_flags |= DB_EMPTY;
	}

	return db;
}


/** 
 * Write one record to an open file.
 * 
 * The functions which reference it can be grouped as:
 * - which need the original UNIX format
 *     - #cddbd_do_transmit - only single temporary file at a time
 *     - #cddbp_transmit - writes to a socket
 *     - #do_cddb_read - writes to stdout
 *     .
 * - which need the Windows format when compiled for Windows
 *   (Unix otherwise)
 *     - #cddbd_update - file (insert/update, uses discid when Win.)
 *     - #db_post - file (insert/update, uses discid when Win.)
 *     .
 * .
 *
 * @param fp     opened file the record will be written to
 * @param db     DB record to be written
 * @param level  actually not used
 * @param discid the discid of the record. If nonzero a "Windows" line 
 *               #FILENAME=discid will be prepended.
 * 
 * @return 1 if success, 0 otherwise
 */
#ifdef DB_WINDOWS_FORMAT
int db_write(FILE *fp, db_t *db, int level, unsigned int discid)
#else
int db_write(FILE *fp, db_t *db, int level)
#endif
{
	int i;
	int x;
	lhead_t *lh;
	db_parse_t *dp;

#ifdef DB_WINDOWS_FORMAT
	if(discid > 0) {
		fprintf(fp, "%s%08x\n", STARTTAG, discid);
	}
#endif
	for(i = 0; i < DP_NPHASE; i++) {
		dp = &parstab[i];
		lh = db->db_phase[i];

		if (PROTO_ENAB(P_READ) || ((i != DP_DYEAR) && (i != DP_DGENRE))) {
			if(dp->dp_flags & PF_MULTI)
				x = db_write_multi(fp, lh, dp);
			else if(dp->dp_flags & PF_NUMERIC)
				x = db_write_num(fp, lh, dp);
			else
				x = db_write_str(fp, lh, dp, 0);

			if(x == 0) return 0;
		}
	}

	return 1;
}


int
db_write_multi(FILE *fp, lhead_t *lh, db_parse_t *dp)
{
	int i;
	link_t *lp;

	for(i = 0, list_rewind(lh), list_forw(lh); !list_rewound(lh);
	    i++, list_forw(lh)) {
		lp = list_cur(lh);
		if(db_write_str(fp, (lhead_t *)lp->l_data, dp, i) == 0)
			return 0;
	}

	return 1;
}


int
db_write_num(FILE *fp, lhead_t *lh, db_parse_t *dp)
{
	int i;
	int n;
	char *s;
	link_t *lp;

	/* Compute how many discids we can put on a line. */
	n = DBLINESIZ - strlen(dp->dp_pstr);
	n /= CDDBDISCIDLEN + 1;

	for(i = 0, list_rewind(lh), list_forw(lh); i < list_count(lh); i++,
	    list_forw(lh)) {
		lp = list_cur(lh);

		if(!(i % n) && fputs(dp->dp_pstr, fp) == EOF)
			return 0;

		if(fprintf(fp, "%08x", ptr_to_uint32 (lp->l_data)) == EOF)
			return 0;

		if(i == (list_count(lh) - 1) || !((i + 1) % n)) {
			if(fp == stdout)
				s = "\r\n";
			else
				s = "\n";
		}
		else {
			s = ",";
		}

		if(fputs(s, fp) == EOF)
			return 0;
	}

	return 1;
}


int
db_write_str(FILE *fp, lhead_t *lh, db_parse_t *dp, int trk)
{
	link_t *lp;
	char buf[CDDBBUFSIZ];

	for(list_rewind(lh), list_forw(lh); !list_rewound(lh); list_forw(lh)) {
		lp = list_cur(lh);

		if(fprintf(fp, dp->dp_pstr, trk) == EOF)
			return 0;

		if(fp == stdout) {
			strncpy(buf, (char *)lp->l_data, sizeof(buf));
			buf[sizeof(buf) - 1] = '\0';
			strip_crlf(buf);

			if(fprintf(fp, "%s\r\n", buf) == EOF)
				return 0;
		}
		else {
			if(fputs((char *)lp->l_data, fp) == EOF)
				return 0;
		}
	}

	return 1;
}

/** 
 * Compares two unsigned int arguments (used for qsort)
 * 
 * @param a first argument
 * @param b second argument
 * 
 * @return comparsion result (<, ==, >0 when a<b, a==b, a>b).
 */
int comp_uint(const void * a, const void * b) {
	unsigned int x = *((unsigned int *)a);
	unsigned int y = *((unsigned int *)b);
	if(x < y)
		return -1;
	if(x == y)
		return 0;
	return 1;
}

/** 
 * Copies one record from opened file src to opened file dest.
 *
 * Reads one record from the src (up to the EOF or #FILENAME tag)
 * and appends it to the dest using the Windows format (hence the input
 * parameter srcDiscid). If another record in src was encountered after
 * this one (due to the tag #FILENAME), the corresponding discid is returned
 * in nextDiscid, zero otherwise (note that zero is not a valid discid). 
 * When no output required the nextDiscid can be NULL.
 *
 * Both files remain opened, the src is just after the line #FILENAME (if 
 * found). The global variables #db_curr_discid and 
 * #db_next_discid are NOT affected by this function.
 * 
 * @param dest       destination opened file the record will be written to
 * @param src        source file from which the record will be copied
 * @param srcDiscid  discid of the record for the #FILENAME tag
 * @param nextDiscid returns next encountered value of (if any) #FILENAME
 * 
 * @return 0 when success
 */
#ifdef DB_WINDOWS_FORMAT
int db_copy_entry(FILE *dest, 
                  FILE *src,
                  unsigned int srcDiscid,
                  unsigned int * nextDiscid) {
	char buf[CDDBBUFSIZ];

	fprintf(dest, "%s%08x\n", STARTTAG, srcDiscid);

	while(fgets(buf, sizeof(buf), src) != NULL) {
		if(strncmp(buf, STARTTAG, STARTTAGLEN) == 0) {
			if(nextDiscid != NULL) {
				if(sscanf(buf+STARTTAGLEN, "%08x", nextDiscid)
				   != 1) {
					cddbd_log(LOG_ERR,
					          "Can't parse discid from <%s>",
					          buf+STARTTAGLEN);
					*nextDiscid = 0;
					return 1;
				}
				return 0; /* found/parsed next #FILENAME=discid */
			}
			return 0; /* found/parsed next #FILENAME= */
		}
		if(fputs(buf, dest) == EOF) {
			cddbd_log(LOG_ERR, "Can't write to file");
			return 1;
		}
	}

	if(nextDiscid != NULL) {
		*nextDiscid = 0;
	}
	return 0;
}
#endif /* DB_WINDOWS_FORMAT */

/** 
 * Merge Unix format files into a (existing) Windows database.
 *
 * All the records from unixDir are merged into Windows DB file(s) winDir.
 * The unixDir records take precedence over the original ones (i.e. these
 * will be overwritten).
 * 
 * @param unixDir source directory containing the records (in Unix format)
 *                to be merged in. This can be the same as winDir.
 * @param winDir  output directory - where the Unix records should be merged in.
 *                it can be also empty. It is assumed that the records in
 *                the already existing files are sorted.
 * @param delete  if != 0 the source files in unixDir will be deleted afterwards.
 * 
 * @return 0 if success
 */
#ifdef DB_WINDOWS_FORMAT
int db_merge_to_windb(char * unixDir, char * winDir, int delete)
{
	unsigned int   discIDs[WINMAXPOSTRECORDS+10];
	DIR *dir;
	struct dirent *dp;
	char           file[CDDBBUFSIZ];     /**< result file of a operation */
	char           unixFile[CDDBBUFSIZ]; /**< Unix file to be merged in */
	char           srcWinFile[CDDBBUFSIZ];/**< Source Win file we are merging in */
	FILE          *fp;                   /**< result file of a operation */
	FILE          *fpUnix;               /**< Unix file to be merged in */
	FILE          *srcWin;               /**< Source Win file we are merging in */
	unsigned int   numRecords;
	unsigned int   discid;
	unsigned int   start, stop;
	unsigned int   winDiscid;
	int            dstDBmap[0xff][2]; /**< maps high discid byte to filename*/
	int            a, ci, highByte, nextHighByte;

	/* First of all, get the lock for merging
	   in the current directory. */
	if((ci = categ_index(cddbd_basename(winDir))) == -1) {
		cddbd_log(LOG_ERR | LOG_UPDATE,
		          "Windows directory %s to merge into "
			  "does not correspond to a category.",
			  winDir);
		quit(QUIT_ERR);
	}

	cddbd_lock(categlocks[ci][CATLK_MERGE], 1);

	for(a=0; a<0xff; ++a)
		dstDBmap[a][0] = dstDBmap[a][1] = -1;

	/* 
	 * Prepare a list of Unix files to merge. I.e. go through the directory
	 * and note all the Unix format filenames to discIDs and sort it afterwards.
	 */
	if((dir = opendir(unixDir)) == NULL) {
		cddbd_log(LOG_ERR, 
		          "Warning: can't open dir. <%s>",
		          unixDir);
		cddbd_unlock(categlocks[ci][CATLK_MERGE]);
		return 1;
	}
	numRecords = 0;

	while((dp = readdir(dir)) != NULL) {
		if(strlen(dp->d_name) != CDDBDISCIDLEN) {
			continue;
		}
		if(sscanf(dp->d_name, "%08x", &discid) != 1) {
			cddbd_log(LOG_ERR,
			          "Warning: ignoring non-CDDB"
			          "file: %s", file);
			continue;
		}
		if(numRecords > WINMAXPOSTRECORDS) {
			cddbd_log(LOG_ERR,
			          "Warning: too many files (%d) in %s, expected max. %d",
			          numRecords,
			          unixDir,
			          WINMAXPOSTRECORDS);
			break;
		}
		discIDs[numRecords++] = discid;
	}
	closedir(dir);
	qsort(discIDs, numRecords, sizeof(discIDs[0]), comp_uint);


	/*
	 * Prepare the destination Windows file map. I.e. dstDBmap will contain
	 * for each "high byte" (possible highest value of the discid) start and 
	 * stop value of the corresponding Windows range file (if exists). 
	 * Otherwise the limits -1 from the initialisation will be there.
	 */
	if((dir = opendir(winDir)) == NULL) {
		cddbd_log(LOG_ERR, 
		          "Warning: can't open dir. <%s>",
		          winDir);
		cddbd_unlock(categlocks[ci][CATLK_MERGE]);
		return 1;
	}
	while((dp = readdir(dir)) != NULL) {
		cddbd_snprintf(file, sizeof(file), "%s/%s", winDir,
		               dp->d_name);
		if(strlen(dp->d_name) != WINFILEIDLEN) {
			continue;
		}

		start = 0;
		stop = 0;
		if(sscanf(dp->d_name, "%02xto%02x", &start, &stop) != 2) {
			cddbd_log(LOG_ERR,
			          "Warning: ignoring non-CDDB"
			          "file: %s", file);
			continue;
		}
		for(a=start; a<=stop; ++a) {
			dstDBmap[a][0] = start;
			dstDBmap[a][1] = stop;
		}
	}
	closedir(dir);

	/* 
	 * Now the merge phase. There are two cases:
	 *  - there is no corresponding Windows file (i.e. we will create one)
	 *  - there is already a corresponding Windows file so we need to merge it in
	 */
	fpUnix = fp = NULL;
	a = 0;
	while(a < numRecords) {
		highByte = nextHighByte = (discIDs[a] >> 24) & 0xff;
		if(dstDBmap[highByte][0] < 0 ) {
			/*
			 * File does not exist. So we create a Windows file xxtoxx and write there
			 * all Unix files with that high byte.
			 */
			cddbd_snprintf(file, sizeof(file), "%s/%02xto%02x", winDir,
			               highByte, highByte);
			if((fp=fopen(file,"w")) == NULL) {
				cddbd_log(LOG_ERR,
				          "Warning: cannot create file: %s",
				          file);
				cddbd_unlock(categlocks[ci][CATLK_MERGE]);
				return 1;
			}

			do /* copy all the records for this new Win file */ {
				highByte = nextHighByte;
				cddbd_snprintf(unixFile, sizeof(unixFile), "%s/%08x", unixDir,
				               discIDs[a]);
				if((fpUnix=fopen(unixFile, "r")) == NULL) {
					cddbd_log(LOG_ERR,
					          "Warning: cannot open file: %s",
					          unixFile);
					fclose(fp);
					cddbd_unlock(categlocks[ci][CATLK_MERGE]);
					return 1;
				}
				if(db_skip(fpUnix, NULL)) {
					cddbd_log(LOG_ERR, "Warning: cannot skip to record in file: %s",
					          unixFile);
					fclose(fpUnix);
					fclose(fp);
					cddbd_unlock(categlocks[ci][CATLK_MERGE]);
					return 1;
				}
				if(db_copy_entry(fp, fpUnix, discIDs[a], NULL)) {
					cddbd_log(LOG_ERR,
					          "Warning: cannot copy over one record - file: %s",
					          unixFile);
					fclose(fpUnix);
					fclose(fp);
					cddbd_unlock(categlocks[ci][CATLK_MERGE]);
					return 1;
				}
				fclose(fpUnix); /* it is already copied over */
				if(delete) {
					if(unlink(unixFile)) {
						cddbd_log(LOG_ERR,
						          "Warning: cannot unlink file: %s",
						          unixFile);
						fclose(fp);
						cddbd_unlock(categlocks[ci][CATLK_MERGE]);
						return 1;
					}
				}
				++a;
				if(a >= numRecords) /* last record? */
					break;

				nextHighByte = (discIDs[a] >> 24) & 0xff;
			} while(nextHighByte == highByte); /* are we still within the Win file? */
			fclose(fp);
			fp = NULL;
			(void)cddbd_fix_file(file, db_file_mode, db_uid, db_gid);
		}
		else {
			/*
			 * File already exists. Create a temp file xxtoyy_temp, merge into it original
			 * xxtoyy and corresponding Unix files and finaly rename the xxtoyy_temp
			 * to xxtoyy.
			 */
			start = dstDBmap[highByte][0]; /* start of this Win file */
			stop = dstDBmap[highByte][1];  /* stop of this Win file */
			cddbd_snprintf(srcWinFile, 
			               sizeof(srcWinFile), 
			               "%s/%02xto%02x", 
			               winDir,
			               dstDBmap[highByte][0],
			               dstDBmap[highByte][1]);
			cddbd_snprintf(file, 
			               sizeof(file), 
			               "%s/%02xto%02x_temp", 
			               winDir,
			               dstDBmap[highByte][0],
			               dstDBmap[highByte][1]);
			if((srcWin=fopen(srcWinFile, "r")) == NULL) {
				cddbd_log(LOG_ERR,
				          "Warning: cannot open file: %s",
				          srcWinFile);
				cddbd_unlock(categlocks[ci][CATLK_MERGE]);
				return 1;
			}
			if((fp=fopen(file, "w")) == NULL) {
				cddbd_log(LOG_ERR,
				          "Warning: cannot create file: %s",
				          file);
				fclose(srcWin);
				cddbd_unlock(categlocks[ci][CATLK_MERGE]);
				return 1;
			}

			/* do the merging magic */
			if(db_skip(srcWin, NULL)) {
				cddbd_log(LOG_ERR, "Warning: cannot skip to record in file: %s",
				          srcWinFile);
				fclose(fp);
				fclose(srcWin);
				cddbd_unlock(categlocks[ci][CATLK_MERGE]);
				return 1;
			}
			winDiscid = db_curr_discid;
			do {
				if(winDiscid < discIDs[a]) { /* take it from the srcWin (lower discid) */
					if(db_copy_entry(fp, srcWin, winDiscid, &winDiscid)) {
						cddbd_log(LOG_ERR,
						          "Warning: cannot copy over one record - file: %s",
						          srcWinFile);
						fclose(srcWin);
						fclose(fp);
						cddbd_unlock(categlocks[ci][CATLK_MERGE]);
						return 1;
					}

					if(winDiscid == 0) { /* the src win file is exhausted */
						fclose(srcWin);
						srcWin = NULL;
						winDiscid = 0xffffffff;
					}
				}
				else { /* write the Unix record and decide later whether to skip Win */
					cddbd_snprintf(unixFile,
					               sizeof(unixFile),
					               "%s/%08x", 
					               unixDir,
					               discIDs[a]);
					if((fpUnix=fopen(unixFile, "r")) == NULL) {
						cddbd_log(LOG_ERR,
						          "Warning: cannot open file: %s",
						          unixFile);
						if(srcWin != NULL)
							fclose(srcWin);
						fclose(fp);
						cddbd_unlock(categlocks[ci][CATLK_MERGE]);
						return 1;
					}
					if(db_skip(fpUnix, NULL)) {
						cddbd_log(LOG_ERR, "Warning: cannot skip to record in file: %s",
						          unixFile);
						fclose(fpUnix);
						if(srcWin != NULL)
							fclose(srcWin);
						fclose(fp);
						cddbd_unlock(categlocks[ci][CATLK_MERGE]);
						return 1;
					}
					if(db_copy_entry(fp, fpUnix, db_curr_discid, NULL)) {
						cddbd_log(LOG_ERR,
						          "Warning: cannot copy over one record - file: %s",
						          unixFile);
						fclose(fpUnix);
						if(srcWin != NULL)
							fclose(srcWin);
						fclose(fp);
						cddbd_unlock(categlocks[ci][CATLK_MERGE]);
						return 1;
					}
					fclose(fpUnix);

					/* eventually delete the unix file */
					if(delete) {
						if(unlink(unixFile)) {
							cddbd_log(LOG_ERR,
							          "Warning: cannot unlink file: %s",
							          unixFile);
							if(srcWin != NULL)
								fclose(srcWin);
							fclose(fp);
							cddbd_unlock(categlocks[ci][CATLK_MERGE]);
							return 1;
						}
					}

					if(winDiscid == discIDs[a]) { 
						/* We are "overwriting" with the Unix record 
						   (same discid) => skip the record 
						   in the srcWinFile */
						if(db_skip(srcWin, NULL)) {
							cddbd_log(LOG_ERR, "Warning: cannot skip to record in file: %s",
							          srcWinFile);
							fclose(fp);
							if(srcWin != NULL)
								fclose(srcWin);
						}
						winDiscid = db_curr_discid;
					}

					++a;
					if(a >= numRecords) /* last record? */
						break;
					highByte = (discIDs[a] >> 24) & 0xff;
				}
			} while(highByte <= stop); /* are we still within this Win file? */

/* copy the rest of the srcWin to file */
			if(srcWin != NULL) {
				while(winDiscid > 0) { 
					if(db_copy_entry(fp, srcWin, winDiscid, &winDiscid)) {
						cddbd_log(LOG_ERR,
						          "Warning: cannot copy over one record - file: %s",
						          srcWinFile);
						fclose(srcWin);
						fclose(fp);
					}
				}
				fclose(srcWin);
				srcWin = NULL;
			}
			fclose(fp);
			fp = NULL;

/* and finally rename file to srcWinFile (overwrite the original one) */
			if(rename(file, srcWinFile)) {
				cddbd_log(LOG_ERR,
				          "Warning: cannot rename file %s to %s (%d)",
				          file,
				          srcWinFile,
				          errno);
				cddbd_unlock(categlocks[ci][CATLK_MERGE]);
				return 1;
			}
			(void)cddbd_fix_file(srcWinFile, db_file_mode, db_uid, db_gid); /* to be sure */
		}
	}
	cddbd_unlock(categlocks[ci][CATLK_MERGE]);
	return 0;
}
#endif

/** 
 * Posts (creates/updates) a new DB record.
 *
 * Used by:
 *   - #cddbd_submit - just add/update (by email submission in post dir.)
 *   - #do_cddb_write - just add/update (directly writes to the post dir.)
 *   - #cddbd_check_db - update in the live database
 *
 * Note that even for Windows format it creates a Unix format entry and 
 * it is merged into the database using #db_merge_to_windb.
 *
 * @param db     DB record
 * @param dir    directory where it should be "posted"
 * @param discid discid of the DB record. Also see #db_write
 * @param errstr error string
 * 
 * @return 0 if successful
 */
int db_post(db_t *db, char *dir, unsigned int discid, char *errstr)
{
	FILE *fp;
	int link = 0;
	char file[CDDBBUFSIZ];
	char name[CDDBDISCIDLEN + 1];
	struct stat sbuf;

	/* Convert to appropriate charset for saving. */
	if(db->db_flags & DB_ENC_ASCII)
		;
	else if(db->db_flags & DB_ENC_UTF8)
		switch (file_charset) {
		case FC_ONLY_ISO:
			db_utf8_latin1(db);
			break;
		case FC_PREFER_ISO:
			if (!db_utf8_latin1_exact(db))
				if (db_looks_like_utf8(db))
					db_latin1_utf8(db);
			break;
		default:
			break;
		}
	else if(db->db_flags & DB_ENC_LATIN1)
		switch (file_charset) {
		default:
		case FC_ONLY_ISO:
			break;
		case FC_PREFER_ISO:
			if (db_looks_like_utf8(db))
				db_latin1_utf8(db);
			break;
		case FC_PREFER_UTF:
		case FC_ONLY_UTF:
			db_latin1_utf8(db);
			break;
		}
	else {
		cddbd_log(LOG_ERR, "Internal error in db_post.");
		return 0;
	}

	cddbd_snprintf(name, sizeof(name), "%08x", discid);

	/* Make sure discid is in the DB entry. */
	if(!list_find(db->db_phase[DP_DISCID], uint_as_ptr (discid))) {
		cddbd_snprintf(errstr, CDDBBUFSIZ,
		    "discid %s is not in the DB entry", name);

		db_errno = DE_INVALID;
		return 0;
	}

	/* Check to see if we're writing to the database directly   */
	if(!strcmp(cddbd_dirname(dir), cddbdir))
		link = 1;                 /* direct write to db */
	
	/* If writing to the database directly,
	   remove existing file and all linked files. 
	   Nice idea, but is inconsistent with what happens in cddbd_update.
	   Radically unlinking in cddbd_update as well is no solution, since
	   we don't want to remove other entries if they are not linked, but
	   independent entries for other discs. Link-handling is really a mess,
	   so it's best to keep the damage as small as possible until someone
	   rewrites a lot of code for linked entries or gets rid of linking
	   alltogether!
		if(link)
		db_unlink(dir, name);
	*/
	if(stat(dir, &sbuf)) {
		if(mkdir(dir, (mode_t)db_dir_mode)) {
			cddbd_log(LOG_ERR, "Failed to create post dir %s.",
			    dir);

			cddbd_snprintf(errstr, CDDBBUFSIZ,
			    "file access failed");

			db_errno = DE_FILE;
			return 0;
		}

		(void)cddbd_fix_file(dir, db_dir_mode, db_uid, db_gid);
	}
	else if(!S_ISDIR(sbuf.st_mode)) {
		cddbd_log(LOG_ERR, "%s is not a directory.", dir);
		cddbd_snprintf(errstr, CDDBBUFSIZ, "file access failed");
		db_errno = DE_FILE;

		return 0;
	}

	/* Open file to be written. */
	cddbd_snprintf(file, sizeof(file), "%s/%s", dir, name);

	/* file to be created/updated */
	if((fp = fopen(file, "w")) == NULL) {
		cddbd_log(LOG_ERR, "Couldn't open post file: %s (%d).",
		    file, errno);

		cddbd_snprintf(errstr, CDDBBUFSIZ, "file access failed");
		db_errno = DE_FILE;
		return 0;
	}

#ifdef DB_WINDOWS_FORMAT
	if(!db_write(fp, db, MAX_PROTO_LEVEL, discid)) {
#else
	if(!db_write(fp, db, MAX_PROTO_LEVEL)) {
#endif
		cddbd_snprintf(errstr, CDDBBUFSIZ, "server filesystem full");
		db_errno = DE_FILE;

		fclose(fp);
		unlink(file);

		return 0;
	}

	/* zeke - if we have header info, add this to the end of the db */
	/*        submission - but do not add if post dir == db dir.    */
	if (!link && db->db_eh.eh_charset != -1 && db->db_eh.eh_encoding != -1) {
	char buf[CDDBBUFSIZ * 4];
	cddbd_snprintf(buf, sizeof(buf), 
		"## Cs: %01d\n## En: %01d\n## To: %s\n## Rc: %s\n## Ho: %s\n",
		db->db_eh.eh_charset,
		db->db_eh.eh_encoding,
		db->db_eh.eh_to,
		db->db_eh.eh_rcpt,
		db->db_eh.eh_host);

	if(fputs(buf, fp) == EOF) {
		cddbd_snprintf(errstr, CDDBBUFSIZ, "server filesystem full");
		db_errno = DE_FILE;

		fclose(fp);
		unlink(file);
		return 0;
		}
	}

	fclose(fp);

	/* Create any needed links. */
	if(link)
		db_link(db, dir, name, 1);

	/* Note if we can't set stats, but continue. */
	(void)cddbd_fix_file(file, db_file_mode, db_uid, db_gid);

#ifdef DB_WINDOWS_FORMAT
	/* If we're writing directly to the database,
	   merge the posted entry here since
	   cddbd_update() will short-circuit when
	   postdir is the same as cddbdir
	*/
	if(link && db_merge_to_windb(dir, dir, 1)) {
		cddbd_log(LOG_ERR | LOG_UPDATE,
				"Failed to merge in %s.",
				dir);
		quit(QUIT_ERR);
	}
#endif

	db_errno = DE_NO_ERROR;
	return 1;
}


void
db_free(db_t *db)
{
	int i;
	
	if (db == 0){
		return;
	}

	for(i = 0; i < DP_NPHASE; i++)
		if(db->db_phase[i] != 0)
			list_free(db->db_phase[i]);

	free(db);
}


void
db_free_string(void *p)
{
	free(p);
}


void
db_free_multi(void *lh)
{
	list_free((lhead_t *)lh);
}


int
db_classify(char *buf, int *n, char **p)
{
	int i;
	int len;
	db_parse_t *dp;
	char kbuf[CDDBBUFSIZ];

	for(i = 0; i < DP_NPHASE; i++)
		if(!strncmp(parstab[i].dp_str, buf, strlen(parstab[i].dp_str)))
			break;

	if(i == DP_NPHASE)
		return -1;

	dp = &parstab[i];

	if(dp->dp_flags & PF_MULTI) {
		if(sscanf(buf, dp->dp_pstr, n) != 1)
			return -1;
		cddbd_snprintf(kbuf, sizeof(kbuf), dp->dp_pstr, *n);
	}
	else {
		*n = -1;
		strcpy(kbuf, dp->dp_pstr);
	}

	len = strlen(kbuf);
	if(strncmp(kbuf, buf, len))
		return -1;

	*p = &buf[len];

	return i;
}


int
db_parse_discid(char **p, unsigned int *discid)
{
	if(sscanf(*p, "%08x", discid) != 1)
		return 1;

	if(strlen(*p) < CDDBDISCIDLEN)
		return 1;

	*p += CDDBDISCIDLEN;

	if(**p == '\r')
		(*p)++;

	if(**p != ',' && **p != '\n' && **p != '\0')
		return 1;

	if(**p != '\0')
		(*p)++;

	return 0;
}


int
db_sum_discid(int n)
{
	int ret;
	char *p;
	char buf[12];

	cddbd_snprintf(buf, sizeof(buf), "%lu", (unsigned int)n);

	for(ret = 0, p = buf; *p != '\0'; p++)
		ret += *p - '0';

	return ret;
}


unsigned int
db_gen_discid(db_t *db)
{
	int i;
	int n;
	int t;
	int offtab[CDDBMAXTRK + 1];

	/* Convert to seconds. */
	for(i = 0; i < db->db_trks; i++)
		offtab[i] = db->db_offset[i] / CDDBFRAMEPERSEC;

	/* Compute the discid. */
	for(i = 0, t = 0, n = 0; i < (db->db_trks - 1); i++) {
		n += db_sum_discid(offtab[i]);
		t += offtab[i + 1] - offtab[i];
	}

	n += db_sum_discid(offtab[i]);
	t += db->db_disclen - offtab[i];

	return(((n % 0xff) << 24) | (t << 8) | db->db_trks);
}


void
db_cleanup(FILE *fp, db_t *db, int err, int flags)
{
	char buf[CDDBBUFSIZ];

	/* Eat input until we hit the end. */
	if(!(flags & DF_DONE)) {
#ifdef DB_WINDOWS_FORMAT
		if (flags & DF_WIN)
			db_curr_discid = db_next_discid;
#endif
		for(;;) {
			if((flags & DF_STDIN) && !cddbd_timer_sleep(timers))
				continue;

			if(flags & DF_STDIN) { /* reading from stdin - cddb write */
				if(cddbd_gets(buf, sizeof(buf)) == NULL) /* I don't think this can ever happen */
					break;
				if(is_dot(buf)) /* terminating dot found - done with eating */
					break;
			}
			else { /* reading from file */
				if(fgets(buf, sizeof(buf), fp) == NULL) /* EOF */
				break;

#ifdef DB_WINDOWS_FORMAT
				if (flags & DF_WIN) {
					db_line_counter++;
					if(strncmp(buf, STARTTAG, STARTTAGLEN) == 0) {
						if(sscanf(buf+STARTTAGLEN, "%08x", &db_next_discid) != 1) {
							cddbd_log(LOG_ERR,
						          "Can't parse discid from <%s>",
						          buf+STARTTAGLEN);
							continue;
						}
						break;
					}
				}
#endif
			}
		}
	}

	db_free(db);

	db_errno = err;
}


void
db_strcpy(db_t *db, int phase, int n, char *to, int size)
{
	int len;
	int rsize;
	link_t *lp;
	lhead_t *lh;

	lh = db->db_phase[phase];

	if(parstab[phase].dp_flags & PF_MULTI) {
		list_rewind(lh);

		while(n >= 0) {
			list_forw(lh);
			n--;
		}

		lp = list_cur(lh);
		lh = (lhead_t *)lp->l_data;
	}

	list_rewind(lh);
	list_forw(lh);

	/* Make space for the null terminator. */
	size--;
	rsize = size;
	to[0] = '\0';

	for(; !list_rewound(lh) && size > 0; list_forw(lh)) {
		lp = list_cur(lh);

		len = strlen((char *)lp->l_data);
		if(len > size)
			len = size;

		strncat(to, (char *)lp->l_data, len);
		strip_crlf(to);
		to[rsize] = '\0';

		size -= len;
	}
}


int
db_strlen(char *p)
{
	const char *t = p; 
	int i = 0;
	
	while(*t != '\0') {
		/* parse_utf8 advances p for utf8 characters, */
		/* returns -1 for not valid as utf8 */
		if (parse_utf8(&t) == -1) t++;

		/* count character */
		i++;		
	}

	return i;
}


/* zeke - split this out to check disc collision vs rev checking  */
/* (rev checking could ret -1 as well, so need to seperate these) */
int
db_col(db_t *db1, db_t *db2)
{
	/* Same CDID, but not same data = collision */
	if(db1->db_trks != db2->db_trks || !is_fuzzy_match(db1->db_offset,
	    db2->db_offset, db1->db_disclen, db2->db_disclen, db1->db_trks))
		return 1;

	return 0;
}

int
db_cmp(db_t *db1, db_t *db2)
{
	/* Check for the magic unalterable revision. */
	if(db1->db_rev == DB_MAX_REV && db2->db_rev != DB_MAX_REV)
		return 1;

	if(db2->db_rev == DB_MAX_REV && db1->db_rev != DB_MAX_REV)
		return -1;

	/* Otherwise, compare the revision. */
	return(db1->db_rev - db2->db_rev);
}

/* unlink an entry and all other entries listed on it's DISCID line */
/* currently unused (See comment in db_post)
void
db_unlink(char *dir, char *name)
{
	FILE *fp;
	db_t *db;
	link_t *lp;
	lhead_t *lh;
	char buf[CDDBBUFSIZ];

	cddbd_snprintf(buf, sizeof(buf), "%s/%s", dir, name);

	if((fp = fopen(buf, "r")) == NULL) {
		unlink(buf);
		return;
	}

	db = db_read(fp, buf, file_df_flags);
	fclose(fp);

	if(db == 0) {
		unlink(buf);
	}
	else {
		lh = db->db_phase[DP_DISCID];

		for(list_rewind(lh), list_forw(lh); !list_rewound(lh);
		    list_forw(lh)) {
			lp = list_cur(lh);

			cddbd_snprintf(buf, sizeof(buf), "%s/%08x",
			    dir, ptr_to_uint32 (lp->l_data));

			unlink(buf);
		}

		db_free(db);
	}
}
*/

void
db_link(db_t *db, char *dir, char *name, int fix)
{
#ifdef DB_WINDOWS_FORMAT
	return; /* it does not make sense for the Windows format */
}
#else
	link_t *lp;
	lhead_t *lh;
	struct stat sbuf;
	struct stat sbuf2;
	char dbname[CDDBBUFSIZ];
	char dblink[CDDBBUFSIZ];

	cddbd_snprintf(dbname, sizeof(dbname), "%s/%s", dir, name);

	if(stat(dbname, &sbuf) != 0) {
		cddbd_log(LOG_ERR, "Can't stat DB entry: %s.", dbname);
		return;
	}

	lh = db->db_phase[DP_DISCID];

	for(list_rewind(lh), list_forw(lh); !list_rewound(lh); list_forw(lh)) {
		lp = list_cur(lh);

		cddbd_snprintf(dblink, sizeof(dblink), "%s/%08x", dir,
		    ptr_to_uint32 (lp->l_data));

		/* This should already exist. */
		if(!strcmp(dbname, dblink))
			continue;

		/* The link exists already. */
		if(stat(dblink, &sbuf2) == 0) {
			if(verbose && sbuf.st_ino != sbuf2.st_ino) {
				cddbd_log(LOG_ERR,
				    "Warning: %s (ino %u) not linked to %s "
				    "(ino %u)",
				    dblink, sbuf2.st_ino, dbname, sbuf.st_ino);
			}
			continue;
		}

		/* Can't stat it, so complain. */
		if(errno != ENOENT) {
			cddbd_log(LOG_ERR, "Can't stat DB entry: %s", dblink);
			continue;
		}

		if(fix) {
			/* Create the link. */
			if(cddbd_link(dbname, dblink) != 0) {
				cddbd_log(LOG_ERR, "Can't link %s to %s",
				    dblink, dbname);
			}

			if(verbose) {
				cddbd_log(LOG_INFO,
				    "Linked %s to %s.", dblink, dbname);
			}
		}
		else {
			if(verbose) {
				cddbd_log(LOG_INFO,
				    "Link from %s to %s missing.", dblink,
				    dbname);
			}
		}
	}
}
#endif

/* zeke - left param is target, right param is source */
/*        so merging (to)tdb with info (from)fdb...   */
int
db_merge(db_t *tdb, db_t *fdb)
{
	int merged;
	link_t *lp;
	lhead_t *tlh;
	lhead_t *flh;
	link_t *tlp;
	link_t *flp;
	int i;
	int mergephase[] = { DP_DYEAR, DP_DGENRE };

	merged = 0;

	tlh = tdb->db_phase[DP_DISCID];
	flh = fdb->db_phase[DP_DISCID];

	for(list_rewind(flh), list_forw(flh); !list_rewound(flh);
	    list_forw(flh)) {
		lp = list_cur(flh);

		if(list_find(tlh, lp->l_data) == 0) {
			if(list_add_cur(tlh, lp->l_data) == 0) {
				cddbd_log(LOG_ERR,
				    "Can't malloc linked list entry.");
				quit(QUIT_ERR);
			}

			merged++;
		}
	}

	for (i=0; i<2; i++) {
		tlh = tdb->db_phase[mergephase[i]];
		list_rewind(tlh);
		list_forw(tlh);
		tlp = list_cur(tlh);
		flh = fdb->db_phase[mergephase[i]];
		list_rewind(flh);
		list_forw(flh);
		flp = list_cur(flh);

		if (((char *)tlp->l_data)[0]=='\n') {
			free(tlp->l_data);
			tlp->l_data = strdup(flp->l_data);
			if(tlp->l_data == 0) {
				cddbd_log(LOG_ERR,
				    "Can't malloc string list entry.");
				quit(QUIT_ERR);
			}
		}
		
		/* zeke - unset the ASCII bit if non ASCII detected... */
		if ((tdb->db_flags & DB_ENC_ASCII) && !charset_is_valid_ascii(tlp->l_data)) {
			tdb->db_flags &= ~DB_ENC_ASCII;
		}
	}

	return merged;
}


int
db_strip(db_t *db)
{
	int i;
	int x;
	lhead_t *lh;
	db_parse_t *dp;

	x = 1;

	for(i = 0; i < DP_NPHASE; i++) {
		dp = &parstab[i];

		if(!(dp->dp_flags & PF_OSTRIP))
			continue;

		lh = db->db_phase[i];

		if(dp->dp_flags & PF_MULTI)
			x = db_strip_multi(lh);
		else
			x = db_strip_list(lh);

	}

	return x;
}


int
db_strip_multi(lhead_t *lh)
{
	link_t *lp;

	for(list_rewind(lh), list_forw(lh); !list_rewound(lh); list_forw(lh)) {
		lp = list_cur(lh);
		if(!db_strip_list((lhead_t *)lp->l_data))
			return 0;
	}

	return 1;
}


int
db_strip_list(lhead_t *lh)
{
	char *p;

	if(list_count(lh) <= 0)
		return 1;

	/* Empty out the list. */
	list_rewind(lh);
	list_forw(lh);

	while(!list_empty(lh))
		list_delete(lh, list_cur(lh));

	p = (char *)strdup("\n");
	if(p == 0)
		return 0;

	/* Put string in the list. */
	if(list_add_back(lh, (void *)p) == 0) {
		return 0;
	}

	return 1;
}


db_content_t *
db_content_check(db_t *db, int phase, int subphase)
{
	int n;
	arg_t args;
	db_parse_t *dp;
	static db_content_t dc;

	dp = &parstab[phase];
	cddbd_bzero((char *)&dc, sizeof(dc));

	if(!(dp->dp_flags & PF_NUMERIC)) {
		db_strcpy(db, phase, subphase, args.buf, sizeof(args.buf));
		cddbd_parse_args(&args, 0);

		if(is_blank(args.buf, 1))
			dc.dc_flags |= CF_BLANK;
	}

	/* check TTITLE track title field */
	if(dp->dp_flags & PF_CKTRK) {
		if( args.nargs >= 1 && (!cddbd_strcasecmp(args.arg[0], "track") || 
							   !cddbd_strcasecmp(args.arg[0], "audiotrack"))) {
			if(args.nargs == 1) {
				dc.dc_flags |= CF_INVALID;
			}
			else if(args.nargs == 2 && is_numeric(args.arg[1])) {
				n = atoi(args.arg[1]);
				if(n == subphase || n == (subphase + 1))
					dc.dc_flags |= CF_INVALID;
			}
		}
		else if(args.nargs >= 1 && (!strcmp(args.arg[0], "MSG:")))
					dc.dc_flags |= CF_INVALID;
		else if(args.nargs == 1 &&
		    !cddbd_strcasecmp(args.arg[0], "-")) {
			dc.dc_flags |= CF_INVALID;
		}
		else if(args.nargs == 2 &&
		    !cddbd_strcasecmp(args.arg[0], "empty") &&
		    !cddbd_strcasecmp(args.arg[1], "track")) {
			dc.dc_flags |= CF_INVALID;
		}
		else if(args.nargs == 2 &&
		    !cddbd_strcasecmp(args.arg[0], "new") &&
		    !cddbd_strcasecmp(args.arg[2], "track"))
			dc.dc_flags |= CF_INVALID;
	}

	/* check DTITLE disc title */
	if(dp->dp_flags & PF_CKTIT) {
		if(args.nargs == 0)
			dc.dc_flags |= CF_INVALID;
		else if(args.nargs == 1 &&
		    !cddbd_strcasecmp(args.arg[0], "empty"))
			dc.dc_flags |= CF_INVALID;
		else if(args.nargs >= 1 &&
			!strcmp(args.arg[0], "MSG:"))
			dc.dc_flags |= CF_INVALID;
		else if(args.nargs >= 2 &&
		    (!cddbd_strcasecmp(args.arg[0], "new") ||
		     !cddbd_strcasecmp(args.arg[0], "nieuwe") ||
			 !cddbd_strcasecmp(args.arg[0], "no")) &&
		    (!cddbd_strcasecmp(args.arg[1], "artist") ||
		    !cddbd_strcasecmp(args.arg[1], "titel")))
			dc.dc_flags |= CF_INVALID;
		else if(args.nargs == 3 &&
			(!cddbd_strcasecmp(args.arg[0], "unknown") ||
			 !cddbd_strcasecmp(args.arg[0], "unbekannt")) &&
			(!cddbd_strcasecmp(args.arg[1], "/")) &&
			(!cddbd_strcasecmp(args.arg[2], "unknown") ||
			 !cddbd_strcasecmp(args.arg[2], "unbekannt")))
			dc.dc_flags |= CF_INVALID;
	}

	return &dc;
}

/* ARGSUSED */
char *
db_field(db_t *db, int phase, int subphase)
{
	db_parse_t *dp;
	static char name[CDDBPHASESZ];

	dp = &parstab[phase];

	if(dp->dp_flags & PF_MULTI)
		cddbd_snprintf(name, sizeof(name), "%s %d", dp->dp_name,
		    subphase);
	else
		cddbd_snprintf(name, sizeof(name), "%s", dp->dp_name);

	return name;
}


/* Check for a blank line. */
int
is_blank(char *buf, int slash)
{
	while(*buf != '\0' && (isspace(*buf) || (slash && (*buf == '/')))) {
		/* Only allow one of these. */
		if(*buf == '/')
			slash = 0;
		buf++;
	}

	if(*buf == '\n')
		buf++;

	if(*buf == '\0')
		return 1;

	return 0;
}


/* Check for numeric input. */
int
is_numeric(char *buf)
{
	while(*buf != '\0') {
		if(!isdigit(*buf))
			return 0;
		buf++;
	}

	return 1;
}


/* Check for hex input. */
int
is_xnumeric(char *buf)
{
	while(*buf != '\0') {
		if(!isxdigit(*buf))
			return 0;
		buf++;
	}

	return 1;
}


/* Check for a terminating period. */
int
is_dot(char *buf)
{
	return(!strcmp(buf, ".\n") || !strcmp(buf, ".\r\n"));
}


/* Check for a terminating period or two. */
int
is_dbl_dot(char *buf)
{
	return(is_dot(buf) || !strncmp(buf, "..", 2));
}


int
is_crlf(int c)
{
	return(c == '\r' || c == '\n');
}


int
is_wspace(int c)
{
	return(c == ' ' || c == '\t');
}


int
categ_index(char *categ)
{
	int i;

	for(i = 0; categlist[i] != 0; i++)
		if(!strcmp(categ, categlist[i]))
			return i;

	return -1;
}


int
is_parent_dir(char *name)
{
	return(!strcmp(name, ".") || !strcmp(name, ".."));
}


void
asy_decode(char *p)
{
	int c;
	char *p2;
	char *p3;

	while(*p != '\0') {
		/* If we have a mapping, unmap it and compress. */
		c = octet_to_char((unsigned char *)p, '%');

		if(c >= 0) {
			p3 = p;

			*p = (char)c;
			p++;
			p2 = p + 2;

			while(*p2 != '\0') {
				*p = *p2;
				p++;
				p2++;
			}

			*p = '\0';
			p = p3;
		}

		p++;
	}
}


int
asy_encode(char *buf, char **out)
{
	int len;
	char *p;
	char *o;
	static char *obuf;
	static int size = 0;

	len = (strlen(buf) * 3) + 1;

	if(len > size) {
		if(size != 0)
			free(obuf);

		obuf = (char *)malloc(len);

		if(obuf == NULL) {
			size = 0;
			return 0;
		}
	}

	for(p = obuf; *buf != '\0'; buf++) {
		if(asy_mappable((unsigned char)*buf)) {
			o = char_to_octet((unsigned char)*buf, '%');
			len = strlen(o);
			strncpy(p, o, len);
			p += len;
		}
		else {
			*p = *buf;
			p++;
		}
	}

	*p = '\0';
	*out = obuf;

	return 1;
}


int
asy_mappable(unsigned char c)
{
	return(c == '&' || c == '+' || c == '?' || c == '%' ||
	    !isprint((int)c));
}


/*
 * Apply the func to each string in db,
 * and stop if the func returns non-zero.
 */

int
db_strings_multi(lhead_t *lh, db_parse_t *dp,
		 int (*func)(char **p, void *x), void *arg);
int
db_strings_str(lhead_t *lh, db_parse_t *dp, int trk,
	       int (*func)(char **p, void *x), void *arg);

int
db_strings(db_t *db, int (*func)(char **p, void *x), void *arg) {
	int i;
	int x;
	lhead_t *lh;
	db_parse_t *dp;

	for (i = 0; i < DP_NPHASE; i++) {
		dp = &parstab[i];
		lh = db->db_phase[i];

		if (dp->dp_flags & PF_MULTI)
			x = db_strings_multi(lh, dp, func, arg);
		else if (dp->dp_flags & PF_NUMERIC)
			x = 0;
		else
			x = db_strings_str(lh, dp, 0, func, arg);

		if (x)
			return x;
	}

	return 0;
}

int
db_strings_multi(lhead_t *lh, db_parse_t *dp,
		 int (*func)(char **p, void *x), void *arg) {
	int i;
	int x;
	link_t *lp;

	for (i = 0, list_rewind(lh), list_forw(lh); !list_rewound(lh);
	     i++, list_forw(lh)) {
		
		lp = list_cur(lh);

		x = db_strings_str((lhead_t *)lp->l_data, dp, i, func, arg);
		if (x)
			return x;
	}

	return 0;
}

int
db_strings_str(lhead_t *lh, db_parse_t *dp, int trk,
	       int (*func)(char **p, void *x), void *arg) {
	link_t *lp;
	int x;

	for (list_rewind(lh), list_forw(lh); !list_rewound(lh);
		 list_forw(lh)) {

		lp = list_cur(lh);

		x = (*func)((char **)&lp->l_data, arg);
		if (x)
			return x;
	}

	return 0;
}

int
db_latin1_utf8_aux(char **p, void *unused)
{
	char *s;

	charset_latin1_utf8(*p, &s);
	free(*p);
	*p = s;
	return 0;
}

/* Convert from ISO-8859-1 to UTF-8. */
void
db_latin1_utf8(db_t *db)
{
	if (!(db->db_flags & DB_ENC_LATIN1)) {
		cddbd_log(LOG_ERR, "Internal error in db_latin1_utf8.");
		return;
	}
	db_strings(db, &db_latin1_utf8_aux, 0);
	
	db->db_flags &= ~(DB_ENC_UTF8 | DB_ENC_LATIN1);
	db->db_flags |= DB_ENC_UTF8;
}

int
db_utf8_latin1_aux(char **p, void *_inexact)
{
	int *inexact = (int *)_inexact;
	char *s;
	int r;

	r = charset_utf8_latin1(*p, &s);
	if (r == -1)
		return 1;
	if (r)
		*inexact = 1;
	
	free(*p);
	*p = s;
	return 0;
}

/* Convert from UTF-8 to ISO-8859-1.
   Return 0 if exact, 1 if inexact, -1 if error. */
int
db_utf8_latin1(db_t *db)
{
	int inexact = 0;

	if (!(db->db_flags & DB_ENC_UTF8)) {
		cddbd_log(LOG_ERR, "Internal error in db_utf8_latin1.");
		return -1;
	}
	
	if (db_strings(db, db_utf8_latin1_aux, &inexact))
		return -1;
	if (inexact)
		; /* XXX add warning? */
	
	db->db_flags &= ~(DB_ENC_UTF8 | DB_ENC_LATIN1);
	db->db_flags |= DB_ENC_LATIN1;
	return inexact;
}

int
db_utf8_latin1_exact_aux(char **p, void *unused)
{
	char *s;
	int r;

	r = charset_utf8_latin1(*p, &s);
	
	free(s);
	return r;
}
	
/* Convert from UTF-8 to ISO-8859-1 if exact.
   Return 0 if exact, 1 if inexact (not converted), -1 if error. */
int
db_utf8_latin1_exact(db_t *db)
{
	int unused;
	int r;

	if (!(db->db_flags & DB_ENC_UTF8))
		cddbd_log(LOG_ERR, "Internal error in db_utf8_latin1_exact.");
	
	r = db_strings(db, db_utf8_latin1_exact_aux, 0);
	if (r)
		return r;
	
	db_strings(db, db_utf8_latin1_aux, &unused);
	
	db->db_flags &= ~(DB_ENC_UTF8 | DB_ENC_LATIN1);
	db->db_flags |= DB_ENC_LATIN1;
	return 0;
}

int
db_looks_like_utf8_aux(char **p, void *unused)
{
	return !charset_is_utf8(*p);
}

/* Return non-zero if well-formed UTF-8. */
int
db_looks_like_utf8(db_t *db)
{
	/* zeke - always returns "1" if non-illegal chars found  */
	/* zeke - init parse flags, set in scans in "parse_utf8" */

	if (db_strings(db, db_looks_like_utf8_aux, 0))
		return 0;
	return 1;
}

/* Check and disambiguate the charset.
   This is called after reading input from a client. */
int
db_disam_charset(db_t *db)
{
	if(!(db->db_flags & DB_ENC_ASCII)) {
		if(db->db_flags & DB_ENC_UTF8)
			db->db_flags &= ~DB_ENC_LATIN1;
		
		if((db->db_flags & DB_ENC_LATIN1) && utf_as_iso != UAI_ACCEPT)
			if (db_looks_like_utf8(db))
				return 1;
	}
	return 0;
}
