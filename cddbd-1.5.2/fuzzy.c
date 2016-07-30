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
static char *const _fuzzy_c_ident_ = "@(#)$Id: fuzzy.c,v 1.21.2.4 2006/04/19 16:49:05 joerg78 Exp $";
#endif

#include <stdlib.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include "access.h"
#include "cddbd.h"
#include "bit_array.h"

/* Flags for read_db_fuzzy. */
#define FH_OFFSET	0x00000001

/* Variable declarations. */

fhdr_t fhdr;
flist_t *flist;

extern unsigned int db_line_counter;

/* Prototypes. */

int comp_fhash(const void *, const void *);

void fmatch_free(void *);

#ifdef DB_WINDOWS_FORMAT
db_errno_t read_db_fuzzy(char *, fhash_t *, char *, int, char *, int *);
#else
db_errno_t read_db_fuzzy(char *, fhash_t *, char *, int, char *);
#endif


/* ARGSUSED */
void
do_cddb_query_fuzzy(arg_t *args)
{
	int i;
	int ntrks;
	int nsecs;
	int offtab[CDDBMAXTRK];
	unsigned int discid;
	char buf[CDDBBUFSIZ];
	link_t *lp;
	lhead_t *lh;
	fmatch_t *fm;

	/* Shouldn't be syntax errors here, but check anyway. */
	if(sscanf(args->arg[args->nextarg + 1], "%x", &discid) != 1 ||
	    sscanf(args->arg[args->nextarg + 2], "%d", &ntrks) != 1 ||
	    sscanf(args->arg[args->nextarg + ntrks + 3], "%d", &nsecs)
	    != 1) {
		printf("500 Command syntax error.\r\n");
		return;
	}

	for(i = 0; i < ntrks; i++) {
		if(sscanf(args->arg[args->nextarg + i + 3], "%d",
		    &offtab[i]) != 1 || offtab[i] < 0) {
			printf("500 Command syntax error.\r\n");
			return;
		}
	}

	if((lh = fuzzy_search(ntrks, offtab, nsecs)) == 0) {
		printf("202 No match for disc ID %08x.\r\n", discid);
		cddbd_log(LOG_ACCESS | LOG_UQUERY,
		    "Query: unsuccessful %08x", discid);
	}
	else {
		/* Found matches. Now print them. */
		printf("211 Found inexact matches, list ");
		printf("follows (until terminating `.')\r\n");

		for(list_rewind(lh), list_forw(lh); !list_rewound(lh);
		    list_forw(lh)) {
			lp = list_cur(lh);
			fm = (fmatch_t *)lp->l_data;

			printf("%s %08x %s\r\n", categlist[fm->fm_catind],
			    fm->fm_discid, fm->fm_dtitle);
		}

		printf(".\r\n");

		if(lh->lh_count == 1)
			strcpy(buf, "match");
		else
			strcpy(buf, "matches");

		cddbd_log(LOG_ACCESS | LOG_FUZZY,
		    "Query: %08x found %d fuzzy %s",
		    discid, lh->lh_count, buf);

		list_free(lh);
	}

	return;
}


lhead_t *
fuzzy_search(int ntrks, int *offtab, int nsecs)
{
	int i;
	int j;
	int x;
	FILE *fp;
	int found;
	lhead_t *lh;
	fhash_t *fh;
	fmatch_t *fm;
	char tit[CDDBBUFSIZ];
	char file[CDDBBUFSIZ];
	char errstr[CDDBBUFSIZ];
	unsigned char  chartmp[bit_size(CDDBMAXTRK,OFFSET_BIT_WIDTH)]; /* tmp array */

	(void)cddbd_lock(locks[LK_FUZZY], 1);

	/* Open the hash file. */
	if((fp = fopen(servfile[SF_FUZZY], "r")) == NULL) {
		cddbd_log(LOG_ERR | LOG_HASH,
		    "Can't open hash file %s (%d).", servfile[SF_FUZZY], errno);
		return 0;
	}

	/* Free the lock, now that we have a handle on the file. */
	cddbd_unlock(locks[LK_FUZZY]);

	/* Read the hash file header. */
	if(fread(&fhdr, sizeof(fhdr), 1, fp) != 1) {
		cddbd_log(LOG_ERR | LOG_HASH,
		    "Can't read hash file header (%d).", errno);
		fclose(fp);
		return 0;
	}

	/* Validate the hash file. */
	if(fhdr.hd_magic != FUZZY_MAGIC || fhdr.hd_version != FUZZY_VERSION) {
		cddbd_log(LOG_ERR | LOG_HASH,
		    "Bad fuzzy matching hash file: %s.", servfile[SF_FUZZY]);
		fclose(fp);
		return 0;
	}

	if(fseek(fp, fhdr.hd_addr[ntrks - 1], SEEK_CUR) != 0) {
		cddbd_log(LOG_ERR | LOG_HASH,
		    "Corrupt hash file: can't seek past header: %s",
		    servfile[SF_FUZZY]);
		fclose(fp);
		return 0;
	}

	if((fh = (fhash_t *)malloc(sizeof(fhash_t) + ((CDDBMAXTRK - 1) *
	    sizeof(int)))) == 0) {
		cddbd_log(LOG_ERR | LOG_HASH, "Can't malloc hash element.");
		quit(QUIT_ERR);
	}

	if((lh = list_init(0, 0, fmatch_free, 0)) == 0) {
		cddbd_log(LOG_ERR | LOG_HASH, "Can't malloc hash list.");
		quit(QUIT_ERR);
	}

	/* Check entries. */
	found = 0;

	/* Compute the max tolerance for the CD length. */
	x = fuzzy_factor * (ntrks + 1) / fuzzy_div / CDDBFRAMEPERSEC;

	if(x < 1)
		x = 1;

	for(i = 0; i < fhdr.hd_tcount[ntrks - 1]; i++) {
		if(fread(&fh->fh_s, sizeof(fh->fh_s), 1, fp) != 1) {
			cddbd_log(LOG_ERR | LOG_HASH,
			    "Truncated fuzzy matching hash file: %s.",
			    servfile[SF_FUZZY]);
			break;
		}

		/* Sanity check. */
		if(fh->fh_trks != ntrks) {
			cddbd_log(LOG_ERR | LOG_HASH,
			    "Corrupt hash file: failed sanity check: %s",
			    servfile[SF_FUZZY]);
			break;
		}

		/* Check to see if length is within tolerance. */
		j = fh->fh_disclen - nsecs;

		/* All entries beyond this point are not matches. */
		if(j > x)
			break;

		/* Find abs. */
		if(j < 0)
			j *= -1;

		/* Not a match if the allowable diff is < than the actual. */
		if(j > x) {
			/* Skip to the next record. */
			if(fseek(fp, bit_size(ntrks, OFFSET_BIT_WIDTH), SEEK_CUR) != 0) {
				cddbd_log(LOG_ERR | LOG_HASH,
				    "Corrupt hash file: failed to skip to next "
				    "record: %s", servfile[SF_FUZZY]);
				break;
			}

			continue;
		}

		if(fread(chartmp, bit_size(ntrks,OFFSET_BIT_WIDTH), 
		    1, fp) != 1) {
			cddbd_log(LOG_ERR | LOG_HASH,
			    "Truncated fuzzy matching hash file: %s.",
			    servfile[SF_FUZZY]);
			break;
		}
		bit_char2uint(ntrks,OFFSET_BIT_WIDTH,(unsigned int *) fh->fh_offset,chartmp);
		if(!is_fuzzy_match(offtab, fh->fh_offset, nsecs, fh->fh_disclen,
		    ntrks))
			continue;

		cddbd_snprintf(file, sizeof(file), "%s/%s/%08x",
		    cddbdir, fhdr.hd_categ[fh->fh_catind], fh->fh_discid);

		/* Get the title from the database file. */
#ifdef DB_WINDOWS_FORMAT
		if(read_db_fuzzy(file, fh, tit, 0, errstr, NULL) != DE_NO_ERROR)
			/* use NULL as last parameter as we do not want the whole file... */
			continue;
#else
		if(read_db_fuzzy(file, fh, tit, 0, errstr) != DE_NO_ERROR)
			continue;
#endif

		found++;

		if((fm = (fmatch_t *)malloc(sizeof(fmatch_t))) == 0) {
			cddbd_log(LOG_ERR | LOG_HASH,
			    "Can't allocate memory for match list.");
			quit(QUIT_ERR);
		}

		/* Copy the data over. */
		fm->fm_s = fh->fh_s;

		if((fm->fm_catind =
		    (short)categ_index(fhdr.hd_categ[fh->fh_catind])) < 0) {
			cddbd_log(LOG_ERR | LOG_HASH,
			    "Corrupt hash file: illegal category %d: %s.",
			    fh->fh_catind, file);
			quit(QUIT_ERR);
		}

		if((fm->fm_dtitle = strdup(tit)) == 0) {
			cddbd_log(LOG_ERR | LOG_HASH,
			    "Can't allocate memory for match dtitle.");
			quit(QUIT_ERR);
		}

		if(list_add_cur(lh, fm) == 0) {
			cddbd_log(LOG_ERR | LOG_HASH,
			    "Can't add to match list.");
			quit(QUIT_ERR);
		}
	}

	if(!found) {
		list_free(lh);
		lh = 0;
	}

	free(fh);
	fclose(fp);

	return lh;
}


void
fmatch_free(void *p)
{
	fmatch_t *fm;

	fm = (fmatch_t *)p;
	free(fm->fm_dtitle);
	free(fm);
}


void
cddbd_build_fuzzy(void)
{
	int i;
	int j;
	int sz;
	int bad;
	int links;
	int noread;
	int nohash;
	int entries;
	FILE *fp;
#ifdef DB_WINDOWS_FORMAT
	int last;
#endif
	DIR *dirp;
	struct dirent *dp;
	flist_t *fl;
	flist_t **ftab;
	fhash_t *fh;
	fhash_t *fhash;
	lhead_t *lh;
	struct stat sbuf;
	char *tfuzz;
	char file[CDDBBUFSIZ];
	char file2[CDDBBUFSIZ];
	char errstr[CDDBBUFSIZ];
	unsigned char chartmp[bit_size(CDDBMAXTRK,OFFSET_BIT_WIDTH)];

	if(!cddbd_lock(locks[LK_MKFUZZ], 0)) {
		cddbd_log(LOG_ERR | LOG_HASH,
		    "Build of fuzzy matching hash file already in progress.");
		quit(QUIT_RETRY);
	}

	/* Open a temporary hash file. */
	tfuzz = cddbd_mktemp();

	if((fp = fopen(tfuzz, "w")) == NULL) {
		cddbd_log(LOG_ERR | LOG_HASH,
		    "Can't open %s for writing (%d).", tfuzz, errno);
		quit(QUIT_ERR);
	}

	(void)cddbd_fix_file(tfuzz, dir_mode, uid, gid);

	cddbd_log(LOG_INFO, "Generating the fuzzy matching hash file.");

	bad = 0;
	links = 0;
	entries = 0;
	nohash = 0;
	noread = 0;
	flist = 0;

	if((fhash = (fhash_t *)malloc(sizeof(fhash_t) + ((CDDBMAXTRK - 1) *
	    sizeof(int)))) == 0) {
		cddbd_log(LOG_ERR | LOG_HASH, "Can't malloc hash element.");
		quit(QUIT_ERR);
	}

	/* Reset the elapsed time counter. */
	(void)cddbd_elapse(0);

	for(i = 0; categlist[i] != 0; i++) {
		/* Put the category name in the header. */
		strcpy(fhdr.hd_categ[i], categlist[i]);

		cddbd_snprintf(file2, sizeof(file2), "%s/%s", cddbdir,
		    categlist[i]);

		cddbd_log(LOG_INFO, "Scanning %s.", file2);

		if((dirp = opendir(file2)) == NULL) {
			cddbd_log(LOG_ERR | LOG_HASH,
			    "Can't open %s for reading.", file2);
			quit(QUIT_ERR);
		}

		lh = list_init(0, 0, 0, 0);
		if(lh == 0) {
			cddbd_log(LOG_ERR | LOG_HASH,
			    "Can't malloc linked list.");
			quit(QUIT_ERR);
		}

		while((dp = readdir(dirp)) != NULL) {
#ifdef DB_WINDOWS_FORMAT
			/* the progress bar makes sense for win format only */
			if(debug) {
				fputc('.', stdout);
				fflush(stdout);
			}
#endif

			/* Delay to slow things down, if need be. */
			if(cddbd_elapse(elapse_time))
				cddbd_delay(delay_time);

			/* Make sure this is a database file. */
#ifdef DB_WINDOWS_FORMAT
			if(strlen(dp->d_name) != WINFILEIDLEN)
				continue;
#else
			if(strlen(dp->d_name) != CDDBDISCIDLEN)
				continue;

			entries++;
#endif

			/* for DB_WINDOWS_FORMAT will do e.g. file = "blues/01to23" 
			   otherwise something like file = "blues/12345678" */
			cddbd_snprintf(file, sizeof(file), "%s/%s", file2,
			    dp->d_name);

			if(stat(file, &sbuf)) {
				cddbd_log(LOG_ERR, 
				    "Warning: can't stat CDDB file: %s", file);
				continue;
			}

#ifdef DB_WINDOWS_FORMAT
			if(sbuf.st_size == 0) { /* skip empty files */
				cddbd_log(LOG_INFO, 
				          "Info: Will skip zero size file: %s", 
				          file);
				continue;
			}

			do { /* cycle through the multirecord file */
				entries++;
#endif
			if(sbuf.st_nlink > 1 && list_find(lh,
			    ino_t_as_ptr (sbuf.st_ino)) != 0) {
				links++;
				continue;
			}

			/* Parse the database file. */
#ifdef DB_WINDOWS_FORMAT
			switch(read_db_fuzzy(file, fhash, 0, FH_OFFSET, errstr, &last))
#else
			switch(read_db_fuzzy(file, fhash, 0, FH_OFFSET, errstr))
#endif
			{
			case DE_NO_ERROR:
				fl = (flist_t *)malloc(sizeof(flist_t) +
				    ((fhash->fh_trks - 1) *
				    sizeof(fhash->fh_offset[0])));

				if(fl == 0) {
					cddbd_log(LOG_ERR | LOG_HASH,
					    "Can't malloc hash list entry "
					    "(%d).", errno);

					quit(QUIT_ERR);
				}

				/* Copy the temp hash struct to the new one. */
				fh = &fl->fl_fhash;
				fh->fh_s = fhash->fh_s;

				for(j = 0; j < fhash->fh_trks; j++)
					fh->fh_offset[j] = fhash->fh_offset[j];

				/* Note the category. */
				fh->fh_catind = (short)i;

				/* Count the database entry. */
				fhdr.hd_count++;
				fhdr.hd_tcount[fh->fh_trks - 1]++;

#ifdef DB_WINDOWS_FORMAT
				fh->fh_discid = db_curr_discid;
#else
				sscanf(dp->d_name, "%08x", &fh->fh_discid);
#endif

				fl->fl_next = flist;
				flist = fl;

				break;

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

			if(sbuf.st_nlink > 1 &&
			    list_add_cur(lh, ino_t_as_ptr (sbuf.st_ino)) == 0) {
				cddbd_log(LOG_ERR | LOG_HASH,
				    "Can't malloc linked list entry.");
				quit(QUIT_ERR);
			}
#ifdef DB_WINDOWS_FORMAT
			} while(!last); /* while it is not the last one in the file...
			                   - end of cycle through the multirecord file */
#endif
		}

		list_free(lh);
		closedir(dirp);

#ifdef DB_WINDOWS_FORMAT
		if(debug) /* finish the progress bar */
			fputc('\n', stdout);
#endif
	}

	free(fhash);

	if(fhdr.hd_count == 0) {
		cddbd_log(LOG_ERR | LOG_HASH, "No valid database entries.");
		quit(QUIT_ERR);
	}

	ftab = (flist_t **)malloc(sizeof(flist_t *) * fhdr.hd_count);
	if(ftab == NULL) {
		cddbd_log(LOG_ERR | LOG_HASH,
		    "Can't malloc hash table (%d).", errno);
		quit(QUIT_ERR);
	}

	for(i = 0, fl = flist; fl != 0; i++, fl = fl->fl_next)
		ftab[i] = fl;

	/* Sort the entries. */
	qsort(ftab, fhdr.hd_count, sizeof(flist_t *), comp_fhash);

	/* Write the header out. */
	if(fwrite(&fhdr, sizeof(fhdr_t), 1, fp) != 1) {
		cddbd_log(LOG_ERR | LOG_HASH,
		    "Can't write hash table header (%d).", errno);
		quit(QUIT_ERR);
	}

	fh = &ftab[0]->fl_fhash;

	/* Tally the sizes. */
	for(i = 0; i < (CDDBMAXTRK - 1); i++) {
		sz = fhdr.hd_tcount[i] * (sizeof(fh->fh_s) +
		    bit_size(i + 1, OFFSET_BIT_WIDTH ));

		for(j = (i + 1); j < CDDBMAXTRK; j++)
			fhdr.hd_addr[j] += sz;
	}

	/* Write the records out. */
	for(i = 0; i < fhdr.hd_count; i++) {
		fh = &ftab[i]->fl_fhash;
		bit_uint2char(fh->fh_trks,OFFSET_BIT_WIDTH,(unsigned int *) fh->fh_offset,chartmp);
		if(fwrite(&(fh->fh_s), sizeof(fh->fh_s), 1, fp) != 1 ||
		    fwrite(chartmp, bit_size(fh->fh_trks,OFFSET_BIT_WIDTH)
		    , 1, fp) != 1) {
			cddbd_log(LOG_ERR | LOG_HASH,
			    "Can't write hash table entry (%d).", errno);
			quit(QUIT_ERR);
		}

		free(ftab[i]);
	}

	free(ftab);

	/* Write the header out again with the magic number. */
	rewind(fp);
	fhdr.hd_magic = FUZZY_MAGIC;
	fhdr.hd_version = FUZZY_VERSION;

	if(fwrite(&fhdr, sizeof(fhdr_t), 1, fp) != 1) {
		cddbd_log(LOG_ERR | LOG_HASH,
		    "Can't write hash table header (%d).", errno);
		quit(QUIT_ERR);
	}

	fclose(fp);

	(void)cddbd_lock(locks[LK_FUZZY], 1);

	if(unlink(servfile[SF_FUZZY]) != 0 && errno != ENOENT) {
		cddbd_log(LOG_ERR | LOG_HASH,
		    "Can't unlink %s (%d).", servfile[SF_FUZZY], errno);
		quit(QUIT_ERR);
	}

	if(cddbd_link(tfuzz, servfile[SF_FUZZY]) != 0) {
		cddbd_log(LOG_ERR | LOG_HASH,
		    "Can't link %s to %s (%d).", tfuzz, servfile[SF_FUZZY],
		    errno);
		quit(QUIT_ERR);
	}

	cddbd_freetemp(tfuzz);

	cddbd_unlock(locks[LK_MKFUZZ]);
	cddbd_unlock(locks[LK_FUZZY]);

	cddbd_log(LOG_INFO, "Hashed %d database entries out of %d.",
	    fhdr.hd_count, entries);
	cddbd_log(LOG_INFO,
	    "Ignored %d files: %d invalid, %d unhashable, %d unreadable, %d links.",
	    (entries - fhdr.hd_count), bad, nohash, noread, links);
	cddbd_log(LOG_INFO, "Done creating hash file.");
}


int
comp_fhash(const void *c1, const void *c2)
{
	fhash_t *h1;
	fhash_t *h2;

	h1 = &((*(flist_t **)c1)->fl_fhash);
	h2 = &((*(flist_t **)c2)->fl_fhash);

	if(h1->fh_trks != h2->fh_trks)
		return (h1->fh_trks - h2->fh_trks);

	if(h1->fh_disclen != h2->fh_disclen)
		return (h1->fh_disclen - h2->fh_disclen);

	return 0;
}


#ifdef DB_WINDOWS_FORMAT
/** 
 * Reads given file and fills the provided hash record.
 *
 * Referenced by:
 *  - cddbd_build_fuzzy()
 *      sequentially reads all the records using readdir (i.e. the format
 *      of #discid is like blues/01to23. In this case we store and reuse the
 *      opened file from previous call in a static var.
 *
 *  - fuzzy_search()
 *      reads single specific record (i.e. the format of #discid is like
 *      blues/12345678)
 * 
 * @param discid provided disc ID (actually a file path)
 * @param fh     hash record (already allocated)
 * @param dtitle disc title (out)
 * @param flags  flags
 * @param err    respective error message
 * @param last   (in) when not NULL - sequential, out status of last record
 * 
 * @return error status
 */
db_errno_t read_db_fuzzy(char *discid, 
                         fhash_t *fh, 
                         char *dtitle, 
                         int flags, 
                         char *err,
                         int *last)
{
	int i;
	static FILE *fp = NULL; /* need to keep it as there are more records */
	db_t *db;

	if(last == NULL) { /* single record */
		/* get the id, open the file, skip to the record */
		if(fp != NULL) {
			fclose(fp);
			fp = NULL;
		}
		if((fp = db_prepare_unix_read(discid)) == NULL) {
			cddbd_snprintf(err, CDDBBUFSIZ, "can't open %s for reading",
			               discid);
			return DE_FILE;
		}
	}
	else { /* sequential */
		if(fp == NULL) { /* the first one */
			db_line_counter = 0;

			if((fp = fopen(discid, "r")) == NULL) {
				cddbd_snprintf(err, CDDBBUFSIZ, "can't open %s for reading",
				               discid);
				return DE_FILE;
			}
 
			if(db_skip(fp, NULL)) {
				cddbd_snprintf(err, CDDBBUFSIZ, "can't skip record separator");
				fclose(fp);
				fp = NULL;
				return DE_FILE;
			}
		}
	}

	db = db_read(fp, err, file_df_flags | DF_WIN);

	/* single record or end of file reached */
	if(last==NULL) {
		fclose(fp);
		fp = NULL;
	} 
	else if(feof(fp) || ferror(fp)) {
		fclose(fp);
		fp = NULL;
		*last = 1;
	} else {
		*last = 0;
	}

	if(db==0) {
		return db_errno;
	}

	if(flags & FH_OFFSET) {
		fh->fh_disclen = db->db_disclen;
		fh->fh_trks = db->db_trks;

		for(i = 0; i < db->db_trks; i++)
			fh->fh_offset[i] = db->db_offset[i];
	}
	
	/* Convert charset. */
	convert_db_charset(db);
	
	if(dtitle != 0)
		db_strcpy(db, DP_DTITLE, 0, dtitle, CDDBBUFSIZ);

	db_free(db);
	return DE_NO_ERROR;
}
#else
db_errno_t
read_db_fuzzy(char *discid, fhash_t *fh, char *dtitle, int flags, char *err)
{
	int i;
	FILE *fp;
	db_t *db;

	if((fp = fopen(discid, "r")) == NULL) {
		cddbd_snprintf(err, CDDBBUFSIZ, "can't open %s for reading",
		    discid);

		return DE_FILE;
	}

	db = db_read(fp, err, file_df_flags);
	fclose(fp);

	if(db == 0)
		return db_errno;

	if(flags & FH_OFFSET) {
		fh->fh_disclen = db->db_disclen;
		fh->fh_trks = db->db_trks;

		for(i = 0; i < db->db_trks; i++)
			fh->fh_offset[i] = db->db_offset[i];
	}
	
	/* Convert charset. */
	convert_db_charset(db);
	
	if(dtitle != 0)
		db_strcpy(db, DP_DTITLE, 0, dtitle, CDDBBUFSIZ);

	db_free(db);
	return DE_NO_ERROR;
}
#endif

int
is_fuzzy_match(int *offtab1, int *offtab2, int nsecs1, int nsecs2, int ntrks)
{
	int i;
	int x;
	int lo1;
	int lo2;
	int avg;
	
	/* Check the difference between track offsets. */
	for(i = 0, lo1 = 0, lo2 = 0, avg = 0; i < ntrks; i++) {
		lo1 = offtab1[i] - lo1;
		lo2 = offtab2[i] - lo2;

		x = lo1 - lo2;
		if(x < 0)
			x *= -1;
		avg += x;

		/* Track diff too great. */
		if(x > fuzzy_factor)
			return 0;

		lo1 = offtab1[i];
		lo2 = offtab2[i];
	}

	/* Compare disc length as if it were a track. */
	lo1 = (nsecs1 * CDDBFRAMEPERSEC) - lo1;
	lo2 = (nsecs2 * CDDBFRAMEPERSEC) - lo2;

	x = lo1 - lo2;
	if(x < 0)
		x *= -1;

	/* Track diff too great. */
	if(x > fuzzy_factor)
		return 0;

	avg += x;
	avg /= ntrks + 1;

	return((i == ntrks) && (avg <= (fuzzy_factor / fuzzy_div)));
}
