/*
 *   cddbd - CD Database Protocol Server
 *
 *   Copyright (C) 1996-1997  Steve Scherf (steve@moonsoft.com)
 *   Portions Copyright (C) 1999-2006  by various authors
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
static char *const _cddbd_c_ident_ = "@(#)$Id: cddbd.c,v 1.41.2.9 2006/04/18 22:10:25 joerg78 Exp $";
#endif

#include <stdlib.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/time.h>
#include "patchlevel.h"
#include "access.h"
#include "cddbd.h"

/**
 * \todo The windows version needs to be properly tested.
 */

/* Preprocessor definitions. */

/* Used for getting command-line args in main. */
#define NEXTARG(ag) \
	i++; \
	if(argc <= i) \
		usage(name); \
	(ag) = *(argv + i)

/* Definitions used by do_log(). */
#define LH_CLNT_CONN	0
#define LH_CLNT_USER	1
#define LH_IFACE_CONN	2
#define LH_READ		3
#define LH_NHEAD	4


/* Prototypes. */

void cddbd_access_err(int);
void cddbd_access_to(void);
void cddbd_block_sigs(void);
void cddbd_cddbp_cmd(void);
void cddbd_cgi_cmd(void);
void cddbd_check_nus(void);
void cddbd_cleantemp(void);
void cddbd_connect_to(void);
void cddbd_db_init(void);
void cddbd_init(void);
void cddbd_input_to(void);
void cddbd_lock_free_lk(void *);
void cddbd_log_prim(FILE *, int, char *, void *, void *, void *, void *,
    void *, void *);
void cddbd_mk_servfiles(void);
void cddbd_mk_workdirs(void);
void cddbd_read_access(char *, int);
void cddbd_strip(char *, char);
void cddbd_timer_init(ctimer_t *);
void cddbd_translate(char *, char, char);
void count_log(lhead_t *, char *, char *);
void do_cddb(arg_t *);
void do_cddb_hello(arg_t *);
void do_cddb_query(arg_t *);
void do_discid(arg_t *);
void do_cddb_lscat(arg_t *);
void do_cddb_read(arg_t *);
/* void do_cddb_srch(arg_t *); */
void do_cddb_unlink(arg_t *);
void do_cddb_write(arg_t *);
void do_get(arg_t *);
void do_help(arg_t *);
void do_log(arg_t *);
void do_motd(arg_t *);
void do_validate(arg_t *);
void do_proto(arg_t *);
void do_put(arg_t *);
void do_quit(arg_t *);
void do_sites(arg_t *);
void do_stat(arg_t *);
void do_update(arg_t *);
void do_ver(arg_t *);
void do_whom(arg_t *);
void free_cperm(void *);
void free_hperm(void *);
void free_log(void *);
void modify_perms(hperm_t *);
void reduce_rev(char *, char *, char *, int *);
void sighand(int);
void trunc_log(void);
void usage(char *);
void usage2(char *name, char *extra);
void usage_uid(void);
static void finish_header(char *charset);

int _do_cddb_hello(arg_t *);
int _do_validate(char *, uint32_t);
int cddbd_is_cgi(void);
int cddbd_locked(clck_t *);
int cddbd_nus(int);
int cddbd_parse_access(arg_t *, int);
int cddbd_parse_iface(char *, char *, int, char *);
int cddbd_secure(void);
int cddbd_strcasecmp(char *, char *);
int comp_log(void *, void *);
int comp_logdata(const void *, const void *);
int comp_rev(char *, char *);
int cvt_date(char *, char *);
int get_conv(char **, char *);
int has_parent_dir(char *);
int make_year(int);
int match_rev(char *, char *, int);

char *cddbd_safenv(char *);
char *cddbd_dirname(char *);

logdata_t **sort_log(lhead_t *);

extern void asy_decode(char *);

/* truly global variables. */

int verbose;				/* Verbose mode if true. */
clck_t *locks[LK_NUMLOCKS];
#ifdef DB_WINDOWS_FORMAT
clck_t ***categlocks;
#endif

/* global variables with module-wide scope.  These should be static! */
static int build_fuzzy;			/* Build the fuzzy match hash file.*/
int categcnt;				/* Category count. */
int interface;				/* Interface proto (cddbp/http/mail). */
static int check;				/* Check the database. */
static int db_access;				/* Database was accessed. */
int db_dir_mode;			/* DB directory creation mode. */
int db_file_mode;			/* DB file creation mode. */
int debug;				    /* Debug mode if true (global var). */
static int make_key;				/* If true, make a new key. */
static int make_pwd;				/* If true, make a new key. */
int delay_time;				/* How much time to delay. */
static int skip_log;				/* Don't log connection if true. */
int dir_mode;				/* Directory creation mode. */
int dup_ok;				/* Dup directory exists if true. */
int dup_policy;				/* How to treat duplicate entries. */
int elapse_time;			/* How much time to work. */
int email_time;				/* How long to delay between emails. */
int file_charset;			/* Charset used in files. */
int file_df_flags;			/* Flags for db_read from a file. */
int file_mode;				/* File creation mode. */
int fuzzy_div;				/* Fuzzy matching divisor. */
int fuzzy_factor;			/* Fuzzy matching factor. */
static int hang_time;				/* How long to hold clients. */
static int hello;				/* Handshake done if true. */
static int level;				/* Protocol level we're running at. */
static int lock_time;				/* Wait between lock retries. */
static int lock_wait;				/* Max # seconds to wait for a lock. */
static int logging;				/* Logging in progress. */
static int log_hiwat;				/* Log file high water mark. */
static int log_lowat;				/* Log file low water mark. */
static int match_entries;			/* Find entries that match. */
static int max_hangs;				/* Max # of concurrent hangs. */
int max_lines;				/* Max # of lines in a cddb entry. */
static int max_users;				/* Max # of simultaneous users. */
int max_xmits;				/* Max # of simultaneous transmits. */
static int mode;				/* Mode of operation. */
static int nus;				/* Number of current users. */
static int put_size;				/* Max input size on a file put. */
static int quiet;				/* Don't log messages, if true. */
static int secure;				/* If true, not running setuid. */
static int standalone;				/* Standalone mode, if true. */
int strip_ext;				/* Strip extended data , if true. */
static int test_mail;				/* Mail server test mode. */
static int tmpcount;				/* Number of temp files we've got. */
static int update;				/* Update database with new entries. */
int ck_berzerk;				/* Check for berzerk clients. */
int utf_as_iso;				/* See the documentation. */
int xmit_time;				/* Transmit timeout in seconds. */

static uint32_t cksum;				/* Saved username checksum. */

uid_t db_uid;				/* DB user-id. */
static uid_t euid;				/* Effective user-id. */
uid_t uid;				/* Owner user-id. */
static uid_t ruid;				/* Real user-id. */

gid_t db_gid;				/* DB group-id. */
static gid_t egid;				/* Effective group-id. */
gid_t gid;				/* Owner group-id. */
static gid_t rgid;				/* Real group-id. */

char altaccfile[CDDBBUFSIZ];
char *categlist[CDDBMAXDBDIR + 1];
char admin_email[CDDBBUFSIZ];
char bounce_email[CDDBBUFSIZ];
char cddbdir[CDDBBUFSIZ];
char dupdir[CDDBBUFSIZ];
char host[CDDBHOSTNAMELEN + 1];
char apphost[CDDBHOSTNAMELEN + 1];
static char lockfile[CDDBBUFSIZ];
static char motdfile[CDDBBUFSIZ];
char postdir[CDDBBUFSIZ];
char pwdfile[CDDBBUFSIZ];
char rhost[CDDBHOSTNAMELEN + 1];
char *servfile[SF_NUMSERVFILES];
char sitefile[CDDBBUFSIZ];
char smtphost[CDDBBUFSIZ];
char test_email[CDDBBUFSIZ];
char user[CDDBBUFSIZ];
char workdir[CDDBBUFSIZ];
static char *workdirs[WD_NUMWORKDIRS];

static char *accessfile = ACCESSFILE;
static char *lockprefix = "db_lck";
static char *tmp_prefix = "cddbd_";
static char *tmpdir = TMPDIR;

static char *cgi_request_method = "REQUEST_METHOD";
static char *cgi_query_string = "QUERY_STRING";
static char *cgi_content_len = "CONTENT_LENGTH";
static char *cgi_gateway_iface = "GATEWAY_INTERFACE";

static char *workdir_name[WD_NUMWORKDIRS] = {
	"locks",
	"server",
	"tmp"
};

static char *servfile_name[SF_NUMSERVFILES] = {
	"fuzzy_index",
	"history",
	"log"
};

static char *lock_name[LK_NUMLOCKS] = {
	"fuzzy",
	"mkfuzz",
	"hist",
	"log",
	"nus",
	"put",
	"update"
};

#ifdef DB_WINDOWS_FORMAT
static char *categlock_name[CATLK_NUMLOCKS] = {
	"catmerge"
};
#endif

char *asy_prefix[ASY_MAXPREFIX] = {
	"cmd=",
	"hello=",
	"proto="
};

static char *put_list[] = {
	altaccfile,
	motdfile,
	sitefile,
	0
};

static char *rejectstr = "Rejected connect [%s]: %s@%s (%s) using %s %s";
static char *hrejectstr = "Rejected connect [%s]: %s";
static char *hellostr = "Connect [%s]: %s@%s (%s) using %s %s";
static char *hellosstr =
    "%*s %*s %*s Connect [%127[^]]]: %127[^@]@%*s %*[(]%127[^)]%*s using %127s";
static char *readstr = "Read: %s %08x user:%08x %.65s";
static char *readsstr = "%*s %*s %*s Read: %*s %*08x user:%08x %65[^\r\n]";
static char *verstr =
    "cddbd v%sPL%d Copyright (c) 1996-2006 Steve Scherf et al.";
char *verstr2 =
    "cddbd v%sPL%d Copyright (c) Steve Scherf et al.";
char *saltstr = "salt=";

static pid_t curpid;				/* Our current pid. */

static int log_flags;				/* Logging flags. */

static long lastsecs;

hperm_t hperm;				/* Global host permissions. */

static lhead_t *lock_head;			/* Lock list head. */

/* Interface table. */
static iface_t iface_map[IF_NIFACE] = {
	{ "submit_mail",   's', (IFL_NOCOUNT | IFL_NOCONN | IFL_NOLOG) },
	{ "email",         'e', (IFL_NOCONN | IFL_CMD) },
	{ "cddbp",         'c', IFL_CMD },
	{ "http",          'h', IFL_CMD },
	{ "Unknown",      '\0', (IFL_NOCONN | IFL_NOHEAD) },
	{ "none",         '\0', (IFL_NOCONN | IFL_NOHEAD | IFL_NOLOG) }
};


/* Access file fields. */
static access_t acctab[] = {
	{ "altaccfile",  altaccfile,   AC_PATH,    0,	  (ad_t)"" },
	{ "apphost",     apphost,      AC_STRING,  0,	  (ad_t)host },
	{ "motdfile",    motdfile,     AC_PATH,    0,	  (ad_t)"" },
	{ "sitefile",    sitefile,     AC_PATH,    0,	  (ad_t)"" },
	{ "pwdfile",     pwdfile,      AC_PATH,    0,	  (ad_t)"" },
	{ "workdir",     workdir,      AC_PATH,    0,	  (ad_t)"" },
	{ "cddbdir",     cddbdir,      AC_PATH,    0,	  (ad_t)"" },
	{ "dupdir",      dupdir,	     AC_PATH,    0,	  (ad_t)"" },
	{ "postdir",     postdir,      AC_PATH,    0,	  (ad_t)"" },
	{ "smtphost",    smtphost,     AC_STRING,  0,       (ad_t)LHOST },
	{ "admin_email", admin_email,  AC_STRING,  AF_NODEF,(ad_t)(char *)0 },
	{ "bounce_email",bounce_email, AC_STRING,  0,	  (ad_t)"" },
	{ "test_email",  test_email,   AC_STRING,  0,	  (ad_t)"" },
	{ "elapse_time", &elapse_time, AC_NUMERIC, 0,	  (ad_t)DEF_ELAPSE_TIME },
	{ "delay_time",  &delay_time,  AC_NUMERIC, 0,	  (ad_t)DEF_DELAY_TIME },
	{ "xmit_time",   &xmit_time,   AC_NUMERIC, AF_ZERO, (ad_t)DEF_XMIT_TO },
	{ "email_time",  &email_time,  AC_NUMERIC, AF_ZERO, (ad_t)DEF_EMAIL_TIME },
	{ "lock_time",   &lock_time,   AC_NUMERIC, AF_ZERO, (ad_t)DEF_LOCK_TIME },
	{ "hang_time",   &hang_time,   AC_NUMERIC, AF_ZERO, (ad_t)DEF_HANG_TIME },
	{ "max_hangs",   &max_hangs,   AC_NUMERIC, AF_ZERO, (ad_t)DEF_MAX_HANG },
	{ "lock_wait",   &lock_wait,   AC_NUMERIC, AF_ZERO, (ad_t)DEF_LOCK_WAIT },
	{ "transmits",   &max_xmits,   AC_NUMERIC, AF_ZERO, (ad_t)DEF_MAX_XMITS },
	{ "post_lines",  &max_lines,   AC_NUMERIC, AF_ZERO, (ad_t)DEF_MAX_LINES },
	{ "put_size",    &put_size,    AC_NUMERIC, AF_ZERO, (ad_t)DEF_MAX_SIZE },
	{ "users",       &max_users,   AC_NUMERIC, AF_ZERO, (ad_t)DEF_MAX_USERS },
	{ "fuzzy_factor",&fuzzy_factor,AC_NUMERIC, AF_ZERO, (ad_t)DEF_FUZZ_FACT },
	{ "fuzzy_div",   &fuzzy_div,   AC_NUMERIC, AF_NONZ, (ad_t)DEF_FUZZ_DIV },
	{ "log_hiwat",   &log_hiwat,   AC_NUMERIC, AF_ZERO, (ad_t)DEF_LOG_HIWAT },
	{ "log_lowat",   &log_lowat,   AC_NUMERIC, AF_ZERO, (ad_t)DEF_LOG_LOWAT },
	{ "user",        &uid,	     AC_USER,    AF_NODEF,(ad_t)(uid_t)0 },
	{ "group",       &gid,	     AC_GROUP,   AF_NODEF,(ad_t)(gid_t)0 },
	{ "file_mode",   &file_mode,   AC_MODE,    0,	  (ad_t)DEF_FILE_MODE },
	{ "dir_mode",    &dir_mode,    AC_MODE,    0,	  (ad_t)DEF_DIR_MODE },
	{ "db_user",     &db_uid,      AC_USER,    AF_NODEF,(ad_t)(uid_t)0 },
	{ "db_group",    &db_gid,      AC_GROUP,   AF_NODEF,(ad_t)(gid_t)0 },
	{ "db_file_mode",&db_file_mode,AC_MODE,    0,	  (ad_t)DEF_FILE_MODE },
	{ "db_dir_mode", &db_dir_mode, AC_MODE,    0,	  (ad_t)DEF_DIR_MODE },
	{ "strip_ext",   &strip_ext,   AC_BOOL,    0,	  (ad_t)DEF_STRIP_EXT },
	{ "ck_berzerk",  &ck_berzerk,  AC_BOOL,    0,	  (ad_t)DEF_CK_BERZERK },
	{ 0 }
};


/* Protocol flags. */
proto_t proto_flags[MAX_PROTO_LEVEL + 1] = {
	0,
	0,
	P_QUOTE,
	P_SITES,
	P_QUERY,
	P_READ,
	P_UTF8
};


/* Log flags. */
static log_t log[] = {
	{ LOG_NONE,    0, 0,        "none",   0, },
	{ LOG_INPUT,   0, 0,        "input",  0, },
	{ LOG_INFO,    0, 0,        "info",   0, },
	{ (unsigned int)LOG_ALL,     0, 0,        "all",    0, },
	{ LOG_ACCESS,  0, 0,        "access", "Accesses", },
	{ LOG_QUERY,   0, 0,        "",       "    Successful queries", },
	{ LOG_UQUERY,  0, 0,        "",       "    Unsuccessful queries", },
	{ LOG_FUZZY,   0, 0,        "",       "    Fuzzy matches", },
	{ LOG_READ,    0, 0,        "",       "    Entries read", },
	{ LOG_WRITE,   0, L_NOSHOW, "post",   "    Entries posted", },
	{ LOG_SITES,   0, L_NOSHOW, "",       "    Site list downloads", },
	{ LOG_GET,     0, L_NOSHOW, "",       "    Admin downloads", },
	{ LOG_ERR,     0, 0,        "errors", "Errors", },
	{ LOG_XMIT,    0, L_NOSHOW, "",       "    Transmit errors", },
	{ LOG_NET,     0, L_NOSHOW, "",       "    Network errors", },
	{ LOG_LOCK,    0, L_NOSHOW, "",       "    Locking errors", },
	{ LOG_HASH,    0, L_NOSHOW, "",       "    Hash file errors", },
	{ LOG_UPDATE,  0, L_NOSHOW, "",       "    Update errors", },
	{ LOG_MAIL,    0, L_NOSHOW, "",       "    Mail errors", },
	{ LOG_INTERN,  0, L_NOSHOW, "",       "Internal errors", },
	{ LOG_REJECT,  0, L_NOSHOW, "",       "Rejected connections", },
	{ LOG_COLLIS,  0, L_NOSHOW, "",       "Disc ID collisions", },
	{ LOG_PASSWD,  0, L_NOSHOW, "",       "Validation attempts", },
	{ LOG_HELLO,   0, 0,        "hello",  "Connections", },
	{ 0 }
};


/* Timer array. */
ctimer_t timers[] = {
	{ "input",   cddbd_input_to,   CT_INPUT_RST,  DEF_INPUT_TO,   0, 0 },
	{ "access",  cddbd_access_to,  CT_ACCESS_RST, DEF_ACCESS_TO,  0, 0 },
	{ "connect", cddbd_connect_to, CT_WRITE_DIS,  DEF_CONNECT_TO, 0, 0 },
	{ 0 }
};


/* Help strings. */
static char **help[] = {
	cddb_help,
	discid_help,
	get_help,
	help_help,
	log_help,
	motd_help,
	proto_help,
	put_help,
	quit_help,
	sites_help,
	stat_help,
	update_help,
	validate_help,
	ver_help,
	whom_help,
	0
};

static char **chelp[] = {
	hello_help,
	lscat_help,
	query_help,
	read_help,
/*	srch_help, */
	unlink_help,
	write_help,
	0
};


/* Command definitions. */

static cmd_t cmd[] = {
	{ "cddb",  	do_cddb,  	cddb_help,     (CF_SUBCMD | CF_ASY) },
	{ "get",   	do_get,  	get_help,      (CF_ASY | CF_SECURE) },
	{ "help",  	do_help,  	help_help,     CF_ASY },
	{ "log",   	do_log,  	log_help,      CF_ASY },
	{ "motd",  	do_motd,  	motd_help,     CF_ASY },
	{ "validate",	do_validate, 	validate_help, CF_SECURE },
	{ "proto",  	do_proto,  	proto_help,    0 },
	{ "put",    	do_put,  	put_help,      CF_SECURE },
	{ "quit",   	do_quit,  	quit_help,     0 },
	{ "sites",  	do_sites,  	sites_help,    CF_ASY },
	{ "stat",   	do_stat,  	stat_help,     CF_ASY },
	{ "update", 	do_update, 	update_help,   CF_SECURE },
	{ "ver",    	do_ver,  	ver_help,      CF_ASY },
	{ "whom",   	do_whom,  	whom_help,     CF_ASY },
	{ "discid",	do_discid,	discid_help,   CF_ASY },
	{ 0 }
};

static cmd_t cddb_cmd[] = {
	{ "hello", do_cddb_hello, hello_help, 0 },
	{ "query", do_cddb_query, query_help, (CF_HELLO | CF_ACCESS | CF_ASY) },
	{ "read",  do_cddb_read,  read_help,  (CF_HELLO | CF_ACCESS | CF_ASY) },
/*	{ "srch",  do_cddb_srch,  srch_help,  (CF_HELLO | CF_ACCESS | CF_ASY) },*/
	{ "write", do_cddb_write, write_help, (CF_HELLO | CF_ACCESS | CF_SECURE)},
	{ "lscat", do_cddb_lscat, lscat_help, (CF_HELLO | CF_ACCESS | CF_ASY)},
	{ "unlink", do_cddb_unlink, unlink_help, (CF_HELLO | CF_ACCESS | CF_SECURE)},
	{ 0 }
};


/* Month table. */
static month_t month[] = {
	{ "Jan", 31 },
	{ "Feb", 29 },
	{ "Mar", 31 },
	{ "Apr", 30 },
	{ "May", 31 },
	{ "Jun", 30 },
	{ "Jul", 31 },
	{ "Aug", 31 },
	{ "Sep", 30 },
	{ "Oct", 31 },
	{ "Nov", 30 },
	{ "Dec", 31 }
};


/* Day table. */
static char *day[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

/* Convert charset for sending to client. */
void
convert_db_charset_proto(db_t *db, int plevel)
{
	if(db->db_flags & DB_ENC_ASCII)
		;
	else if(db->db_flags & DB_ENC_UTF8) {
		if(!(db->db_flags & DB_ENC_ASCII) && plevel < UNICODE_LEVEL)
			db_utf8_latin1(db);
	}
	else if(db->db_flags & DB_ENC_LATIN1) {
		if(plevel >= UNICODE_LEVEL)
			db_latin1_utf8(db);
	}
}

/* Convert charset for sending to client. */
void
convert_db_charset(db_t *db)
{
	convert_db_charset_proto(db, level);
}

/* ARGSUSED */
int
main(int argc, char **argv)
{
	int i;
	int port= 0;		/* indicates "default" port */
	int nmode;
	int fix_level= MIN_FIX_LEVEL;
	int check_level= MIN_CHECK_LEVEL;
	char *p;
	char *name;
	char when[CDDBBUFSIZ];

	/* remote operations */
	int rmt_op = RMT_OP_NONE;
	int nrmt_op = RMT_OP_NONE;
	char *rarg = (char *) 0;
	char *rarg2 = (char *) 0;
	char *remote = (char *) 0;

	/* Set the default interface type. */
	interface = IF_NONE;

	name = argv[0];

	mode = MODE_NONE;
	nmode = MODE_NONE;

	level = DEF_PROTO_LEVEL;

	ruid = getuid();
	rgid = getgid();
	euid = geteuid();
	egid = getegid();

	secure = cddbd_secure();

	if(cddbd_is_cgi())
		mode = MODE_CGI;
	else
	while(*++argv) {
		argc--;

		if(**argv != '-')
			usage(name);

		i = 0; /* number of following arguments processed */

		while(*++(*argv)) {
			switch(**argv) {
			case 'a':
				/* Don't allow rogues to change accessfile .*/
				if(!secure)
					usage_uid();

				NEXTARG(accessfile);

				break;

			case 'c':
				nmode = MODE_DB;
				check++;

				NEXTARG(p);
/** printf ("option c: p=%s\n", p);  **/

				if(p == 0)
					usage2(name, "1. p=0");

				if(!strcmp(p, "default")) {
					check_level = MIN_CHECK_LEVEL;
				}
				else {
					check_level = atoi(p);
					if(check_level < MIN_CHECK_LEVEL)
						usage2(name, "check_level < MIN_CHECK_LEVEL");

					if(check_level > MAX_CHECK_LEVEL)
						usage2(name, "check_level > MAX_CHECK_LEVEL");
				}

				NEXTARG(p);

				if(p == 0)
					usage2(name, "2. p=0");

				if(!strcmp(p, "default")) {
					fix_level = MIN_FIX_LEVEL;
				}
				else {
					fix_level = atoi(p);
					if(fix_level < MIN_FIX_LEVEL)
						usage2(name, "fix_level < MIN_FIX_LEVEL");

					if(fix_level > MAX_FIX_LEVEL)
						usage2(name, "fix_level > MAX_FIX_LEVEL");
				}

				break;

			case 'C':
				nmode = MODE_DB;
				nrmt_op = RMT_OP_CMD;

				NEXTARG(remote);
				NEXTARG(rarg);
				NEXTARG(rarg2);

				break;

			case 'd':
				debug++;
				break;

			case 'e':
				nmode = MODE_DB;
				match_entries++;
				break;

			case 'f':
				nmode = MODE_DB;
				build_fuzzy++;
				break;

			case 'k':
				nmode = MODE_IMMED;
				make_key++;
				break;

			case 'l':
				nmode = MODE_DB;
				nrmt_op = RMT_OP_LOG;

				NEXTARG(remote);
				break;

			case 'm':
				nmode = MODE_MAIL;
				break;

			case 'M':
				nmode = MODE_MAIL;
				test_mail++;
				break;

			case 'p':
				nmode = MODE_DB;
				nrmt_op = RMT_OP_PUT;

				NEXTARG(remote);
				NEXTARG(rarg);

				if(remote == 0 || rarg == 0)
					usage(name);

				break;

			case 'P':
				nmode = MODE_IMMED;
				make_pwd++;
				break;

			case 'q':
				quiet++;
				break;

			case 's':
				nmode = MODE_SERV;
				standalone++;

				NEXTARG(p);

				if(p == 0)
					usage(name);

				if(!strcmp(p, "default")) {
					port = 0;
				}
				else {
					port = atoi(p);
					if(port <= 0)
						usage(name);
				}

				break;

			case 'T':
				nmode = MODE_DB;
				rmt_op = RMT_OP_CATCHUP;

				NEXTARG(remote);

				if(remote == 0)
					usage(name);

				NEXTARG(p);

				if(p == 0)
					usage(name);

				if(!strcmp("now", p)) {
					strcpy(when, p);
				}
				else {
					if(cvt_date(p, when) == 0)
						usage(name);
				}

				rarg = when;

				break;

			case 't':
				nmode = MODE_DB;
				nrmt_op = RMT_OP_TRANSMIT;

				NEXTARG(remote);

				break;

			case 'u':
				nmode = MODE_DB;
				update++;
				break;

			case 'v':
				verbose++;
				break;

			default:
				usage(name);
			}

			if(i > 0 && *(argv + i) == 0)
				usage(name);

			if(nrmt_op != RMT_OP_NONE) {
				if(rmt_op != RMT_OP_NONE)
					usage(name);

				rmt_op = nrmt_op;
				nrmt_op = RMT_OP_NONE;
			}

			if(nmode != MODE_NONE) {
				if(mode != MODE_NONE && mode != nmode)
					usage(name);

				mode = nmode;
				nmode = MODE_NONE;
			}
		}

		argv += i;
	}

	/* Make a new key. */
	if(make_key) {
		generate_key();
		exit(QUIT_OK);
	}

	/* Make a password. */
	if(make_pwd) {
		generate_pwd();
		exit(QUIT_OK);
	}

	/* The default mode is server mode. */
	if(mode == MODE_NONE)
		mode = MODE_SERV;

	switch(mode) {
	case MODE_SERV:
		interface = IF_CDDBP;
		break;

	case MODE_CGI:
		interface = IF_HTTP;
		break;

	case MODE_DB:
		interface = IF_NONE;
		break;

	case MODE_MAIL:
	default:
		interface = IF_UNKNOWN;
		break;
	}

	/* If we're not secure, we can only function as a server. */
	if(!secure && !(mode & MFL_SERV))
		usage_uid();

	/* Initialize data structures. */
	cddbd_init();

	/* Set up the signal handler. */
	for(i = 0; i < NSIG; i++) {
		switch(i) {
		case SIGCHLD:
		case SIGHUP:
			/* Ignore these signals just in case. */
			signal(i, SIG_IGN);
			break;

		case SIGPIPE:
			/* Ignore this if we don't have a connection. */
			if(iface_map[interface].if_flags & IFL_NOCONN) {
				signal(i, SIG_IGN);
				break;
			}

			/* FALLTHROUGH */

		default:
			signal(i, sighand);
			break;
		}
	}

	/* Wait for connections. */
	if(standalone) {
		cddbd_stand(port);
		standalone = 0;
	}
	else {
		/* Get the configuration. */
		cddbd_check_access();

		/* If no interface, we need to check perms here. */
		if(interface == IF_NONE)
			hperm = *ck_host_perms(interface);
	}

	/* Find the remote hostname if we're a server. */
	if(mode != MODE_MAIL)
		get_rmt_hostname(0, 0, rhost);

	/* Set effective uid/gid if needed/possible. */
	cddbd_setuid();

	/* Initialize the database. */
	cddbd_db_init();

	/* Clean up the database. */
	if(check)
		cddbd_check_db(check_level, fix_level);

	/* Update the database with new entries. */
	if(update)
		cddbd_update();

	/* Link matching entries. */
	if(match_entries)
		cddbd_match();

	/* Build the fuzzy matching hash table. */
	if(build_fuzzy)
		cddbd_build_fuzzy();

	/* Perform remote ops. */
	if(rmt_op != RMT_OP_NONE)
		cddbd_rmt_op(rmt_op, remote, rarg, rarg2);

	/* Be a mail filter. */
	if(mode == MODE_MAIL) {
		if(test_mail)
			cddbd_mail(MF_TEST);
		else
			cddbd_mail(0);
	}

	/* Be a CGI program. */
	if(mode == MODE_CGI) {
		/* Make sure there aren't too many users. */
		cddbd_check_nus();

		/* Perform CGI mode service. */
		cddbd_cgi_cmd();
	}

	/* Be an Internet server. */
	if(mode == MODE_SERV) {
		/* Make sure there aren't too many users. */
		cddbd_check_nus();

		/* Start serving. */
		cddbd_cddbp_cmd();
	}

	quit(QUIT_OK);

  return 0;
}


int
cddbd_secure(void)
{
	int i;
	struct passwd *pw;

	for(i = 0; secure_users[i] != 0; i++) {
		pw = getpwnam(secure_users[i]);
		if(pw != NULL && pw->pw_uid == ruid)
			break;
	}

	/* If we're a trusted user, or not running setuid, we're secure. */
	return(secure_users[i] != 0 || ruid == euid || ruid == 0);
}


void
cddbd_setuid(void)
{
	/* If we're root, and we want to run under a different gid, change. */
	if(euid == 0 && egid != gid && gid == db_gid) {
		if(setgid(gid) != 0) {
			cddbd_log(LOG_ERR,
			    "Can't change gid: effective %d, wanted %d.",
			    egid, gid);

			quit(QUIT_ERR);
		}

		egid = getegid();
	}

	/* If we're root, and we want to run under a different uid, change. */
	if(euid == 0 && euid != uid && uid == db_uid) {
		if(setuid(uid) != 0) {
			cddbd_log(LOG_ERR,
			    "Can't change uid: effective %d, wanted %d.",
			    euid, uid);

			quit(QUIT_ERR);
		}

		euid = geteuid();
	}
}


void
cddbd_cddbp_cmd(void)
{
	int pf;
	char *p;
	cmd_t *c;
	arg_t args;
	int errcnt;

	/* Set the interface type. */
	interface = IF_CDDBP;

	/* Dup stdin to stdout and stderr. */
	dup2(0, 1);
	dup2(0, 2);

	/* Set up client permissions. */
	hperm = *ck_host_perms(interface);

	/* Start up banner. */
	p = (WRITE_OK(hperm) ? "200" : "201");
	printf("%s %s CDDBP server v%sPL%d ready at %s\r\n",
		p, apphost, VERSION, PATCHLEVEL, get_time(1));
	fflush(stdout);

	/* Command processing loop. */
	for(errcnt = 0;;) {
		if(cddbd_gets(args.buf, sizeof(args.buf)) == NULL)
			break;

		if(strlen(args.buf) >= (sizeof(args.buf) - 1)) {
			printf("500 Input too long.\r\n");
			errcnt++;
			continue;
		}

		/* Remove CR/LF so we can print this. */
		strip_crlf(args.buf);

		if(charset_is_valid_latin1(args.buf) || charset_is_valid_utf8(args.buf)) {
			cddbd_log(LOG_INPUT, "Input: \"%s\"", args.buf);

			/* Parse the input. */
			if(PROTO_ENAB(P_QUOTE))
				pf = PF_HONOR_QUOTE | PF_REMAP_QSPC;
			else
				pf = 0;

			cddbd_parse_args(&args, pf);

			if(args.flags & (AF_TRUNC | AF_MAXARG)) {
				printf("500 Input too long.\r\n");
				errcnt++;
				continue;
			}

			/* Skip empty input lines. */
			if(args.nargs == 0)
				continue;

			/* Execute the command. */
			for(c = cmd; c->cmd; c++) {
				if(!cddbd_strcasecmp(args.arg[0], c->cmd)) {
					/* Ensure we have a handshake. */
					if((c->flags & CF_HELLO) && !hello) {
						printf("409 No handshake.\r\n");
						break;
					}

					/* Disallow if not secure. */
					if((c->flags & CF_SECURE) && !secure) {
						printf("401 Permission denied."
						    "\r\n");
						break;
					}

					(*(c->func))(&args);
					break;
				}
			}

			/* No command match found. */
			if(c->cmd == 0) {
				errcnt++;
				printf("500 Unrecognized command.\r\n");
			}
			else
				errcnt = 0;
		}
		else {
			errcnt++;
			printf("500 Illegal input.\r\n");
		}

		if(errcnt >= CDDBMAXERR) {
			printf("530 Too many errors, closing connection.\r\n");
			_quit(QUIT_ERR, 0);
		}

		fflush(stdout);
	}

	quit(QUIT_OK);
}


void
sighand(int sig)
{
	switch(sig) {
	case SIGPIPE:
		/* This signal is okay. */
		quit(QUIT_OK);

		/* NOTREACHED */

	case SIGHUP:
		signal(SIGHUP, SIG_IGN);
		cddbd_check_access();

		return;

	case SIGTERM:
		/* This signal is okay. */
		if(standalone)
			quit(QUIT_OK);

		break;

	case SIGWINCH:
		return;

	default:
		break;
	}

	cddbd_log(LOG_ERR, "Received signal %d.", sig);
	quit(QUIT_ERR);
}


void
cddbd_block_sigs(void)
{
	int i;

	for(i = 0; i < NSIG; i++) {
		switch(i) {
		case SIGSEGV:
		case SIGBUS:
		case SIGFPE:
		case SIGILL:
		case SIGABRT:
			signal(i, SIG_DFL);
			break;

		default:
			signal(i, SIG_IGN);
			break;
		}
	}
}


void
quit(int status)
{
	_quit(status, 1);
}


void
_quit(int status, int msg)
{
	lhead_t *lh;
	static int quitting = 0;

	/* Avoid endless loops. */
	if(quitting > MAX_QUITS)
		exit(status);

	quitting++;

	/* Lock out (almost) all signals so we can exit in peace. */
	cddbd_block_sigs();

	/* If this is the standalone daemon, exit safely. */
	if(standalone) {
		cddbd_log(LOG_INFO, "Standalone daemon exiting, status: %d",
		    status);
		exit(status);
	}

	/* Log connections from gawkers. */
	if(CONNECT_OK(hperm) && !hello && !skip_log &&
	    !(iface_map[interface].if_flags & IFL_NOLOG))
		cddbd_log(LOG_HELLO, "Connect [%s]: %s, no handshake.",
		    iface_map[interface].if_name, rhost);

	/* Unlink our private files. */
	if(lockfile[0] != '\0')
		unlink(lockfile);

	/* We may need to clean up the history file. */
	if(locks[LK_HIST] != 0 && locks[LK_HIST]->lk_refcnt == 0)
		(void)cddbd_close_history();

	if(status != QUIT_OK)
		cddbd_log(LOG_INFO, "Quitting, status %d.", status);

	/* Clean out any leftover tmp files. */
	cddbd_cleantemp();

	/* Free any pending locks. */
	if(lock_head != 0) {
		lh = lock_head;
		lock_head = 0;
		list_free(lh);
	}

	if((mode & MFL_QMSG) && msg) {
		if(status == QUIT_OK) {
			printf("230 %s Closing connection.  Goodbye.\r\n",
			    apphost);
		}
		else {
			printf("530 %s Server error, closing connection.\r\n",
			    apphost);
		}
	}

	fflush(stdout);
	exit(status);
}


int
cddbd_strcasecmp(char *s1, char *s2)
{
	int c1;
	int c2;

	if(s1 == NULL || s2 == NULL)
		return 0;

	for(;;) {
		if(isupper(*s1))
			c1 = tolower(*s1);
		else
			c1 = (int)(unsigned char)*s1;

		if(isupper(*s2))
			c2 = tolower(*s2);
		else
			c2 = (int)(unsigned char)*s2;

		if(c1 != c2 || c1 == '\0' || c2 == '\0')
			return(c1 - c2);

		s1++;
		s2++;
	}
}


int
cddbd_strncasecmp(char *s1, char *s2, int n)
{
	int c1;
	int c2;

	if(s1 == NULL || s2 == NULL)
		return 0;

	while(n--) {
		if(isupper(*s1))
			c1 = tolower(*s1);
		else
			c1 = (int)(unsigned char)*s1;

		if(isupper(*s2))
			c2 = tolower(*s2);
		else
			c2 = (int)(unsigned char)*s2;

		if(c1 != c2 || c1 == '\0' || c2 == '\0')
			return(c1 - c2);

		s1++;
		s2++;
	}

	return 0;
}


char *
cddbd_strcasestr(char *hay, char *needle)
{
	int len;

	if(hay == NULL || needle == NULL)
		return 0;

	len = strlen(needle);

	for(; *hay != '\0'; hay++)
		if(!cddbd_strncasecmp(hay, needle, len))
			return hay;

	return 0;
}


int
cddbd_charcasecmp(char *set, char *s)
{
	int i;
	char *p;
	char buf[CDDBBUFSIZ];
	char buf2[CDDBBUFSIZ];

	for(i = 0, p = buf; set[i] != '\0' && (p - buf) < CDDBBUFSIZ; i++)
		if(isalnum(set[i])) {
			*p = set[i];
			p++;
		}

	*p = '\0';

	for(i = 0, p = buf2; s[i] != '\0' && (p - buf2) < CDDBBUFSIZ; i++)
		if(isalnum(s[i])) {
			*p = s[i];
			p++;
		}

	*p = '\0';

	return(cddbd_strcasecmp(buf, buf2));
}


/* check the current number of users */
void
cddbd_check_nus(void)
{
	FILE *fp;

	if(max_users == 0)
		return;

	(int)cddbd_lock(locks[LK_NUS], 1);

	nus = cddbd_nus(0);

	if(nus >= max_users) {
		cddbd_log(LOG_INFO,
		    "Service rejected: nus (%d) >= max_users (%d).",
		    nus, max_users);

		printf("433 No connections allowed: ");
		printf("%d users allowed, %d currently active.\r\n",
		    max_users, nus);

		_quit(QUIT_ERR, 0);
	}

	cddbd_snprintf(lockfile, sizeof(lockfile), "%s/%s.%05d",
	    workdirs[WD_LOCK], lockprefix, curpid);

	if((fp = fopen(lockfile, "w")) == NULL) {
		cddbd_log(LOG_ERR, "Can't create lock file: %s (%d).",
		    lockfile, errno);
		quit(QUIT_ERR);
	}

	fprintf(fp, "unknown unknown - %s", rhost);
	fclose(fp);

	(void)cddbd_fix_file(lockfile, file_mode, uid, gid);

	cddbd_unlock(locks[LK_NUS]);
}


/* retrieve number of users; if list==1, print list of users */
int
cddbd_nus(int list)
{
	int tpid;
	int len;
	int nus;
	FILE *fp;
	DIR *dirp;
	pid_t pid;
	char hst[CDDBBUFSIZ];
	char rhst[CDDBBUFSIZ];
	char user[CDDBBUFSIZ];
	char clnt[CDDBBUFSIZ];
	char lock[CDDBBUFSIZ];
	struct dirent *dp;

	/* Count active users. */
	if((dirp = opendir(workdirs[WD_LOCK])) == NULL) {
		cddbd_log(LOG_ERR, "Can't open lock dir: %s (%d).",
		    workdirs[WD_LOCK], errno);
		return -1;
	}

	nus = 0;

	/* count lock files in  the lock dir and print their contents if list==1 */
	while((dp = readdir(dirp)) != NULL) {
		len = strlen(lockprefix);

		if(!strncmp(dp->d_name, lockprefix, len)) {
			if(sscanf(&dp->d_name[len], ".%d", &tpid) != 1)
				continue;

			pid = (pid_t)tpid;

			cddbd_snprintf(lock, sizeof(lock), "%s/%s",
			    workdirs[WD_LOCK], dp->d_name);

			if(kill(pid, 0) != 0 && errno == ESRCH) {
				/* The lock file is stale, remove it. */
				unlink(lock);
			}
			else {
				if(list) {
					fp = fopen(lock, "r");
					if(fp == NULL || fscanf(fp, "%s%s%s%s",
					    user, hst, clnt, rhst) != 4) {
						printf("%-5d  -         ", pid);
						printf("unknown\r\n");
					}
					else {
						clnt[8] = '\0';
						printf("%-5d  %-8s  %s@%s (%s)",
						    pid, clnt, user, hst, rhst);
						printf("\r\n");
					}
				}

				/* Count this user. */
				nus++;
			}
		}
	}

	closedir(dirp);

	return nus;
}


void
cddbd_check_access(void)
{
	int i;
	int n;
	ctimer_t *tp;
	access_t *at;
	iface_t *ip;

	/* Set up defaults. */
	for(i = 0; acctab[i].at_fname != 0; i++) {
		at = &acctab[i];

		/* No default for this field. */
		if(at->at_flags & AF_NODEF)
			continue;

		switch(at->at_type) {
		case AC_PATH:
		case AC_STRING:
			strcpy((char *)at->at_addr, (char *)at->at_def);
			break;

		case AC_USER:
			n = ptr_to_uint32 (at->at_def);
			*(uid_t *)at->at_addr = (uid_t)n;
			break;

		case AC_GROUP:
			n = ptr_to_uint32 (at->at_def);
			*(gid_t *)at->at_addr = (gid_t)n;
			break;

		case AC_NUMERIC:
		case AC_MODE:
		case AC_BOOL:
			*(int *)at->at_addr = ptr_to_uint32 (at->at_def);
			break;

		default:
			cddbd_log(LOG_ERR,
			    "Internal error: bad access type: %d.",
			    acctab[i].at_type);

			quit(QUIT_ERR);
		}
	}

	dup_ok = 0;
	dup_policy = DUP_DEFAULT;
	log_flags = LOG_ALL;

	/* Default charset handling. */
	file_charset = FC_PREFER_ISO;
	file_df_flags = DF_ENC_LATIN1 | DF_ENC_UTF8;
	utf_as_iso = UAI_REJECT;

	uid = geteuid();
	gid = getegid();
	db_uid = uid;
	db_gid = gid;

	for(tp = timers; tp->func != 0; ++tp)
		tp->seconds = tp->def_seconds;

	for(i = 0; i < IF_NIFACE; i++) {
		ip = &iface_map[i];

		if(ip->if_flags & IFL_NOHEAD)
			continue;

		if(ip->if_host != NULL)
			list_free(ip->if_host);

		if(ip->if_client != NULL)
			list_free(ip->if_client);

		ip->if_host = list_init(0, 0, free_hperm, 0);
		ip->if_client = list_init(0, 0, free_cperm, 0);

		if(ip->if_host == NULL || ip->if_client == NULL) {
			cddbd_log(LOG_ERR, "Can't allocate list entry.");
			quit(QUIT_ERR);
		}
	}

	/* Read the access file. */
	cddbd_read_access(accessfile, 1);

	/* If we have an alternate access file, read it. */
	if(strlen(altaccfile) != 0)
		cddbd_read_access(altaccfile, 0);

	cddbd_mk_workdirs();
	cddbd_mk_servfiles();

	/* Verify what we've found */

	if(max_hangs > max_users) {
		cddbd_log(LOG_ERR, "Max concurrent hangs (%d) > max users (%d)."
		    " Setting to %d.", max_hangs, max_users, max_users);
		max_hangs = max_users;
	}

	/* Must have a work dir. */
	if(workdir[0] == '\0') {
		cddbd_log(LOG_ERR, "No work directory specified in %s.",
		    accessfile);
		quit(QUIT_ERR);
	}

	/* Must have a database dir. */
	if(cddbdir[0] == '\0') {
		cddbd_log(LOG_ERR, "No CDDB directory specified in %s.",
		    accessfile);
		quit(QUIT_ERR);
	}

	if(log_hiwat <= log_lowat && log_hiwat > 0) {
		cddbd_log(LOG_ERR, "Log file low water mark is not lower than"
		    " the high water mark - disabling auto-paring.");

		log_hiwat = 0;
		log_lowat = 0;
	}

	if(log_hiwat == 0 || log_lowat == 0) {
		log_hiwat = 0;
		log_lowat = 0;
	}
}

/**
 * Reads access file.
 *
 * @param accessfile  path to access file
 * @param mandatory   0 if failure to open not fatal
 */
void
cddbd_read_access(char *accessfile, int mandatory)
{
	int i;
	int n;
	int line;
	FILE *fp;
	log_t *lp;
	arg_t args;
	iface_t *ip;
	cperm_t *cp;
	hperm_t *hp;
	ctimer_t *tp;
	char buf[CDDBBUFSIZ];
	char errstr[CDDBBUFSIZ];
	char *p;


	if((fp = fopen(accessfile, "r")) == NULL) {
		if(mandatory) {
			cddbd_log(LOG_ERR, "Can't open access file: %s (%d).",
			    accessfile, errno);
			quit(QUIT_ERR);
		}

		return;
	}

	line = 0;

	while(fgets(args.buf, sizeof(args.buf), fp) != NULL) {
		line++;
		cddbd_parse_args(&args, 0);

		/* Skip comments and blank lines. */
		if(args.nargs == 0 || args.arg[0][0] == '#')
			continue;

		/* Warn of lines that can't be properly parsed. */
		if(args.flags & (AF_TRUNC | AF_MAXARG)) {
			cddbd_log(LOG_ERR,
			    "Parse error in access file %s on line %d.",
			    accessfile, line);
		}

		/* Check for generic fields. */
		if(cddbd_parse_access(&args, line))
			continue;

		/* Read permissions. */
		if(!cddbd_strcasecmp("host_perms:", args.arg[0])) {
			if(args.nargs != 9)
				cddbd_access_err(line);

			/* Don't scan these if we're just doing DB ops. */
			if(!(mode & MFL_SERV))
				continue;

			if(!cddbd_parse_iface(args.arg[1], buf, line, errstr)) {
				cddbd_log(LOG_ERR, errstr);
				quit(QUIT_ERR);
			}

			for(p = buf; *p != '\0'; p++) {
				for(i = 0; i < IF_NIFACE; i++)
					if(iface_map[i].if_char == *p)
						break;

				ip = &iface_map[i];

				hp = (hperm_t *)malloc(sizeof(hperm_t));
				if(hp == NULL) {
					cddbd_log(LOG_ERR, "Malloc failed.");
					quit(QUIT_ERR);
				}

				if(list_add_back(ip->if_host, (void *)hp) == 0){
					cddbd_log(LOG_ERR,
					    "Can't allocate list entry.");
					quit(QUIT_ERR);
				}

				/* Start with the defaults. */
				*hp = hperm;

				hp->hp_host = strdup(args.arg[2]);
				if(hp->hp_host == NULL) {
					cddbd_log(LOG_ERR, "Malloc failed.");
					quit(QUIT_ERR);
				}

				if(!cddbd_strcasecmp("connect", args.arg[3]))
					hp->hp_connect = HP_CONNECT_OK;
				else if(!cddbd_strcasecmp("noconnect",
				    args.arg[3]))
					hp->hp_connect = HP_CONNECT_NO;
				else if(!cddbd_strcasecmp("hang",
				    args.arg[3])) {
					if(ip->if_flags & IFL_NOCONN)
						hp->hp_connect = HP_CONNECT_NO;
					else
						hp->hp_connect =
						    HP_CONNECT_HANG;
				}
				else
					cddbd_access_err(line);

				/* Take the defaults if not secure. */
				if(!secure)
					break;

				if(!cddbd_strcasecmp("post", args.arg[4]))
					hp->hp_write = HP_WRITE_OK;
				else if(!cddbd_strcasecmp("nopost",
				    args.arg[4]))
					hp->hp_write = HP_WRITE_NO;
				else
					cddbd_access_err(line);

				if(!cddbd_strcasecmp("update", args.arg[5]))
					hp->hp_update = HP_UPDATE_OK;
				else if(!cddbd_strcasecmp("noupdate",
				    args.arg[5]))
					hp->hp_update = HP_UPDATE_NO;
				else
					cddbd_access_err(line);

				if(!cddbd_strcasecmp("get", args.arg[6]))
					hp->hp_get = HP_GET_OK;
				else if(!cddbd_strcasecmp("noget", args.arg[6]))
					hp->hp_get = HP_GET_NO;
				else
					cddbd_access_err(line);

				if(!cddbd_strcasecmp("put", args.arg[7]))
					hp->hp_put = HP_PUT_OK;
				else if(!cddbd_strcasecmp("noput", args.arg[7]))
					hp->hp_put = HP_PUT_NO;
				else
					cddbd_access_err(line);

				if(!cddbd_strcasecmp("nopasswd", args.arg[8]))
					hp->hp_passwd = HP_PASSWD_OK;
				else {
					if(strlen(args.arg[8]) > CDDBPLBLSIZ) {
						cddbd_log(LOG_ERR,
						    "Password label too long.");
						cddbd_access_err(line);
					}

					hp->hp_passwd = HP_PASSWD_REQ;
					strcpy(hp->hp_pwdlbl, args.arg[8]);
				}
			}

			continue;
		}

		if(!cddbd_strcasecmp("client_perms:", args.arg[0])) {
			if(args.nargs != 6)
				cddbd_access_err(line);

			if(!cddbd_parse_iface(args.arg[1], buf, line, errstr)) {
				cddbd_log(LOG_ERR, errstr);
				quit(QUIT_ERR);
			}

			for(p = buf; *p != '\0'; p++) {
				for(i = 0; i < IF_NIFACE; i++)
					if(iface_map[i].if_char == *p)
						break;

				ip = &iface_map[i];

				cp = (cperm_t *)malloc(sizeof(cperm_t));
				if(cp == NULL) {
					cddbd_log(LOG_ERR, "Malloc failed.");
					quit(QUIT_ERR);
				}

				if(list_add_back(ip->if_client, (void *)cp)
				    == 0) {
					cddbd_log(LOG_ERR,
					    "Can't allocate list entry.");
					quit(QUIT_ERR);
				}

				if(!strcmp(args.arg[2], "allow"))
					cp->cp_perm = CP_ALLOW;
				else if(!strcmp(args.arg[2], "disallow"))
					cp->cp_perm = CP_DISALLOW;
				else if(!strcmp(args.arg[2], "hang")) {
					if(ip->if_flags & IFL_NOCONN)
						cp->cp_perm = CP_DISALLOW;
					else
						cp->cp_perm = CP_HANG;
				}
				else
					cddbd_access_err(line);

				cp->cp_client = strdup(args.arg[3]);
				cp->cp_lrev = strdup(args.arg[4]);
				cp->cp_hrev = strdup(args.arg[5]);

				if(cp->cp_client == NULL || cp->cp_lrev == NULL ||
				    cp->cp_hrev == NULL) {
					cddbd_log(LOG_ERR, "Malloc failed.");
					quit(QUIT_ERR);
				}

				if(strcmp(cp->cp_lrev, "-") &&
				    strcmp(cp->cp_hrev, "-") &&
				    strcmp(cp->cp_lrev, cp->cp_hrev) &&
				    match_rev(cp->cp_lrev, cp->cp_hrev,
				    CC_NEWER)) {
					cddbd_log(LOG_ERR,
					    "Lower bound > upper bound "
					    "in perms rule on line %d in %s.",
					    line, accessfile);
					quit(QUIT_ERR);
				}
			}

			continue;
		}

		/* Scan for timers. */
		for(tp = timers; tp->func != 0; ++tp) {
			cddbd_snprintf(buf, sizeof(buf), "%s_time:", tp->name);

			if(!cddbd_strcasecmp(buf, args.arg[0])) {
				if(args.nargs != 2)
					cddbd_access_err(line);

				if(sscanf(args.arg[1], "%d", &n) != 1)
					cddbd_access_err(line);

				/* Ensure the timeout is legal. */
				if(n < 0) {
					cddbd_log(LOG_ERR,
					    "Illegal %s value, line %d.",
					    tp->name, accessfile);
					quit(QUIT_ERR);
				}

				tp->seconds = n;

				break;
			}
		}

		/* Found a match, so go back and read the next line. */
		if(tp->func != 0)
			continue;

		/* Scan for logging flags. */
		if(!cddbd_strcasecmp("logging:", args.arg[0])) {
			if(args.nargs < 2)
				cddbd_access_err(line);

			log_flags = 0;

			for(i = 1; i < args.nargs; i++) {
				for(lp = log; lp->name; lp++) {
					if(!cddbd_strcasecmp(args.arg[i],
					    lp->name)) {
						if(lp->flag)
							log_flags |= lp->flag;
						else
							log_flags = 0;
						break;
					}
				}

				/* Illegal arg. */
				if(lp->name == 0)
					cddbd_access_err(line);
			}

			continue;
		}

		/* Scan for update flags. */
		if(!cddbd_strcasecmp("dup_policy:", args.arg[0])) {
			if(args.nargs != 2)
				cddbd_access_err(line);

			if(!strcmp(args.arg[1], "never")) {
				dup_policy = DUP_NEVER;
				continue;
			}

			if(!strcmp(args.arg[1], "compare")) {
				dup_policy = DUP_COMPARE;
				continue;
			}

			if(!strcmp(args.arg[1], "always")) {
				dup_policy = DUP_ALWAYS;
				continue;
			}

			cddbd_access_err(line);
		}

		/* Scan for file_charset flags. */
		if(!cddbd_strcasecmp("file_charset:", args.arg[0])) {
			if(args.nargs != 2)
				cddbd_access_err(line);
			if(!strcmp(args.arg[1], "only_iso")) {
				file_charset = FC_ONLY_ISO;
				file_df_flags = DF_ENC_LATIN1;
			}
			else if(!strcmp(args.arg[1], "prefer_iso")) {
				file_charset = FC_PREFER_ISO;
				file_df_flags = DF_ENC_LATIN1 | DF_ENC_UTF8;
			}
			else if(!strcmp(args.arg[1], "prefer_utf")) {
				file_charset = FC_PREFER_UTF;
				file_df_flags = DF_ENC_LATIN1 | DF_ENC_UTF8;
			}
			else if(!strcmp(args.arg[1], "only_utf")) {
				file_charset = FC_ONLY_UTF;
				file_df_flags = DF_ENC_UTF8;
				continue;
			}
			else
				cddbd_access_err(line);
			continue;
		}

		/* Scan for utf_as_iso flags. */
		if(!cddbd_strcasecmp("utf_as_iso:", args.arg[0])) {
			if(args.nargs != 2)
				cddbd_access_err(line);
			if(!strcmp(args.arg[1], "accept"))
				utf_as_iso = UAI_ACCEPT;
			else if(!strcmp(args.arg[1], "reject"))
				utf_as_iso = UAI_REJECT;
			else if(!strcmp(args.arg[1], "convert"))
				utf_as_iso = UAI_CONVERT;
			else
				cddbd_access_err(line);
			continue;
		}

		/* Garbage line in access file. */
		cddbd_access_err(line);
	}

	fclose(fp);
}


void
free_hperm(void *h)
{
	hperm_t *hp;

	hp = (hperm_t *)h;
	free(hp->hp_host);
	free(hp);
}


void
free_cperm(void *c)
{
	cperm_t *cp;

	cp = (cperm_t *)c;
	free(cp->cp_client);
	free(cp->cp_lrev);
	free(cp->cp_hrev);
	free(cp);
}


int
cddbd_parse_iface(char *s, char *buf, int line, char *errstr)
{
	int i;
	char *p;
	iface_t *ip;
	int ifs[IF_NIFACE];

	if(*s == '-') {
		p = buf;

		for(i = 0; i < IF_NIFACE; i++) {
			ip = &iface_map[i];

			if(ip->if_flags & IFL_NOHEAD)
				continue;

			if(strchr(s, ip->if_char))
				continue;

			*p = ip->if_char;
			p++;
		}

		*p = '\0';
		p = s + 1;
	}
	else {
		strncpy(buf, s, CDDBBUFSIZ);
		buf[CDDBBUFSIZ - 1] = '\0';
		p = s;
	}

	if(buf[0] == '\0') {
		cddbd_snprintf(errstr, CDDBBUFSIZ,
		    "No interface flag defined on line "
		    "%d in %s.", line, accessfile);
		return 0;
	}

	for(i = 0; i < IF_NIFACE; i++)
		ifs[i] = 0;

	for(; *p != '\0'; p++) {
		for(i = 0; i < IF_NIFACE; i++)
			if(iface_map[i].if_char == *p)
				break;

		if(i == IF_NIFACE)
			cddbd_access_err(line);

		ip = &iface_map[i];

		if(ip->if_flags & IFL_NOHEAD)
			cddbd_access_err(line);

		if(ifs[i]) {
			cddbd_snprintf(errstr, CDDBBUFSIZ,
			    "Interface flag '%c' multiply "
			    "defined on line %d in %s.",
			    ip->if_char, line, accessfile);
			return 0;
		}

		ifs[i]++;
	}

	return 1;
}


int
cddbd_parse_access(arg_t *args, int line)
{
	int i;
	int n;
	char *p;
	access_t *at= (access_t *) 0;
	struct group *gr;
	struct passwd *pw;

	for(i = 0; acctab[i].at_fname != 0; i++) {
		at = &acctab[i];
		n = strlen(at->at_fname);

		if(!strncmp(at->at_fname, args->arg[0], n) &&
		    args->arg[0][n] == ':')
			break;
	}

	if(acctab[i].at_fname == 0 || at == (access_t *) 0)
		return 0;

	switch(at->at_type) {
	case AC_PATH:
		if(args->nargs != 2)
			cddbd_access_err(line);

		if((int)strlen(args->arg[1]) > CDDBPATHSIZ) {
			cddbd_log(LOG_ERR,
			    "Pathname too long (max %d) on line %d in %s.",
			    CDDBPATHSIZ, line, accessfile);

			quit(QUIT_ERR);
		}

		p = (char *)at->at_addr;
		strncpy(p, args->arg[1], CDDBPATHSIZ);

		/* Ensure the value is legal. */
		if(p[0] != '/') {
			cddbd_log(LOG_ERR,
			    "Absolute path required for %s on line %d in %s.",
			    at->at_fname, line, accessfile);

			quit(QUIT_ERR);
		}

		break;

	case AC_STRING:
		if(args->nargs != 2)
			cddbd_access_err(line);

		if((int)strlen(args->arg[1]) > CDDBBUFSIZ) {
			cddbd_log(LOG_ERR,
			    "Value too long (max %d) on line %d in %s.",
			    CDDBBUFSIZ, line, accessfile);

			quit(QUIT_ERR);
		}

		p = (char *)at->at_addr;
		strncpy(p, args->arg[1], CDDBBUFSIZ);
		p[CDDBBUFSIZ - 1] = '\0';

		break;

	case AC_BOOL:
		if(args->nargs != 2)
			cddbd_access_err(line);

		if(sscanf(args->arg[1], "%d", &n) == 1) {
			if(n < 0 || n > 1)
				cddbd_access_err(line);
		}
		else if(!cddbd_strcasecmp(args->arg[1], "no") ||
		    !cddbd_strcasecmp(args->arg[1], "false"))
			n = 0;
		else if(!cddbd_strcasecmp(args->arg[1], "yes") ||
		    !cddbd_strcasecmp(args->arg[1], "true"))
			n = 1;
		else
			cddbd_access_err(line);

		*(int *)at->at_addr = n;

		break;

	case AC_NUMERIC:
		if(args->nargs != 2)
			cddbd_access_err(line);

		if(sscanf(args->arg[1], "%d", &n) != 1)
			cddbd_access_err(line);

		/* Ensure the value is positive. */
		if((at->at_flags & AF_ZERO) && n < 0) {
			cddbd_log(LOG_ERR,
			    "Illegal value for %s (%d), line %d in %s.",
			    at->at_fname, n, line, accessfile);
			quit(QUIT_ERR);
		}

		/* Ensure the value is greater than zero. */
		if((at->at_flags & AF_NONZ) && n < 1) {
			cddbd_log(LOG_ERR,
			    "Illegal value for %s (%d), line %d in %s.",
			    at->at_fname, n, line, accessfile);
			quit(QUIT_ERR);
		}

		*(int *)at->at_addr = n;

		break;

	case AC_USER:
		if(args->nargs != 2)
			cddbd_access_err(line);

		if(!strcmp(args->arg[1], "default"))
			break;

		if(isdigit(args->arg[1][0])) {
			*(int *)at->at_addr = atoi(args->arg[1]);
			break;
		}

		/* Ensure the value is legal. */
		if((pw = getpwnam(args->arg[1])) == NULL) {
			cddbd_log(LOG_ERR,
			    "Unknown user \"%s\", line %d in %s.",
			    args->arg[1], line, accessfile);
			quit(QUIT_ERR);
		}

		*(uid_t *)at->at_addr = (int)pw->pw_uid;
		endpwent();

		break;

	case AC_GROUP:
		if(args->nargs != 2)
			cddbd_access_err(line);

		if(!strcmp(args->arg[1], "default"))
			break;

		if(isdigit(args->arg[1][0])) {
			*(int *)at->at_addr = atoi(args->arg[1]);
			break;
		}

		/* Ensure the value is legal. */
		if((gr = getgrnam(args->arg[1])) == NULL) {
			cddbd_log(LOG_ERR,
			    "Unknown group \"%s\", line %d in %s.",
			    args->arg[1], line, accessfile);
			quit(QUIT_ERR);
		}

		*(uid_t *)at->at_addr = (int)gr->gr_gid;
		endgrent();

		break;

	case AC_MODE:
		if(args->nargs != 2)
			cddbd_access_err(line);

		if(sscanf(args->arg[1], "%o", &n) != 1)
			cddbd_access_err(line);

		/* Ensure the value is legal. */
		if((n & 0777) != n) {
			cddbd_log(LOG_ERR,
			    "Illegal mode for %s (%o), line %d in %s.",
			    at->at_fname, n, line, accessfile);
			quit(QUIT_ERR);
		}

		*(int *)at->at_addr = n;

		break;

	default:
		cddbd_log(LOG_ERR, "Internal error: bad access type: %d.",
		    acctab[i].at_type);

		quit(QUIT_ERR);
	}

	return 1;
}


void
cddbd_access_err(int line)
{
	cddbd_log(LOG_ERR, "Syntax error, line %d in %s.", line, accessfile);
	quit(QUIT_ERR);
}


void
cddbd_db_init(void)
{
	int cnt;
#ifdef DB_WINDOWS_FORMAT
	int i;
	int j;
#endif
	DIR *dirp;
	char *p;
	char buf[CDDBBUFSIZ];
	struct stat sbuf;
	struct dirent *dp;
	static int initted = 0;

	if(initted)
		return;

	initted++;

	if((dirp = opendir(cddbdir)) == NULL) {
		cddbd_log(LOG_ERR, "Can't open database directory: %s (%d).",
		    cddbdir, errno);
		quit(QUIT_ERR);
	}

	cnt = 0;
	while((dp = readdir(dirp)) != NULL && cnt < CDDBMAXDBDIR) {
		/* Categories must not be of illegal length. */
		if((int)strlen(dp->d_name) > CDDBCATEGSIZ)
			continue;

		cddbd_snprintf(buf, sizeof(buf), "%s/%s", cddbdir, dp->d_name);

		/* If we can't stat, keep going anyway. */
		if(stat(buf, &sbuf))
			continue;

		/* We only care about directories. */
		if(!S_ISDIR(sbuf.st_mode))
			continue;

		/* No hidden, current or parent directories. */
		if(dp->d_name[0] == '.')
			continue;

		p = (char *)malloc(strlen(dp->d_name) + 1);
		if(p == NULL) {
			cddbd_log(LOG_ERR, "Malloc failed.");
			quit(QUIT_ERR);
		}

		strcpy(p, dp->d_name);
		categlist[cnt] = p;

		cnt++;
	}

	if(cnt == 0)
		cddbd_log(LOG_ERR, "Database is empty.");

	categlist[cnt] = 0;
	categcnt = cnt;

	closedir(dirp);

#ifdef DB_WINDOWS_FORMAT
	/* Initialize category-specific locks */
	categlocks = calloc(sizeof(clck_t **), categcnt);

	for(i = 0; i < categcnt; i++) {
		categlocks[i] = calloc(sizeof(clck_t *), CATLK_NUMLOCKS);
		for(j = 0; j < CATLK_NUMLOCKS; j++) {
			cddbd_snprintf(buf, sizeof(buf), "%s_%s", categlock_name[j], categlist[i]);
			categlocks[i][j] = cddbd_lock_alloc(buf);
		}
	}
#endif
}


void
cddbd_init(void)
{
	int i;
	struct passwd *pw;

	/* Initialize locks. */
	for(i = 0; i < LK_NUMLOCKS; i++)
		locks[i] = cddbd_lock_alloc(lock_name[i]);

	/* Initialize some global variables. */
	trklen = strlen(trkstr);
	hdrlen = strlen(hdrstr);
	sublen = strlen(substr);
	prclen = strlen(prcstr);

	curpid = getpid();

	pw = getpwuid(euid);
	if(pw == 0) {
		strcpy(admin_email, "postmaster");
		strcpy(user, "cddbd");
	}
	else {
		strcpy(admin_email, pw->pw_name);
		strcpy(user, pw->pw_name);
	}
	endpwent();

	if(gethostname(host, sizeof(host)) < 0)
		strcpy(host, "unknown");

	strcpy(rhost, "unknown");

	for(i = MIN_PROTO_LEVEL; i < MAX_PROTO_LEVEL; i++)
		proto_flags[i + 1] |= proto_flags[i];

	hperm.hp_host = LHOST;
	hperm.hp_connect = HP_CONNECT_OK;
	hperm.hp_passwd = HP_PASSWD_OK;
	hperm.hp_pwdlbl[0] = '\0';

	/* If IF_NONE, we're doing DB ops. Allow anything. */
	if((interface == IF_NONE) && secure) {
		hperm.hp_get = HP_GET_OK;
		hperm.hp_put = HP_PUT_OK;
		hperm.hp_write = HP_WRITE_OK;
		hperm.hp_update = HP_UPDATE_OK;
	}
	else {
		hperm.hp_get = HP_GET_NO;
		hperm.hp_put = HP_PUT_NO;
		hperm.hp_write = HP_WRITE_NO;
		hperm.hp_update = HP_UPDATE_NO;
	}

	/* Make server filenames temporarily. */
	cddbd_mk_servfiles();
}


void
cddbd_input_to(void)
{
	printf("530 %s Inactivity timeout after %d seconds, ",
	    apphost, (int)timers[INPUT_TIMER].seconds);
	printf("closing connection.\r\n");

	cddbd_log(LOG_INFO, "Input timeout.");

	_quit(QUIT_ERR, 0);
}


void
cddbd_access_to(void)
{
	printf("530 %s Database access timeout after %d seconds, ",
	    apphost, (int)timers[ACCESS_TIMER].seconds);
	printf("closing connection.\r\n");

	cddbd_log(LOG_INFO, "Access timeout.");

	_quit(QUIT_ERR, 0);
}


void
cddbd_connect_to(void)
{
	printf("530 %s Server expiration timeout after %d seconds, ",
	    apphost, (int)timers[CONNECT_TIMER].seconds);
	printf("closing connection.\r\n");

	cddbd_log(LOG_INFO, "Connect timeout.");

	_quit(QUIT_ERR, 0);
}


void
cddbd_timer_init(ctimer_t *timers)
{
	ctimer_t *tp;

	/* Reset all timers. */
	for(tp = timers; tp->func != 0; ++tp)
		if(tp->seconds > 0 && !(WRITE_OK(hperm) &&
		    (tp->flags & CT_WRITE_DIS)))
			tp->left = tp->seconds;
		else
			tp->left = -1;

	/* Start clock. */
	lastsecs = time(0);
}


int
cddbd_timer_sleep(ctimer_t *timers)
{
	int n;
	long secs;
	ctimer_t *tp;
	fd_set readfds;
	struct timeval timeout;
	struct timeval *timeoutp;

	/* If we accessed the database, reset timers that care. */
	if(db_access) {
		for(tp = timers; tp->func != 0; ++tp) {
			if((tp->flags & CT_ACCESS_RST) && (tp->left > 0))
				tp->left = tp->seconds;
		}

		db_access = 0;
	}

	/* Length of next timeout is minimum of all timers. */
	timeout.tv_sec = -1;
	timeout.tv_usec = 0;

	for(tp = timers; tp->func != 0; ++tp) {
		if(tp->left >= 0 &&
		    ((tp->left < timeout.tv_sec) || (timeout.tv_sec < 0)))
			timeout.tv_sec = tp->left;
	}

	/* If there are no active timeouts, block until keyboard input. */
	if(timeout.tv_sec < 0)
		timeoutp = 0;
	else
		timeoutp = &timeout;

	/* Do the select. */
	FD_ZERO(&readfds);
	FD_SET(0, &readfds);

	errno = 0;

	n = select(1, &readfds, (fd_set *)NULL, (fd_set *)NULL, timeoutp);

	/* "Interrupted system call" isn't a real error. */
	if(n < 0 && errno != EINTR)
		quit(QUIT_OK);

	/* Calculate the number of seconds since the last loop. */
	secs = time(0) - lastsecs;
	if(secs < 0)
		secs = 0;

	/* Subtract time from timers that have time remaining. */
	for(tp = timers; tp->func != 0; ++tp) {
		if(tp->left > 0) {
			tp->left -= secs;

			if(tp->left < 0)
				tp->left = 0;
		}
	}

	/* Update lastsecs */
	lastsecs += secs;

	/* If we have input, reset timers that care. */
	if(n > 0) {
		for(tp = timers; tp->func != 0; ++tp) {
			if((tp->flags & CT_INPUT_RST) && tp->left >= 0)
				tp->left = tp->seconds;
		}
	}

	/* Process timers that have expired. */
	for(tp = timers; tp->func != 0; ++tp) {
		if(tp->left == 0) {
			(*tp->func)();
			tp->left = tp->seconds;
		}
	}

	/* Did we find input? */
	return (n > 0);
}


void
do_get(arg_t *args)
{
	FILE *fp;
	char *file;
	char path[CDDBBUFSIZ];
	struct stat sbuf;

	if(args->nargs != 2) {
		printf("500 Command syntax error.\r\n");
		return;
	}

	if(!GET_OK(hperm)) {
		printf("401 Permission denied.\r\n");
		return;
	}

	file = args->arg[++args->nextarg];

	/* Don't allow access of parent directory. */
	if(strrchr(file, '.') != NULL) {
		printf("401 Permission denied.\r\n");
		return;
	}

	/* Check for workdir. */
	if(stat(workdir, &sbuf)) {
		cddbd_log(LOG_ERR, "Work directory does not exist: %s.",
		    workdir);
		printf("402 File access failed.\r\n");
		return;
	}

	if(!S_ISDIR(sbuf.st_mode)) {
		cddbd_log(LOG_ERR, "%s is not a directory.", workdir);
		printf("402 File access failed.\r\n");
		return;
	}

	cddbd_snprintf(path, sizeof(path), "%s/%s", workdir, file);

	(void)cddbd_lock(locks[LK_PUT], 1);

	if(stat(path, &sbuf)) {
		printf("402 File not found.\r\n");
		cddbd_unlock(locks[LK_PUT]);

		return;
	}

	if(!(sbuf.st_mode & S_IRUSR)) {
		printf("401 Permission denied.\r\n");
		cddbd_unlock(locks[LK_PUT]);

		return;
	}

	if(S_ISREG(sbuf.st_mode)) {
		if((fp = fopen(path, "r")) == NULL) {
			cddbd_log(LOG_INFO, "Can't open %s for reading.", path);
			printf("402 File access failed.\r\n");
			return;
		}

		cddbd_log(LOG_INFO | LOG_GET, "File downloaded from %s: %s.",
		    rhost, path);

		printf("210 OK, %s follows (until terminating `.')\r\n", file);

		fp_copy(fp, stdout);
		fclose(fp);

		printf(".\r\n");

	}
	else {
		printf("402 Not a regular file.\r\n");
	}

	cddbd_unlock(locks[LK_PUT]);
}


void
do_put(arg_t *args)
{
	int i;
	int cnt;
	FILE *fp;
	char *p;
	char *file;
	char *tpath;
	char buf[CDDBBUFSIZ];
	char path[CDDBBUFSIZ];
	struct stat sbuf;

	if(args->nargs != 2) {
		printf("500 Command syntax error.\r\n");
		return;
	}

	if(!PUT_OK(hperm)) {
		printf("401 Permission denied.\r\n");
		return;
	}

	file = args->arg[++args->nextarg];

	/* Check for workdir. */
	if(stat(workdir, &sbuf)) {
		cddbd_log(LOG_ERR, "Work directory does not exist: %s.",
		    workdir);
		printf("402 File access failed.\r\n");
		return;
	}

	if(!S_ISDIR(sbuf.st_mode)) {
		cddbd_log(LOG_ERR, "%s is not a directory.", workdir);
		printf("402 File access failed.\r\n");
		return;
	}

	cddbd_snprintf(path, sizeof(path), "%s/%s", workdir, file);
	tpath = cddbd_mktemp();

	for(i = 0; put_list[i] != 0; i++)
		if(strlen(put_list[i]) != 0 && !strcmp(put_list[i], path))
			break;

	if(put_list[i] == 0 || has_parent_dir(path)) {
		cddbd_log(LOG_INFO, "Attempt to write disallowed file %s.",
		    path);
		printf("401 Permission denied.\r\n");
		return;
	}

	if(stat(path, &sbuf) == 0) {
		if(!(sbuf.st_mode & S_IFREG)) {
			cddbd_log(LOG_INFO, "Attempt to write non-file %s.",
			    path);
			printf("402 Not a regular file.\r\n");
			return;
		}

		if(!(sbuf.st_mode & S_IWUSR)) {
			cddbd_log(LOG_INFO,
			    "Attempt to write protected file %s.", path);
			printf("401 Permission denied.\r\n");
			return;
		}
	}

	printf("320 OK, input file data (terminate with `.')\r\n");
	fflush(stdout);

	if((fp = fopen(tpath, "w")) == NULL) {
		cddbd_log(LOG_INFO, "Can't open %s for writing.", tpath);
		printf("402 File access failed.\r\n");
		cddbd_freetemp(tpath);

		return;
	}

	for(i = 0, cnt = 0; i < max_lines; i++) {
		/* Unexpected disconnect. */
		if(cddbd_gets(buf, sizeof(buf)) == NULL)
			quit(QUIT_ERR);

		/* Done inputting. */
		if(is_dot(buf))
			break;

		p = buf;

		if(is_dbl_dot(p))
			p++;

		strip_crlf(p);

		cnt += strlen(p);
		if(cnt >= put_size) {
			cddbd_log(LOG_INFO, "File put too large: %s.", path);
			printf("501 Input too long.\r\n");

			fclose(fp);
			cddbd_freetemp(tpath);

			return;
		}

		/* Problem writing file. */
		if(fputs(p, fp) == EOF || fputc('\n', fp) == EOF) {
			cddbd_log(LOG_INFO, "Can't write %s.", tpath);
			printf("402 File access failed.\r\n");

			fclose(fp);
			cddbd_freetemp(tpath);

			return;
		}
	}

	fclose(fp);

	if(i >= max_lines) {
		cddbd_log(LOG_INFO, "File put too large: %s.", path);
		printf("501 Input too long.\r\n");
		cddbd_freetemp(tpath);

		return;
	}

	(void)cddbd_lock(locks[LK_PUT], 1);

	if(unlink(path) != 0 && errno != ENOENT) {
		cddbd_log(LOG_ERR, "Can't unlink %s (%d).", path, errno);
		printf("402 File access failed.\r\n");

		cddbd_freetemp(tpath);
		cddbd_unlock(locks[LK_PUT]);

		return;
	}

	if(cddbd_link(tpath, path) != 0) {
		cddbd_log(LOG_ERR, "Can't link %s to %s (%d).",
		    tpath, path, errno);

		printf("402 File access failed.\r\n");
	}
	else
		printf("200 Put successful.\r\n");

	cddbd_freetemp(tpath);
	cddbd_unlock(locks[LK_PUT]);
}


void
do_help(arg_t *args)
{
	int i;
	int sub;
	char **hlp;

	args->nextarg++;
	sub = 0;

	if(args->nargs == 1) {
		hlp = 0;
	}
	else {
		for(i = 0; cmd[i].cmd; i++) {
			if(!cddbd_strcasecmp(args->arg[args->nextarg],
			    cmd[i].cmd))
				break;
		}

		if(cmd[i].cmd == 0) {
			printf("500 Unknown HELP subcommand.\r\n");
			return;
		}

		if((args->nargs > 3 && (cmd[i].flags & CF_SUBCMD)) ||
		    (args->nargs > 2 && !(cmd[i].flags & CF_SUBCMD))) {
			printf("500 Too many args to HELP command.\r\n");
			return;
		}

		hlp = cmd[i].help;

		if(cmd[i].flags & CF_SUBCMD && (args->nargs != 3))
			sub = 1;

		if(cmd[i].flags & CF_SUBCMD && (args->nargs == 3)) {
			args->nextarg++;

			for(i = 0; cddb_cmd[i].cmd; i++) {
				if(!cddbd_strcasecmp(args->arg[args->nextarg],
				    cddb_cmd[i].cmd))
					break;
			}

			if(cddb_cmd[i].cmd == 0) {
				printf("500 Unknown CDDB HELP subcommand.\r\n");
				return;
			}

			hlp = cddb_cmd[i].help;
		}
	}

	printf("210 OK, help information follows (until terminating `.')\r\n");

	if(hlp == 0) {
		printf("The following commands are supported:\r\n\r\n");

		for(i = 0; help[i]; i++)
			printf("%s\r\n", help[i][0]);

		for(i = 0; help_info[i]; i++)
			printf("%s\r\n", help_info[i]);
	}
	else {
		for(i = 0; hlp[i]; i++)
			printf("%s\r\n", hlp[i]);

		if(sub)
			for(i = 0; chelp[i]; i++)
				printf("%s\r\n", chelp[i][0]);
	}

	printf(".\r\n");
}


void
do_cddb_lscat(arg_t *args)
{
	int i;

	if(args->nargs != 2) {
		printf("500 Command syntax error.\r\n");
		return;
	}

	printf("210 OK, category list follows (until terminating `.')\r\n");

	for(i = 0; categlist[i] != 0; i++)
		printf("%s\r\n", categlist[i]);
	printf(".\r\n");
}


void
do_cddb_unlink(arg_t *args)
{
	char *category;
	char file[CDDBBUFSIZ];
	unsigned int discid;

#ifdef DB_WINDOWS_FORMAT
	printf("502 Unimplemented.\r\n");
	return;
#endif

	category = args->arg[++args->nextarg];

	if(args->nargs != 4 ||
	    sscanf(args->arg[++args->nextarg], "%08x", &discid) != 1) {
		printf("500 Command syntax error.\r\n");
		return;
	}

	if(categ_index(category) < 0) {
		printf("501 Invalid category: %s\r\n", category);
		return;
	}

	if(!WRITE_OK(hperm)) {
		printf("401 Permission denied.\r\n");
		return;
	}

	cddbd_snprintf(file, sizeof(file), "%s/%s/%08x", cddbdir, category,
	    discid);

	if (unlink(file) == 0) {
		cddbd_log(LOG_INFO, "Unlink: %s has been deleted.", file);
		printf("200 OK, file has been deleted.\r\n");
	}
	else {
			cddbd_log(LOG_ERR, "Unlink: Failed to delete %s.", file);
			printf("402 File access failed.\r\n");
	}
}


/********************************************************************
DISCID <ntrks> <off_1> <off_2> <...> <off_n> <nsecs>
    Calculate a CDDB disc ID for a given set of CD TOC information.
    Arguments are:
        ntrks:  total number of tracks on CD.
        off_X:  frame offset of track X.
        nsecs:  total playing length of the CD in seconds.
********************************************************************/

void
do_discid(arg_t *args)
{
	int i;
	int ntrks;
	int nsecs;
	int offtab[CDDBMAXTRK];
	unsigned int n,k;

	if(args->nargs < 3 ||
	    sscanf(args->arg[args->nextarg + 1], "%d", &ntrks) != 1 ||
	    args->nargs != (ntrks + 3) ||
	    sscanf(args->arg[args->nextarg + 2 + ntrks], "%d", &nsecs) != 1 ||
	    ntrks < 0 ||
	    nsecs < 0)  {
		printf("500 Command syntax error.\r\n");
		return;
	}

	for(i = 0; i < ntrks; i++) {
		if(sscanf(args->arg[args->nextarg + i + 2], "%d",
		    &offtab[i]) != 1 || offtab[i] < 0) {
			printf("500 Command syntax error.\r\n");
			return;
		}
		if((i != 0) && (offtab[i] < offtab[i-1])) {
			printf("500 Command syntax error.\r\n");
			return;
		}
	}

	if(nsecs < offtab[ntrks-1]/75) {
		printf("500 Command syntax error.\r\n");
		return;
	}

	n = 0;
	for(i=0;i<ntrks;i++) {
		k=offtab[i]/75;
		while (k > 0) {
			n += (k % 10);
			k /= 10;
		}
	}

	printf("200 Disc ID is %08x\r\n",
	       (n % 0xff) << 24 | (nsecs-offtab[0]/75) << 8 | ntrks);
}

void
do_cddb_query(arg_t *args)
{
	int i;
	FILE *fp;
	db_t *db;
	int ntrks;
	int nsecs;
	int collision;
	int offtab[CDDBMAXTRK];
	unsigned int discid;
	static unsigned int ldiscid = DISCID_EMPTY;
	char buf[CDDBBUFSIZ];
	char errstr[CDDBBUFSIZ];

	int numfound;
	lhead_t *lh;
	link_t *lp;
	fmatch_t *fm;

	if(args->nargs < 6 ||
	    sscanf(args->arg[args->nextarg + 2], "%d", &ntrks) != 1 ||
	    args->nargs != (ntrks + 5) ||
	    sscanf(args->arg[args->nextarg + 1], "%x", &discid) != 1 ||
	    sscanf(args->arg[args->nextarg + 3 + ntrks], "%d", &nsecs) != 1 ||
	    ntrks > CDDBMAXTRK || /* who knows who would try that */
	    ntrks < 0) {
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

	/* Check for berzerk clients.                                      */
	/* There is a small problem with this: If someone happens to query */
	/* two discs with coincidentally the same discid back-to-back,     */
	/* the server thinks it sees a berzerk client and closes the       */
	/* connection. The chance of this happening should be negligibly   */
	/* small, however.                                                 */
	if(ck_berzerk && discid == ldiscid) {
		printf("502 Already performed a query for disc ID: "
		    "%08x\r\n", discid);
		quit(QUIT_OK);
	}
	
	ldiscid = discid;
	
	/* create list */
	if((lh = list_init(0, 0, fmatch_free, 0)) == 0) {
		cddbd_log(LOG_ERR | LOG_QUERY, "Can't malloc match list.");
		quit(QUIT_ERR);
	}
	numfound = 0;

	collision = 0;

	for(i = 0; categlist[i] != 0; i++) {
		cddbd_snprintf(buf, sizeof(buf), "%s/%s/%08x",
		    cddbdir, categlist[i], discid);

		/* No such disc ID in this category. */
#ifdef DB_WINDOWS_FORMAT
		if((fp = db_prepare_unix_read(buf)) == NULL)
#else
		if((fp = fopen(buf, "r")) == NULL)
#endif
			continue;

		/* Check entry. */
		if((db = db_read(fp, errstr, file_df_flags)) == 0) {
			cddbd_log(LOG_ERR, "Failed to read DB entry %s: %s",
			    buf, errstr);
			fclose(fp);
			continue;
		}

		fclose(fp);

		/* They don't match. */
		if(ntrks != db->db_trks || !is_fuzzy_match(offtab,
		    db->db_offset, nsecs, db->db_disclen, ntrks)) {
			collision++;
			db_free(db);
			continue;
		}

		/* Convert charset. */
		convert_db_charset(db);

		/* We found a match. */
		if((fm = (fmatch_t *)malloc(sizeof(fmatch_t))) == 0) {
			cddbd_log(LOG_ERR | LOG_QUERY, "Can't allocate memory for match list.");
			quit(QUIT_ERR);
		}

		/* Copy the data over. */
		fm->fm_catind = i;

		db_strcpy(db, DP_DTITLE, 0, buf, sizeof(buf));
		db_free(db);
		if((fm->fm_dtitle = strdup(buf)) == 0) {
			cddbd_log(LOG_ERR | LOG_QUERY, "Can't allocate memory for match dtitle.");
			quit(QUIT_ERR);
		}

		if(list_add_cur(lh, fm) == 0) {
			cddbd_log(LOG_ERR | LOG_QUERY, "Can't add to match list.");
			quit(QUIT_ERR);
		}
		numfound++;
	}

	if(PROTO_ENAB(P_QUERY) && (numfound > 1)) {
		printf("210 Found exact matches, list follows (until terminating `.')\r\n");
		for(list_rewind(lh), list_forw(lh); !list_rewound(lh);
		    list_forw(lh)) {
			lp = list_cur(lh);
			fm = (fmatch_t *)lp->l_data;
			printf("%s %08x %s\r\n", categlist[fm->fm_catind],
			    discid, fm->fm_dtitle);
		}
		printf(".\r\n");
		cddbd_log(LOG_ACCESS | LOG_QUERY,
		    "Query: %08x found %d exact matches",
		    discid, lh->lh_count);
		list_free(lh);
		return;
	}

	if(numfound){
		list_rewind(lh);
		list_forw(lh);
		lp = list_cur(lh);
		fm = (fmatch_t *)lp->l_data;
		printf("200 %s %08x %s\r\n", categlist[fm->fm_catind], discid, fm->fm_dtitle);
		cddbd_log(LOG_ACCESS | LOG_QUERY,
		    "Query: successful %s %08x", categlist[fm->fm_catind], discid);
		list_free(lh);
		return;
	}
	
	list_free(lh);

	/* We found at least one non-matching entry with our disc ID. */
	if(collision)
		cddbd_log(LOG_INFO | LOG_COLLIS,
		    "Discid collision on DB entry: %08x", discid);

	/* Find a close match if possible. */
	do_cddb_query_fuzzy(args);
}


void
do_cddb_read(arg_t *args)
{
	FILE *fp;
	db_t *db;
	char *category;
	static char lcategory[CDDBBUFSIZ];
	static int needinit = 1;
	char file[CDDBBUFSIZ];
	char buf[CDDBBUFSIZ];
	unsigned int discid;
	static unsigned int ldiscid = DISCID_EMPTY;

	
	if(needinit) {
		lcategory[0]='\0';
		needinit=0;
	}

	category = args->arg[++args->nextarg];

	if(args->nargs != 4 ||
	    sscanf(args->arg[++args->nextarg], "%08x", &discid) != 1) {
		printf("500 Command syntax error.\r\n");
		return;
	}

	if(categ_index(category) < 0) {
		printf("501 Invalid category: %s\r\n", category);
		return;
	}

	/* Check for berzerk clients. Only let them read it once. */
	if(ck_berzerk && discid == ldiscid && !strcmp(category, lcategory)) {
		printf("502 Already read %s %08x\r\n", category, discid);
		quit(QUIT_OK);
	}

	ldiscid = discid;
	strcpy(lcategory, category);

	cddbd_snprintf(file, sizeof(file), "%s/%s/%08x", cddbdir, category,
	    discid);

#ifdef DB_WINDOWS_FORMAT
	if((fp = db_prepare_unix_read(file)) == NULL) {
#else
	if((fp = fopen(file, "r")) == NULL) {
#endif
		printf("401 %s %08x No such CD entry in database.\r\n",
		    category, discid);
		cddbd_log(LOG_INFO, "Read: %s %08x failed", category, discid);

		return;
	}

	if((db = db_read(fp, buf, file_df_flags)) == 0) {
		printf("403 %s %08x CD database entry corrupt.\r\n",
		    category, discid);

		cddbd_log(LOG_ERR, "Failed to read DB entry %s: %s",
		    file, buf);

		fclose(fp);
		return;
	}

	fclose(fp);

	/* Convert charset. */
	convert_db_charset(db);

	/* Remove extended data if required. */
	if(strip_ext && (db->db_flags & DB_STRIP)) {
		if(!db_strip(db)) {
			printf("402 Server error while reading database.\r\n");
			cddbd_log(LOG_ERR, "Memory error during read.");

			db_free(db);
			return;
		}
	}

	printf("210 %s %08x CD database entry follows ", category, discid);
	printf("(until terminating `.')\r\n");

#ifdef DB_WINDOWS_FORMAT
	db_write(stdout, db, level, 0);
#else
	db_write(stdout, db, level);
#endif
	fflush(stdout);

	printf(".\r\n");

	db_strcpy(db, DP_DTITLE, 0, buf, sizeof(buf));
	db_free(db);

	cddbd_log(LOG_ACCESS | LOG_READ, readstr, category, discid, cksum, buf);
}


/** 
 * Handles the "write" command.
 *
 * Reads a record from the stdin and writes it into the post
 * directory (uses #db_post).
 * 
 * @param args input arguments. Contains cateory and discid.
 */
void do_cddb_write(arg_t *args)
{
	db_t *db;
	char *category;
	char pdir[CDDBBUFSIZ];
	char buf[CDDBBUFSIZ];
	unsigned int discid;
	struct stat sbuf;
	int flags;

	category = args->arg[++args->nextarg];

	if(args->nargs != 4 ||
	    sscanf(args->arg[++args->nextarg], "%08x", &discid) != 1) {
		printf("500 Command syntax error.\r\n");
		return;
	}

	if(categ_index(category) < 0) {
		printf("501 Invalid category: %s\r\n", category);
		return;
	}

	if(!WRITE_OK(hperm)) {
		printf("401 Permission denied.\r\n");
		return;
	}

	/* Check for postdir, and create if it doesn't exist. */
	if(stat(postdir, &sbuf)) {
		if(mkdir(postdir, (mode_t)db_dir_mode)) {
			cddbd_log(LOG_ERR, "Failed to create post dir %s.",
			    postdir);
			printf("402 File access failed.\r\n");
			return;
		}

		(void)cddbd_fix_file(postdir, db_dir_mode, db_uid, db_gid);
	}
	else if(!S_ISDIR(sbuf.st_mode)) {
		cddbd_log(LOG_ERR, "%s is not a directory.", postdir);
		printf("402 File access failed.\r\n");
		return;
	}

	if(categ_index(category) < 0) {
		printf("501 Invalid category: %s.\r\n", category);
		return;
	}

	printf("320 OK, input CDDB data (terminate with `.')\r\n");
	fflush(stdout);

	/* Specify acceptable charsets. */
	if(!PROTO_ENAB(P_UTF8)) {
		if(utf_as_iso == UAI_CONVERT)
			flags = DF_ENC_LATIN1 | DF_ENC_UTF8;
		else
			flags = DF_ENC_LATIN1;
	}
	else
		flags = DF_ENC_UTF8;

	if((db = db_read(stdin, buf,
			 flags | (DF_STDIN | DF_SUBMITTER | DF_CK_SUBMIT)))
	    == 0) {
		if(db_errno == DE_INVALID)
			printf("501 Entry rejected: %s.\r\n", buf);
		else {
			printf("402 Internal server error: %s.\r\n",
			    db_errmsg[db_errno]);
			cddbd_log(LOG_ERR, "Failed to write DB entry: %s.",
			    db_errmsg[db_errno]);
		}

		return;
	}

	/* Check and disambiguate charset. */
	if (db_disam_charset(db)) {
		printf("403 Entry rejected: looks like UTF-8.\r\n");
		db_free(db);
		return;
	}

	cddbd_snprintf(pdir, sizeof(pdir), "%s/%s", postdir, category);

	(void)db_post(db, pdir, discid, buf);

	switch(db_errno) {
	case DE_NO_ERROR:
		db_strcpy(db, DP_DTITLE, 0, buf, sizeof(buf));
		cddbd_log(LOG_INFO | LOG_WRITE,
		    "Write: (via CDDBP - %s) %s %08x %s",
		    rhost, category, discid, buf);

		printf("200 CDDB entry accepted.\r\n");
		break;

	case DE_INVALID:
		cddbd_log(LOG_ERR, "Failed to write DB entry: %s.", buf);
		printf("501 Entry rejected: %s.\r\n", buf);
		break;

	default:
		cddbd_log(LOG_ERR, "Failed to write DB entry: %s.", buf);
		printf("402 Server error: %s.\r\n", buf);
		break;
	}

	db_free(db);
}



/* ARGSUSED */
void
do_quit(arg_t *args)
{
	quit(QUIT_OK);
}


/* ARGSUSED */
/*
void
do_cddb_srch(arg_t *args)
{
	printf("500 Command unimplemented.\r\n");
}
*/

/* ARGSUSED */
void
do_ver(arg_t *args)
{
	printf("200 ");
	printf(verstr, VERSION, PATCHLEVEL);
	printf("\r\n");
}


void
do_proto(arg_t *args)
{
	int nlevel;

	if(args->nargs > 2) {
		printf("500 Command syntax error.\r\n");
		return;
	}

	if(args->nargs == 1) {
		printf("200 CDDB protocol level: current %d, supported %d\r\n",
		    level, MAX_PROTO_LEVEL);
		return;
	}

	if(sscanf(args->arg[1], "%d", &nlevel) != 1) {
		printf("500 Command syntax error.\r\n");
		return;
	}

	if(nlevel < MIN_PROTO_LEVEL || nlevel > MAX_PROTO_LEVEL) {
		printf("501 Illegal CDDB protocol level.\r\n");
		return;
	}

	if(nlevel == level) {
		printf("502 CDDB protocol level already %d.\r\n", level);
		return;
	}

	level = nlevel;
	printf("201 OK, CDDB protocol level now: %d\r\n", level);
}


void
do_whom(arg_t *args)
{
	if(args->nargs > 1) {
		printf("500 Command syntax error.\r\n");
		return;
	}

	if(cddbd_nus(0) < 0) {
		printf("401 No user information available.\r\n");
		return;
	}
	
	if(!GET_OK(hperm)) {
		printf("401 No user information available.\r\n");
		return;
	}
	
	printf("210 OK, user list follows (until terminating `.')\r\n");
	printf("Pid:   Client:   User:\r\n");

	(void)cddbd_nus(1);

	printf(".\r\n");
}


void
do_validate(arg_t *args)
{
	uint32_t salt;
	char buf[CDDBBUFSIZ];

	if(args->nargs > 1) {
		printf("500 Command syntax error.\r\n");
		return;
	}

	if(PASSWD_OK(hperm)) {
		printf("503 Validation not required.\r\n");
		return;
	}

	salt = cddbd_rand();

	printf("320 OK, input validation string, %s%08X "
	    "(terminate with newline)\r\n", saltstr, salt);
	fflush(stdout);

	/* Unexpected disconnect. */
	if(cddbd_gets(buf, sizeof(buf)) == NULL)
		quit(QUIT_ERR);

	(void)_do_validate(buf, salt);
}


int
_do_validate(char *pwd, uint32_t salt)
{
	ct_key_t *key;

	strip_crlf(pwd);

	if(strlen(pwd) != CDDBXCRCLEN) {
		hperm.hp_passwd = HP_PASSWD_NO;
		printf("501 Incorrect validation string length.\r\n");

		return -1;
	}

	if(!PASSWD_REQ(hperm) || (key = getpasswd(hperm.hp_pwdlbl)) == NULL ||
	    strtocrc(pwd, salt, key) != CT_PASSWD) {
		cddbd_log(LOG_INFO | LOG_PASSWD,
		    "User validation failed from %s.", rhost);

		sleep(1);

		hperm.hp_passwd = HP_PASSWD_NO;
		printf("502 Invalid validation string.\r\n");

		return -1;
	}

	cddbd_log(LOG_INFO | LOG_PASSWD,
	    "User validation successful from %s.", rhost);
	printf("200 Validation successful.\r\n");

	hperm.hp_passwd = HP_PASSWD_OK;
	modify_perms(&hperm);

	/* Restart timers. */
	if(!(iface_map[interface].if_flags & IFL_NOCONN))
		cddbd_timer_init(timers);

	return 0;
}


void
do_update(arg_t *args)
{
	pid_t f;

	if(args->nargs > 1) {
		printf("500 Command syntax error.\r\n");
		return;
	}

	if(!UPDATE_OK(hperm)) {
		printf("401 Permission denied.\r\n");
		return;
	}

	f = cddbd_fork();

	/* The child does the update. */
	if(f == 0) {
		close(0);
		close(1);
		close(2);

		cddbd_update();
		cddbd_build_fuzzy();

		return;
	}

	if(f < 0) {
		printf("402 Unable to update the database.\r\n");
		cddbd_log(LOG_ERR, "Can't fork child for update (%d).", errno);

		return;
	}

	printf("200 Updating the database.\r\n");
}


void
do_stat(arg_t *args)
{
	char *p;
	int i;
	int cnt;
	int *counts;
	int first;
	csite_t *sp;

	if(args->nargs > 1) {
		printf("500 Command syntax error.\r\n");
		return;
	}

	printf("210 OK, status information follows ");
	printf("(until terminating `.')\r\n");

	printf("Server status:\r\n");
	printf("    current proto: %d\r\n", level);
	printf("    max proto: %d\r\n", MAX_PROTO_LEVEL);
	printf("    interface: %s\r\n", iface_map[interface].if_name);

	p = (GET_OK(hperm) ? "yes" : "no");
	printf("    gets: %s\r\n", p);

	p = (PUT_OK(hperm) ? "yes" : "no");
	printf("    puts: %s\r\n", p);

	p = (UPDATE_OK(hperm) ? "yes" : "no");
	printf("    updates: %s\r\n", p);

	p = (WRITE_OK(hperm) ? "yes" : "no");
	printf("    posting: %s\r\n", p);

	switch(hperm.hp_passwd) {
	case HP_PASSWD_OK:
		p = "accepted";
		break;

	case HP_PASSWD_REQ:
		p = "required";
		break;

	case HP_PASSWD_NO:
	default:
		p = "rejected";
		break;
	}

	printf("    validation: %s\r\n", p);

	p = (PROTO_ENAB(P_QUOTE) ? "yes" : "no");
	printf("    quotes: %s\r\n", p);

	p = (strip_ext ? "yes" : "no");
	printf("    strip ext: %s\r\n", p);

	p = (secure ? "yes" : "no");
	printf("    secure: %s\r\n", p);

	if(WRITE_OK(hperm))
		printf("    max lines: %d\r\n", max_lines);

	cnt = cddbd_nus(0);
	if(cnt >= 0) {
		printf("    current users: %d\r\n", cnt);
		printf("    max users: %d\r\n", max_users);
	}

	counts = cddbd_count();
	printf("Database entries: %d\r\n", counts[0]);

	printf("Database entries by category:\r\n");
	for(i = 0; i < categcnt; i++)
		printf("    %s: %d\r\n", categlist[i], counts[i + 1]);

	first = 1;

	if(GET_OK(hperm) || WRITE_OK(hperm)) {
		setsiteent();

		while((sp = getsiteent(SITE_XMIT)) != NULL) {
			if(sp->st_flags & ST_FLAG_NOXMIT)
			continue;

			if(first) {
				first = 0;
				printf("Pending file transmissions:\r\n");
			}

			cnt = cddbd_count_history(sp->st_name);
			if(cnt < 0)
				printf("    %s: -\r\n", sp->st_name);
			else
				printf("    %s: %d\r\n", sp->st_name, cnt);
		}

		endsiteent();
	}
	printf(".\r\n");
}


void
do_sites(arg_t *args)
{
	int cnt;
	csite_t *sp;
	char lat[CDDBBUFSIZ];
	char lng[CDDBBUFSIZ];

	if(args->nargs > 1) {
		printf("500 Command syntax error.\r\n");
		return;
	}

	skip_log++;

	setsiteent();

	for(cnt = 0; (sp = getsiteent(SITE_INFO)) != NULL; cnt++) {
		/* Don't print non-cddbp sites in legacy mode. */
		if(!PROTO_ENAB(P_SITES) && sp->st_proto != SERV_CDDBP)
			continue;

		if(cnt == 0) {
			printf("210 OK, site information follows ");
			printf("(until terminating `.')\r\n");
		}

		copy_coord(lat, &sp->st_lat);
		copy_coord(lng, &sp->st_long);

		/* Do the new sites behavior. */
		if(PROTO_ENAB(P_SITES)) {
			printf("%s %s %d %s %s %s %s\r\n", sp->st_name,
			    sp->st_pname, sp->st_port, sp->st_addr,
			    lat, lng, sp->st_desc);
		}
		else {
			printf("%s %d %s %s %s\r\n", sp->st_name, sp->st_port,
			    lat, lng, sp->st_desc);
		}
	}

	endsiteent();

	if(cnt == 0) {
		printf("401 No site information available.\r\n");
		return;
	}
	else
		printf(".\r\n");

	cddbd_log(LOG_INFO | LOG_SITES, "Site list downloaded from %s.", rhost);
}


void
do_cddb_hello(arg_t *args)
{
	if(args->nargs != 6) {
		printf("500 Command syntax error.\r\n");
		return;
	}

	args->nextarg++;

	(void)_do_cddb_hello(args);
}


int
_do_cddb_hello(arg_t *args)
{
	FILE *fp;
	char *username;
	char *clientname;
	char *hostname;
	char *version;
	char buf[CDDBBUFSIZ];

	if(hello) {
		printf("402 Already shook hands.\r\n");
		return -1;
	}

	username = args->arg[args->nextarg++];
	hostname = args->arg[args->nextarg++];
	clientname = args->arg[args->nextarg++];
	version = args->arg[args->nextarg];

	switch(ck_client_perms(clientname, version, interface)) {
	case CP_DISALLOW:
		/* Client not allowed. */
		printf("433 Unauthorized client: %s %s\r\n", clientname,
		    version);

		cddbd_log(LOG_INFO | LOG_REJECT, rejectstr,
		    iface_map[interface].if_name, username, hostname,
		    rhost, clientname, version);

		_quit(QUIT_OK, 0);

		/* NOTREACHED */

	case CP_HANG:
		/* Client is nasty and must be delayed. */
		cddbd_log(LOG_INFO | LOG_REJECT, rejectstr,
		    iface_map[interface].if_name, username, hostname,
		    rhost, clientname, version);

		cddbd_hang();

		/* NOTREACHED */

	case CP_ALLOW:
		break;

	default:
		cddbd_log(LOG_ERR | LOG_INTERN,
		    "Unknown response from ck_client_perms().");

		break;
	}

	hello++;

	if(!(mode & MFL_ASY)) {
		printf("200 Hello and welcome %s@%s running %s %s.\r\n",
		    username, hostname, clientname, version);
	}

	cddbd_log(LOG_HELLO, hellostr, iface_map[interface].if_name, username,
	    hostname, rhost, clientname, version);

	cddbd_snprintf(buf, sizeof(buf), "%s %s %s", username, hostname, rhost);
	(void)crc32(CRC_STRING, buf, &cksum, 0);

	if(max_users != 0 &&
	    (fp = fopen(lockfile, "w")) != NULL) {
		fprintf(fp, "%s %s %s %s ",
		    username, hostname, clientname, rhost);
		fclose(fp);
	}

	return 0;
}


void
do_motd(arg_t *args)
{
	FILE *fp;
	struct stat sbuf;
	char buf[CDDBBUFSIZ];

	if(args->nargs != 1) {
		printf("500 Command syntax error.\r\n");
		return;
	}

	if(motdfile[0] == '\0' || stat(motdfile, &sbuf) != 0) {
		printf("401 No message of the day available.\r\n");
		return;
	}

	if((fp = fopen(motdfile, "r")) == NULL) {
		cddbd_log(LOG_ERR, "Can't open motd file: %s (%d).",
		    motdfile, errno);
		printf("401 No message of the day available.\r\n");
		return;
	}

	printf("210 Last modified: %s ", make_time2(localtime(&sbuf.st_mtime)));
	printf("MOTD follows (until terminating `.')\r\n");

	while(fgets(buf, sizeof(buf), fp) != NULL) {
		if(is_dbl_dot(buf))
			printf(".");

		strip_crlf(buf);
		printf("%s\r\n", buf);
	}

	printf(".\r\n");

	fclose(fp);
}


void
do_log(arg_t *args)
{
	FILE *fp;
	int i;
	int x;
	int tpid;
	int days;
	int get;
	int d[7];
	int flag;
	int gflag;
	int incl;
	int lines= 0;
	int log_all;
	int other_cnt;
	int nargs;
	int max_log_list;
	uint32_t tcksum;
	char **arg;
	char buf[CDDBBUFSIZ];
	char iface[CDDBBUFSIZ];
	char user[CDDBBUFSIZ];
	char site[CDDBBUFSIZ];
	char cur[CDDBBUFSIZ];
	char end[CDDBBUFSIZ];
	char start[CDDBBUFSIZ];
	char first[CDDBBUFSIZ];
	char last[CDDBBUFSIZ];
	char tmp_client[CDDBBUFSIZ];
	char dtitle[CDDBBUFSIZ];
	char scksum[CDDBCRCLEN + 1];
	struct stat sbuf;
	lhead_t *list[LH_NHEAD];
	logdata_t *ld;
	logdata_t **logdata[LH_NHEAD];
	time_t t;


	days = 0;
	get = 0;
	nargs = args->nargs - 1;
	arg = &args->arg[1];

	max_log_list = MAX_LOG_LIST;
	
	if(!GET_OK(hperm)) {
		printf("401 Permission denied.\r\n");
		return;
	}
	
	if(nargs > 0 && !cddbd_strcasecmp(arg[0], "get")) {

		get = 1;
		gflag = LOG_ALL;

		nargs--;
		arg++;

		if(nargs > 1 && !cddbd_strcasecmp(arg[0], "-f")) {
			for(i = 0; log[i].name != 0; i++)
				if(!cddbd_strcasecmp(arg[1], log[i].name))
					break;

			if(log[i].name == 0) {
				if(sscanf(arg[1], "%x", &gflag) != 1) {
					printf("500 Bad log flag.\r\n");
					return;
				}
			}
			else
				gflag = log[i].flag;

			if(gflag == 0) {
				printf("500 Bad log flag.\r\n");
				return;
			}

			nargs -= 2;
			arg += 2;
		}
	}

	if(nargs > 1 && !cddbd_strcasecmp(arg[0], "-l")) {
		max_log_list = atoi(arg[1]);

		if(max_log_list < 0) {
			printf("500 Bad log list count: %d.\r\n", max_log_list);
			return;
		}

		nargs -= 2;
		arg += 2;
	}

	if(nargs > 0 && !cddbd_strcasecmp(arg[0], "day")) {
		nargs--;
		arg++;

		if(nargs > 0) {
			days = atoi(arg[0]);
			if(days <= 0) {
				printf("500 Bad day count: %d.\r\n",
				    days);
				return;
			}

			nargs--;
			arg++;
		}
		else
			days = 1;

		t = time(0);
		cvt_time(t - (days * SECS_PER_DAY), start);
		cvt_time(t, end);
	}

	if(nargs > 2 || (days && nargs > 0)) {
		printf("500 Command syntax error.\r\n");
		return;
	}

	/* Lock the log. */
	(void)cddbd_lock(locks[LK_LOG], 1);

	if(log_flags == 0 || (fp = fopen(servfile[SF_LOG], "r")) == NULL) {
		cddbd_unlock(locks[LK_LOG]);
		printf("402 No log information available.\r\n");
		return;
	}

	if(stat(servfile[SF_LOG], &sbuf))
		sbuf.st_size = 0;

	/* Unlock the log. It's safe now, since we have a file descriptor. */
	cddbd_unlock(locks[LK_LOG]);

	log_all = 0;

	if(days == 0) {
		if(nargs == 2) {
			if(!cvt_date(arg[1], end)) {
				printf("501 Invalid end date.\r\n");
				return;
			}
		}
		else
			(void)cvt_date("", end);

		if(nargs >= 1) {
			if(!cvt_date(arg[0], start)) {
				printf("501 Invalid start date.\r\n");
				return;
			}
		}
		else
			log_all = 1;
	}

	/* End is earlier than start. */
	if(!log_all && strcmp(start, end) > 0) {
		printf("501 End date is earlier than start date.\r\n");
		return;
	}

	if(log_all) {
		first[0] = '\0';
		last[0] = '\0';
	}

	if(get) {
		skip_log++;
		cddbd_log(LOG_INFO | LOG_GET, "Log downloaded from %s.", rhost);
		printf("211 OK, log follows (until terminating `.')\r\n");
		fp_copy_init();
	}
	else {
		for(i = 0; log[i].name; i++)
			log[i].cnt = 0;

		for(i = 0; i < LH_NHEAD; i++) {
			if((list[i] = list_init(0, comp_log, free_log, 0)) == 0) {
				cddbd_log(LOG_ERR, "Can't allocate list head.\n");
				quit(QUIT_ERR);
			}

			logdata[i] = 0;
		}

		lines = 0;
	}

	incl = 0;

	while(fgets(buf, sizeof(buf), fp)) {
		x = sscanf(buf, "%d/%d/%d%d:%d:%d [%d,%x]", &d[0], &d[1],
		    &d[2], &d[3], &d[4], &d[5], &tpid, &flag);

		/* Bogus log string. */
		if(x != 8) {
			if(get && incl)
				fp_copy_buf(stdout, buf);
			continue;
		}

		if(get && !(flag & gflag)) {
			incl = 0;
			continue;
		}

		/* Safe to use sprintf. Need speed. */
		sprintf(cur, "%04d%04d%04d%04d%04d%04d",
		    make_year(d[2]), d[0], d[1], d[3], d[4], d[5]);

		/* Bogus log date. */
		if(date_to_tm(cur) == 0) {
			incl = 0;
			continue;
		}

		/* Note the first and last message. */
		if(strcmp(first, cur) > 0 || first[0] == '\0')
			strcpy(first, cur);

		if(strcmp(last, cur) < 0 || last[0] == '\0')
			strcpy(last, cur);

		if(log_all || (strcmp(cur, start) >= 0 &&
		    strcmp(cur, end) <= 0))
			incl = 1;
		else
			incl = 0;

		if(get) {
			if(incl)
				fp_copy_buf(stdout, buf);
			continue;
		}

		if(incl)
			lines++;

		/* Log the message if it's acceptable. */
		for(i = 0; log[i].name; i++) {
			if((log[i].flag & flag) && incl)
				log[i].cnt++;
		}

		if(!incl)
			continue;

		/* Log which discs have been downloaded. */
		if(flag & LOG_READ) {
			if(sscanf(buf, readsstr, &tcksum, dtitle) == 2) {
				sprintf(scksum, "%08x", tcksum);
				count_log(list[LH_READ], dtitle, scksum);
			}
		}

		/* Count clients. */
		if(flag & LOG_HELLO) {
			x = sscanf(buf, hellosstr, iface, user, site,
			    tmp_client);

			if(x != 4)
				strcpy(tmp_client, "Unknown");

			count_log(list[LH_CLNT_CONN], tmp_client, 0);

			if(x < 1) {
				strcpy(iface, "Unknown");
			}
			else {
				for(i = 0; i < IF_NIFACE; i++) {
					if(iface_map[i].if_flags & IFL_NOCOUNT)
						continue;

					if(!cddbd_strcasecmp(
					    iface_map[i].if_name, iface))
						break;
				}

				if(i >= IF_NIFACE)
					strcpy(iface, "Unknown");
			}

			count_log(list[LH_IFACE_CONN], iface, 0);

			if(x == 4) {
				cddbd_snprintf(buf, sizeof(buf), "%s%s", user,
				    site);
				count_log(list[LH_CLNT_USER], tmp_client, buf);
			}
		}
	}

	fclose(fp);

	if(get) {
		printf(".\r\n");
		return;
	}

	printf("210 OK, log summary follows (until terminating `.')\r\n");

	if(log_all) {
		if(first[0] != '\0') {
			strcpy(start, make_time2(date_to_tm(first)));
			strcpy(end, make_time2(date_to_tm(last)));
			printf("Log status between %s and %s\r\n", start, end);
		}
	}
	else {
		strcpy(start, make_time2(date_to_tm(start)));
		strcpy(end, make_time2(date_to_tm(end)));
		printf("Log status between: %s and %s\r\n", start, end);
	}

	for(i = 0; log[i].name; i++)
		if(log[i].banr != 0 && !((log[i].dflag & L_NOSHOW) &&
		    log[i].cnt == 0))
			printf("%s: %d\r\n", log[i].banr, log[i].cnt);

	if(list_count(list[LH_CLNT_CONN]) > 0) {
		logdata[LH_CLNT_CONN] = sort_log(list[LH_CLNT_CONN]);
		logdata[LH_IFACE_CONN] = sort_log(list[LH_IFACE_CONN]);

		printf("Connections by client type:\r\n");

		x = list_count(list[LH_CLNT_CONN]);
		for(other_cnt = 0, i = 0; i < x; i++)
			other_cnt += logdata[LH_CLNT_CONN][i]->ld_count;

		/* Only print max_log_list clients. */
		if(x > max_log_list)
			x = max_log_list;

		for(i = 0; i < x; i++) {
			ld = logdata[LH_CLNT_CONN][i];
			other_cnt -= ld->ld_count;

			printf("    %s: %d\r\n", ld->ld_comp, ld->ld_count);
		}

		if(other_cnt > 0)
			printf("    Other: %d\r\n", other_cnt);

		printf("Connections by interface:\r\n");

		x = list_count(list[LH_IFACE_CONN]);
		for(other_cnt = 0, i = 0; i < x; i++)
			other_cnt += logdata[LH_IFACE_CONN][i]->ld_count;

		if(x > max_log_list)
			x = max_log_list;

		for(i = 0; i < x; i++) {
			ld = logdata[LH_IFACE_CONN][i];
			other_cnt -= ld->ld_count;

			printf("    %s: %d\r\n", ld->ld_comp, ld->ld_count);
		}

		if(other_cnt > 0)
			printf("    Other: %d\r\n", other_cnt);
	}


	if(list_count(list[LH_CLNT_USER]) > 0) {
		logdata[LH_CLNT_USER] = sort_log(list[LH_CLNT_USER]);

		x = list_count(list[LH_CLNT_USER]);
		for(other_cnt = 0, i = 0; i < x; i++)
			other_cnt += logdata[LH_CLNT_USER][i]->ld_count;

		printf("Users: %d\r\n", other_cnt);
		printf("Users by client type:\r\n");

		if(x > max_log_list)
			x = max_log_list;

		for(i = 0; i < x; i++) {
			ld = logdata[LH_CLNT_USER][i];
			other_cnt -= ld->ld_count;

			printf("    %s: %d\r\n", ld->ld_comp, ld->ld_count);
		}

		if(other_cnt > 0)
			printf("    Other: %d\r\n", other_cnt);
	}
	else
		printf("Users: 0\r\n");

	if(list_count(list[LH_READ]) > 0) {
		logdata[LH_READ] = sort_log(list[LH_READ]);

		printf("Most popular disc titles:\r\n");

		x = list_count(list[LH_READ]);
		if(x > max_log_list)
			x = max_log_list;

		for(i = 0; i < x; i++) {
			ld = logdata[LH_READ][i];
			printf("    %d %s\r\n", ld->ld_count, ld->ld_comp);
		}
	}

	printf("Total messages: %d\r\n", lines);
	printf("Log size: %d bytes\r\n", (int)sbuf.st_size);

	printf(".\r\n");
	fflush(stdout);

	for(i = 0; i < LH_NHEAD; i++) {
		if(logdata[i] != 0)
			free(logdata[i]);

		list_free(list[i]);
	}
}


void
free_log(void *l)
{
	if(((logdata_t *)l)->ld_list != 0)
		list_free(((logdata_t *)l)->ld_list);
	free(((logdata_t *)l)->ld_comp);
	free(l);
}


int
comp_log(void *l1, void *l2)
{
	return(cddbd_strcasecmp(((logdata_t *)l1)->ld_comp, (char *)l2));
}


int
comp_logdata(const void *l1, const void *l2)
{
	int i;
	logdata_t *ld1;
	logdata_t *ld2;

	ld1 = *(logdata_t **)l1;
	ld2 = *(logdata_t **)l2;

	i = ld2->ld_count - ld1->ld_count;
	if(i != 0)
		return i;

	return(cddbd_strcasecmp(ld1->ld_comp, ld2->ld_comp));
}


void
count_log(lhead_t *lh, char *comp, char *list_data)
{
	link_t *lp;
	logdata_t *ld;

	if((lp = list_find(lh, comp)) == 0) {
		if((ld = (logdata_t *)malloc(sizeof(logdata_t))) == 0) {
			cddbd_log(LOG_ERR, "Can't allocate memory for list.");
			quit(QUIT_ERR);
		}

		ld->ld_list = 0;
		ld->ld_count = 0;
		ld->ld_comp = strdup(comp);

		if(ld->ld_comp == 0) {
			cddbd_log(LOG_ERR, "Can't allocate memory for list.");
			quit(QUIT_ERR);
		}

		if((lp = list_add_cur(lh, ld)) == 0) {
			cddbd_log(LOG_ERR, "Can't allocate memory for list.");
			quit(QUIT_ERR);
		}
	}
	else
		ld = (logdata_t *)lp->l_data;

	if(list_data != 0) {
		if(ld->ld_list == 0 &&
		    (ld->ld_list = list_init(0, comp_log, free_log, 0)) == 0) {
			cddbd_log(LOG_ERR, "Can't allocate memory for list.");
			quit(QUIT_ERR);
		}

		if(!list_find(ld->ld_list, list_data))
			ld->ld_count++;

		count_log(ld->ld_list, list_data, 0);
	}
	else
		ld->ld_count++;
}


logdata_t **
sort_log(lhead_t *lh)
{
	link_t *lp;
	logdata_t **ld;
	logdata_t **ltab;

	ltab = (logdata_t **)malloc(sizeof(logdata_t *) * list_count(lh));
	if(ltab == 0) {
		cddbd_log(LOG_ERR, "Can't allocate memory for list.");
		quit(QUIT_ERR);
	}

	ld = ltab;

	for(ld = ltab, list_rewind(lh), list_forw(lh); !list_rewound(lh);
	    list_forw(lh), ld++) {
		lp = list_cur(lh);
		(*ld) = (logdata_t *)lp->l_data;
	}

	qsort((void *)ltab, list_count(lh), sizeof(logdata_t *), comp_logdata);

	return ltab;
}


void
do_cddb(arg_t *args)
{
	cmd_t *c;

	if(args->nargs < 2) {
		printf("500 Command syntax error.\r\n");
		return;
	}

	args->nextarg++;

	/* Execute the cddb command. */
	for(c = cddb_cmd; c->cmd; c++) {
		if(cddbd_strcasecmp(args->arg[args->nextarg], c->cmd))
			continue;

		if((mode & MFL_ASY) && !(c->flags & CF_ASY)) {
			printf("500 Unsupported CGI mode command.\r\n");
			_quit(QUIT_OK, 0);
		}

		/* Ensure we have a handshake. */
		if((c->flags & CF_HELLO) && !hello) {
			printf("409 No handshake.\r\n");
			return;
		}

		/* Disallow if not secure. */
		if((c->flags & CF_SECURE) && !secure) {
			printf("401 Permission denied.\r\n");
			return;
		}

		/* Note database accesses. */
		if(c->flags & CF_ACCESS)
			db_access++;

		(*(c->func))(args);

		return;
	}

	/* No valid command found. */
	printf("500 Unrecognized command.\r\n");
}


char *
get_time(int type)
{
	time_t now;
	now = time(0);

	if(type)
		return make_time(localtime(&now));
	else
		return make_time2(localtime(&now));
}


char *
make_time(struct tm *tm)
{
	static char buf[CDDBBUFSIZ];

	cddbd_snprintf(buf, sizeof(buf), "%s %s %02d %02d:%02d:%02d %d",
	    day[tm->tm_wday],
	    month[tm->tm_mon].name,
	    tm->tm_mday,
	    tm->tm_hour,
	    tm->tm_min,
	    tm->tm_sec,
	    (tm->tm_year + 1900));

	return buf;
}


char *
make_time2(struct tm *tm)
{
	static char buf[CDDBBUFSIZ];

	cddbd_snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d:%02d",
	    (tm->tm_mon + 1),
	    tm->tm_mday,
	    (tm->tm_year + 1900),
	    tm->tm_hour,
	    tm->tm_min,
	    tm->tm_sec);

	return buf;
}


int
make_year(int year)
{
	if(year <= 99 && year >= 70)
		year += 1900;
	else if(year < 70 && year >= 0)
		year += 2000;

	return year;
}


void
cddbd_parse_args(arg_t *args, int flags)
{
	int i;
	char *p;
	char *p2;
	int isarg;
	int isquote;
	int nomagic;

	args->flags = 0;
	args->nargs = 0;
	args->nextarg = 0;

	isarg = 0;
	isquote = 0;
	nomagic = 0;

	for(p = args->buf; *p != '\0'; nomagic = 0, p++) {
		if(*p == '\\') {
			p2 = p;
			do {
				p2++;
				*(p2 - 1) = *p2;
			} while(*p2 != '\0');

			if(*p == '\0')
				break;

			nomagic = 1;
		}

		if(!nomagic && *p == '"' && (flags & PF_HONOR_QUOTE)) {
			isquote = !isquote;

			if(isarg) {
				*p = '\0';
				isarg = 0;
			}

			continue;
		}

		if(isspace(*p) && !isquote) {
			if(isarg) {
				*p = '\0';
				isarg = 0;
			}
		}
		else {
			if(isspace(*p) && (flags & PF_REMAP_QSPC))
				*p = '_';

			if(!isarg) {
				args->arg[args->nargs] = p;
				args->nargs++;
				isarg++;
			}
		}

		if(args->nargs >= CDDBMAXARGS) {
			args->flags |= AF_MAXARG;
			break;
		}
	}

	/* Ensure that all args are limited in length. */
	for(i = 0; i < args->nargs; i++)
		if((int)strlen(args->arg[i]) > CDDBARGSIZ &&
		    !(flags & PF_NOTRUNC)) {
			args->flags |= AF_TRUNC;
			args->arg[i][CDDBARGSIZ] = '\0';
		}
}


void
usage(char *name)
{
  usage2 (name, (char *) 0);
}

void
usage2(char *name, char *extra)
{
	char *n;

	n = strrchr(name, '/');
	if(n == NULL)
		n = name;
	else
		n++;

	printf("usage: %s [-uef] [<-l|t <rhost|\"all\">> |\n", n);
if (extra != (char *) 0) printf ("extra: %s\n", extra);
	printf("       <-T <rhost|\"all\"> <hh[mm[ss[MM[DD[YY]]]]]>>]\n");
	printf("       [-c <check_lev|\"default\"> <fix_lev|\"default\">]\n");
	printf("       [-p <rhost|\"all\"> <file>] [-dqv] [-a access_file]\n");
	printf("or:    %s <-m|-M> [-dqv] [-a access_file]\n", n);
	printf("or:    %s -s <port|\"default\"> [-dqv] [-a access_file]\n", n);
	printf("or:    %s -C <rhost|\"all\"> <cddbp_command> <proto_lev>\n", n);
	printf("or:    %s -k\n", n);
	printf("or:    %s -P\n", n);

	exit(QUIT_ERR);
}


void
usage_uid(void)
{
	fprintf(stderr, "Your UID != EUID. Only server mode is permitted.\n");
	exit(QUIT_ERR);
}


struct tm *
date_to_tm(char *date)
{
	int x;
	int d[6];
	static struct tm tm;

	x = sscanf(date, "%04d%04d%04d%04d%04d%04d", &d[0], &d[1],
	    &d[2], &d[3], &d[4], &d[5]);

	if(x != 6)
		return 0;

	/* Check the year. */
	if(d[0] < 0)
		return 0;

	tm.tm_year = d[0] - 1900;

	/* Check the month. */
	d[1]--;
	if(d[1] > 11 || d[1] < 0)
		return 0;

	tm.tm_mon = d[1];

	/* Check the day, including leap years. */
	x = month[d[1]].ndays;
	if(d[2] > x || d[2] < 1 || (d[1] == LEAPMONTH && d[2] == LEAPDAY &&
	    !LEAPYEAR(d[0])))
		return 0;

	tm.tm_mday = d[2];

	/* Check the hour. */
	if(d[3] > 23 || d[3] < 0)
		return 0;

	tm.tm_hour = d[3];

	/* Check the minute. */
	if(d[4] > 59 || d[4] < 0)
		return 0;

	tm.tm_min = d[4];

	/* Check the seconds, allowing for leap seconds. */
	if(d[5] > 61 || d[5] < 0)
		return 0;

	tm.tm_sec = d[5];

	return &tm;
}


int
cvt_date(char *date, char *buf)
{
	int i;
	int x;
	int d[6];
	int ord[6] = { 3, 4, 5, 1, 2, 0 };
	time_t now;
	char tmp[5];
	struct tm *tm;

	now = time(0);
	tm = localtime(&now);


	/* Compute the date for today. */
	cddbd_snprintf(buf, CDDBBUFSIZ, "%04d%04d%04d%04d%04d%04d",
	    make_year(tm->tm_year + 1900), (++tm->tm_mon), tm->tm_mday,
	    tm->tm_hour, tm->tm_min, tm->tm_sec);

	/* Compute the user's date. */
	x = sscanf(date, "%02d%02d%02d%02d%02d%d", &d[0], &d[1],
	    &d[2], &d[3], &d[4], &d[5]);

	if(x == EOF)
		return 0;

	d[5] = make_year(d[5]);

	/* Generate a new date based on the user's input. */
	for(i = 0; i < x; i++) {
		cddbd_snprintf(tmp, sizeof(tmp), "%04d", d[i]);
		strncpy(&buf[(ord[i] * 4)], tmp, 4);
	}

	return(date_to_tm(buf) != 0);
}


void
cvt_time(time_t t, char *buf)
{
	struct tm *tm;

	tm = localtime(&t);

	/* Compute the date. */
	cddbd_snprintf(buf, CDDBBUFSIZ, "%04d%04d%04d%04d%04d%04d",
	    make_year(tm->tm_year + 1900), (++tm->tm_mon), tm->tm_mday,
	    tm->tm_hour, tm->tm_min, tm->tm_sec);
}


void
cddbd_lock_free_lk(void *l)
{
	clck_t *lk;

	lk = (clck_t *)l;
	if(lk->lk_refcnt > 1)
		lk->lk_refcnt = 1;

	cddbd_unlock(lk);
	free(lk);
}


void
cddbd_lock_free(clck_t *lk)
{
	link_t *lp;

	if(lock_head) {
		if((lp = list_find(lock_head, lk)) == 0) {
			cddbd_log(LOG_ERR | LOG_LOCK,
			    "Attempt to free unknown lock %s.", lk->lk_name);
			quit(QUIT_ERR);
		}

		list_delete(lock_head, lp);
	}

	cddbd_lock_free_lk(lk);
}


clck_t *
cddbd_lock_alloc(char *name)
{
	clck_t *lk;
	static int lock_initted = 0;

	if(!lock_initted) {
		lock_initted++;

		if((lock_head = list_init(0, 0, cddbd_lock_free_lk, 0)) == 0) {
			cddbd_log(LOG_ERR | LOG_LOCK,
			    "Can't allocate lock head.");
			quit(QUIT_ERR);
		}
	}

	if((int)strlen(name) > CDDBLCKNAMLEN) {
		cddbd_log(LOG_ERR | LOG_LOCK, "Lock name %s too long.",
		    name);
		quit(QUIT_ERR);
	}

	if((lk = (clck_t *)malloc(sizeof(clck_t))) == NULL) {
		cddbd_log(LOG_ERR | LOG_LOCK, "Can't allocate %s lock memory.",
		    name);
		quit(QUIT_ERR);
	}

	strcpy(lk->lk_name, name);
	lk->lk_refcnt = 0;

	if(list_add_back(lock_head, (void *)lk) == 0) {
		cddbd_log(LOG_ERR | LOG_LOCK, "Can't allocate %s lock.",
		    name);
		quit(QUIT_ERR);
	}

	return lk;
}


int
cddbd_locked(clck_t *lk)
{
	return(lk->lk_refcnt > 0);
}


int
cddbd_lock(clck_t *lk, int dowait)
{
	int fd;
	int cnt;
	int len;
	int tpid;
	time_t ct;   /* current time */
	time_t et;   /* expected time, we are prepared to wait until then but not longer! */
	pid_t pid;
	DIR *dirp;
	struct dirent *dp;
	char myname[CDDBBUFSIZ];
	char hisname[CDDBBUFSIZ];

	/* No lock allocated. Just return. */
	if(lk == 0)
		return 1;

	/* Already have the lock. */
	if(lk->lk_refcnt > 0) {
		lk->lk_refcnt++;
		return 1;
	}
		
	len = strlen(lk->lk_name);
	cddbd_snprintf(myname, sizeof(myname), "%s/%s.%05d", workdirs[WD_LOCK],
	    lk->lk_name, curpid);

	et = time(0) + lock_wait;

	/* Try to acquire the lock in a loop, forever. */
	do {
		if((fd = open(myname, (O_WRONLY | O_CREAT), file_mode)) < 0) {
			cddbd_log(LOG_ERR | LOG_LOCK,
			    "Can't create lock file: %s (%d).", myname, errno);
			quit(QUIT_ERR);
		}

		close(fd);

		/* Scan for inactive links. */
		if((dirp = opendir(workdirs[WD_LOCK])) == NULL) {
			cddbd_log(LOG_ERR | LOG_LOCK,
			    "Can't open lock dir: %s (%d).",
			    workdirs[WD_LOCK], errno);
			quit(QUIT_ERR);
		}

		/* Count lock files and remove inactive links. */
		cnt = 0;

		while((dp = readdir(dirp)) != NULL) {
			if(!strncmp(dp->d_name, lk->lk_name, len)) {
				if(sscanf(&dp->d_name[len+1], "%d", &tpid) != 1)
					continue;

				pid = (pid_t)tpid;

				/* If the lock file is stale, remove it. */
				if(pid != curpid && kill(pid, 0) != 0 &&
				    errno == ESRCH) {
					cddbd_snprintf(hisname, sizeof(hisname),
					    "%s/%s", workdirs[WD_LOCK],
					    dp->d_name);
					unlink(hisname);
				}
				else
					cnt++;
			}
		}

		closedir(dirp);

		/* We own the lock. */
		if(cnt == 1) {
			lk->lk_refcnt = 1;
			return 1;
		}

		unlink(myname);

		/* Leave if we're not waiting for the lock. */
		if(!dowait)
			return 0;

		cddbd_delay(lock_time);

		ct = time(0);
	} while(ct < et);

	/* We failed to get the lock after a lot of tries. */
	if(ct >= et) {
		cddbd_log(LOG_ERR | LOG_LOCK, "Failed to acquire lock: %s",
		    lk->lk_name);

		quit(QUIT_ERR);
	}

	return 0;
}


void
cddbd_unlock(clck_t *lk)
{
	char myname[CDDBBUFSIZ];

	if(lk->lk_refcnt == 0 || --(lk->lk_refcnt) > 0)
		return;

	cddbd_snprintf(myname, sizeof(myname), "%s/%s.%05d", workdirs[WD_LOCK],
	    lk->lk_name, curpid);

	unlink(myname);
}


int
cddbd_link(char *old, char *new)
{
	struct stat sbuf1;
	struct stat sbuf2;

	link(old, new);

	if(stat(new, &sbuf1) != 0 || stat(old, &sbuf2) != 0 ||
	    sbuf1.st_ino != sbuf2.st_ino)
		return 1;

	return 0;
}


int
cddbd_fork(void)
{
	pid_t f;
	link_t *lp;
	clck_t *lk;

	if((f = fork()) == 0) {
		curpid = getpid();

		/* Free any pending locks, since they're not inherited. */
		for(list_rewind(lock_head), list_forw(lock_head);
		    !list_rewound(lock_head); list_forw(lock_head)) {
			lp = list_cur(lock_head);
			lk = (clck_t *)lp->l_data;
			lk->lk_refcnt = 0;
		}

		/* Zero out per-process filenames. */
		lockfile[0] = '\0';
		tmpcount = 0;
	}

	return f;
}


long
cddbd_rand(void)
{
	pid_t tpid;
	static pid_t pid = 0;

	tpid = getpid();

	if(tpid != pid) {
		pid = tpid;
#ifndef __APPLE__
		srand48(pid + time(0));
#else
		srand(pid + time(0));
#endif
	}

#ifndef __APPLE__
	return((long)lrand48());
#else
	return((long)rand());
#endif	
}


/** 
 * Fixup file permissions and ownership.
 * 
 * @param file filename of the file to be fixed
 * @param mode requested mode/permissions (e.g. 777)
 * @param uid  requested owner user ID
 * @param gid  requested owner group ID
 * 
 * @return 0 if success
 */
int cddbd_fix_file(char *file, int mode, uid_t uid, gid_t gid)
{
	int cmode; /* current file mode */
	struct stat sbuf;

	if(stat(file, &sbuf) != 0) {
		cddbd_log(LOG_ERR, "Can't stat: %s (%d).", file, errno);
		return 1;
	}

	cmode = sbuf.st_mode & 07777;

	if(cmode != mode && chmod(file, mode) != 0) {
		cddbd_log(LOG_ERR, "Can't change permissions on: %s (%d). "
		    "Current: %o, wanted: %o.", file, errno, cmode, mode);
		return 1;
	}

	if((sbuf.st_uid != uid || sbuf.st_gid != gid) &&
	    chown(file, uid, gid) != 0) {
		cddbd_log(LOG_ERR, "Can't change owner/group on: %s (%d). "
		    "Current: %d/%d, wanted: %d/%d.",
		    file, errno, sbuf.st_uid, sbuf.st_gid, uid, gid);
		return 1;
	}

	return 0;
}


void
cddbd_hang(void)
{
	int n;
	time_t when;
	fd_set readfds;
	char buf[CDDBBUFSIZ];
	struct timeval timeout;

	hperm.hp_connect = HP_CONNECT_NO;

	if(nus < max_hangs)
		cddbd_log(LOG_INFO, "Hanging client.");
	else {
		cddbd_log(LOG_INFO, "Too many users to hang client.");
		_quit(QUIT_OK, 0);
	}
	when = time(0) + hang_time;

	/* Hang for a while, or until the connection is lost. */
	while(when > time(0)) {
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		/* Do the select. */
		FD_ZERO(&readfds);
		FD_SET(0, &readfds);

		errno = 0;

		n = select(1, &readfds, (fd_set *)NULL, (fd_set *)NULL,
		    &timeout);

		/* "Interrupted system call" isn't a real error. */
		if(n < 0 && errno != EINTR)
			_quit(QUIT_OK, 0);

		/* Did we find input? Check if we still have a connection. */
		if(n > 0 && read(0, buf, sizeof(buf)) <= 0)
			_quit(QUIT_OK, 0);
	}

	_quit(QUIT_OK, 0);
}


char *
cddbd_gets(char *buf, int cnt)
{
	int c;

	while((c = cddbd_getchar()) >= 0) {
		*buf = (char)c;

		buf++;
		cnt--;

		if(c == '\n' || cnt == 1) {
			*buf = '\0';
			return buf;
		}
	}

	return NULL;
}


int
cddbd_getchar(void)
{
	static int i = 0;
	static int e = 0;
	static unsigned char buf[CDDBBUFSIZ];

	if(i == e) {
		while(!cddbd_timer_sleep(timers))
			continue;

		e = read(0, buf, sizeof(buf));
		if(e <= 0) {
			e = i;
			return -1;
		}

		i = 1;
	}
	else
		i++;

	return(buf[i - 1]);
}


char *
cddbd_dirname(char *path)
{
	int len;
	char *p;
	static char *r = NULL;

	if(r != NULL) {
		free(r);
		r = NULL;
	}

	p = strrchr(path, '/');

	if(p == NULL)
		return(".");

	if(p == path)
		return("/");

	len = (int)(p - path);

	r = (char *)malloc(len + 1);
	if(r == NULL)
		return 0;

	strncpy(r, path, len);
	r[len] = '\0';

	return r;
}

char *
cddbd_basename(char *path)
{
	char *p;

	p = strrchr(path, '/');

	if(p == NULL)
		return path;

	p++;

	if(*p == '\0')
		return ".";

	return p;
}

void
cddbd_mk_workdirs(void)
{
	int i;
	int len;
	struct stat sbuf;

	for(i = 0; i < WD_NUMWORKDIRS; i++) {
		if(workdirs[i] != NULL)
			free(workdirs[i]);

		len = strlen(workdir_name[i]) + strlen(workdir) + 2;

		workdirs[i] = (char *)malloc(len);
		if(workdirs[i] == NULL) {
			cddbd_log(LOG_ERR, "Can't allocate workdir memory.");
			quit(QUIT_ERR);
		}

		cddbd_snprintf(workdirs[i], len, "%s/%s", workdir,
		    workdir_name[i]);

		if(stat(workdirs[i], &sbuf)) {
			if(mkdir(workdirs[i], (mode_t)dir_mode)) {
				cddbd_log(LOG_ERR | LOG_LOCK,
				    "Failed to create %s (%d).", workdirs[i],
				    errno);

				quit(QUIT_ERR);
			}

			(void)cddbd_fix_file(workdirs[i], dir_mode, uid, gid);
		}
		else if(!S_ISDIR(sbuf.st_mode)) {
			cddbd_log(LOG_ERR | LOG_LOCK, "%s is not a directory.",
			    workdirs[i]);
			quit(QUIT_ERR);
		}
	}
}

/**
 * Tells whether following the path given
 * will actually traverse a parent directory.
 *
 * @param dir  path to examine
 *
 * @return 0 if path doesn't traverse parent
 */
int
has_parent_dir(char *dir)
{
	int isslash;

	for(isslash = 1; *dir != '\0'; dir++) {
		if(isslash && (!strncmp(dir, "../", 3) || !strcmp(dir, "..")))
			return 1;

		if(*dir == '/')
			isslash = 1;
		else
			isslash = 0;
	}

	return 0;
}

void
cddbd_mk_servfiles(void)
{
	int i;
	int len;
	char *p;

	if(workdirs[WD_SERVER] == 0)
		p = tmpdir;
	else
		p = workdirs[WD_SERVER];

	for(i = 0; i < SF_NUMSERVFILES; i++) {
		if(servfile[i] != NULL)
			free(servfile[i]);

		len = strlen(servfile_name[i]) + strlen(p) + 2;

		servfile[i] = (char *)malloc(len);
		if(servfile[i] == NULL) {
			cddbd_log(LOG_ERR, "Can't allocate servfile memory.");
			quit(QUIT_ERR);
		}

		cddbd_snprintf(servfile[i], len, "%s/%s", p, servfile_name[i]);
	}
}


/* create a temporary file, usually in tmp; try CDDBMAXTMP (== 10) times */
char *
cddbd_mktemp(void)
{
	int i;
	int fd;
	char *p;
	char file[CDDBBUFSIZ];
	static int seq = 0;

	for(i = 0; i < CDDBMAXTMP; i++) {
		cddbd_snprintf(file, sizeof(file), "%s/%s%d.%d",
		    workdirs[WD_TMP], tmp_prefix, seq, curpid);

		p = strdup(file);
		if(p == NULL) {
			cddbd_log(LOG_ERR,
			    "Couldn't allocate temp file memory.");
			quit(QUIT_ERR);
		}

		seq++;

		fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, file_mode);
		if(fd >= 0) {
			close(fd);
			tmpcount++;
			(void)cddbd_fix_file(p, file_mode, uid, gid);

			return p;
		}

		free (p); /* a small chance for a memory leak */
	}

	cddbd_log(LOG_ERR, "Couldn't make temp file.");
	quit(QUIT_ERR);

	return (char *) 0; /* NOTREACHED but make gcc -pedantic happy */
}


void
cddbd_freetemp(char *file)
{
	tmpcount--;

	if(unlink(file))
		cddbd_log(LOG_ERR, "Couldn't unlink %s (%d).", file, errno);

	free(file);
}


void
cddbd_cleantemp(void)
{
	int len;
	int tpid;
	pid_t pid;
	DIR *dirp;
	char file[CDDBBUFSIZ];
	struct dirent *dp;

	/* Don't bother looking if we don't have any temp files. */
	if(tmpcount <= 0)
		return;

	/* Scan for old temp files. */
	if((dirp = opendir(workdirs[WD_TMP])) == NULL) {
		cddbd_log(LOG_ERR, "Can't open tmp dir: %s (%d).",
		    workdirs[WD_TMP], errno);

		quit(QUIT_ERR);
	}

	len = strlen(tmp_prefix);

	while((dp = readdir(dirp)) != NULL) {
		if(strncmp(dp->d_name, tmp_prefix, len) ||
		    sscanf(&dp->d_name[len], "%*d.%d", &tpid) != 1)
				continue;

		pid = (pid_t)tpid;

		/* If the tmp file is ours, or stale, remove it. */
		if(pid == curpid || (kill(pid, 0) != 0 && errno == ESRCH)) {
			cddbd_snprintf(file, sizeof(file), "%s/%s",
			    workdirs[WD_TMP], dp->d_name);

			unlink(file);
		}
	}

	closedir(dirp);
}


void
trunc_log(void)
{
	int cc;
	FILE *ifp;
	FILE *ofp;
	char *tlogfile;
	char buf[CDDBBUFSIZ];
	struct stat sbuf;

	/* Truncate the log if it's too long. */
	if(stat(servfile[SF_LOG], &sbuf) != 0) {
		cddbd_log(LOG_ERR, "Can't stat logfile: %s (%d).",
		    servfile[SF_LOG]);
		return;
	}

	if(sbuf.st_size < log_hiwat || log_hiwat == 0)
		return;

	if((ifp = fopen(servfile[SF_LOG], "r")) == NULL) {
		cddbd_log(LOG_ERR, "Can't open logfile %s for reading (%d).",
		    servfile[SF_LOG], errno_as_ptr);
		return;
	}

	if(fseek(ifp, -(log_lowat), SEEK_END)) {
		cddbd_log(LOG_ERR, "Can't truncate log file: %s. "
		    "Seek failed (%d)", servfile[SF_LOG], errno_as_ptr);
		fclose(ifp);
		return;
	}

	tlogfile = cddbd_mktemp();

	if((ofp = fopen(tlogfile, "w")) == NULL) {
		cddbd_log(LOG_ERR, "Can't open temp logfile %s for "
		    "writing (%d).", servfile[SF_LOG], errno_as_ptr);
		fclose(ifp);
		return;
	}

	/* Throw away the first line, otherwise come back later. */
	if(fgets(buf, sizeof(buf), ifp) == NULL) {
		cddbd_freetemp(tlogfile);
		fclose(ifp);
		fclose(ofp);

		return;
	}

	/* Copy the file from the seek point. */
	while((cc = fread(buf, 1, sizeof(buf), ifp)) > 0) {
		if(fwrite(buf, 1, cc, ofp) != cc) {
			cddbd_log(LOG_ERR, "Can't write temp logfile: %s (%d).",
			    tlogfile, errno_as_ptr);

			cddbd_freetemp(tlogfile);
			fclose(ifp);
			fclose(ofp);

			return;
		}
	}

	fclose(ifp);
	fclose(ofp);

	if(unlink(servfile[SF_LOG]) != 0 && errno != ENOENT) {
		cddbd_log(LOG_ERR, "Can't unlink %s (%d).", servfile[SF_LOG],
		    errno);
	}
	else if(cddbd_link(tlogfile, servfile[SF_LOG]) != 0) {
		cddbd_log(LOG_ERR, "Can't link %s to %s (%d).",
		    tlogfile, servfile[SF_LOG], errno);
	}

	cddbd_freetemp(tlogfile);

	return;
}


int
get_conv(char **p, char *buf)
{
	int cc;
	int pre1;
	int pre2;
	char c;
	char m;
	char *cp= (char *) 0;
	char *p2;
	char cbuf[CDDBBUFSIZ];
	char *mod = "hlL";
	char *conv = "cdDeEfginoOpsuUxX%";

	cc = 0;

	SCOPY(p, buf);

	if(**p == '%') {
		SCOPY(p, buf);
		SPUT('\0', buf);
		return 0;
	}

	p2 = *p;

	while(*p2 != '\0')
		if((cp = strchr(conv, *p2)) == 0)
			p2++;
		else
			break;

	if(*p2 == '\0') {
		/* no conversion specifier found */
		strncpy(buf, *p, (CDDBBUFSIZ - 1));
		buf[CDDBBUFSIZ - 1] = '\0';
		*p = p2;
		return 0;
	}

	if (cp == (char *) 0)
	{ /* This isn't really possible! cp can only be NULL if **p
	   * points to a string containing only % at the end.  This
	   * case is already handled above.
	   */
		return 0;
	}
	c = *cp;

	if((cp = strchr(mod, *(p2 - 1))) != 0)
		m = *(p2 - 1);
	else
		m = 0;

	while(**p != '\0' && **p != c && (!m || **p != m) && !isdigit(**p) &&
	    **p != '.')
		SCOPY(p, buf);

	pre1 = -1;
	pre2 = -1;

	if(isdigit(**p) || **p == '.') {
		if(**p == '0')
			SASGN('0', buf);

		if(sscanf(*p, "%d.%d", &pre1, &pre2) != 2)
			if(sscanf(*p, "%d", &pre1) != 1)
				sscanf(*p, ".%d", &pre2);
	}

	if(pre1 >= CDDBBUFSIZ)
		pre1 = CDDBBUFSIZ - 1;

	if(pre2 >= CDDBBUFSIZ || (pre2 == -1 && c == 's'))
		pre2 = CDDBBUFSIZ - 1;

	if(pre1 >= 0) {
		sprintf(cbuf, "%d", pre1);
		strncpy(&buf[cc], cbuf, (CDDBBUFSIZ - cc - 1));
		buf[CDDBBUFSIZ - 1] = '\0';
		cc = strlen(buf);
	}

	if(pre2 >= 0) {
		sprintf(cbuf, ".%d", pre2);
		strncpy(&buf[cc], cbuf, (CDDBBUFSIZ - cc - 1));
		buf[CDDBBUFSIZ - 1] = '\0';
		cc = strlen(buf);
	}

	*p = p2 + 1;

	if(m != 0)
		SASGN(m, buf);

	SASGN(c, buf);
	SPUT('\0', buf);

	return 1;
}


hperm_t *
ck_host_perms(int iface)
{
	link_t *lp;
	lhead_t *hh;
	hperm_t *hp;
	hperm_t *rp;
	static hperm_t perm;

	hh = iface_map[iface].if_host;

	if(iface_map[iface].if_flags & IFL_CMD) {
		perm.hp_get = HP_GET_NO;
		perm.hp_put = HP_PUT_NO;
		perm.hp_write = HP_WRITE_NO;
		perm.hp_update = HP_UPDATE_NO;
		perm.hp_connect = HP_CONNECT_OK;
		perm.hp_passwd = HP_PASSWD_OK;
	}
	else {
		perm.hp_connect = HP_CONNECT_OK;
		perm.hp_passwd = HP_PASSWD_OK;
	}

	if(!(iface_map[iface].if_flags & IFL_NOHEAD)) {
		for(rp = 0, list_rewind(hh), list_forw(hh); !list_rewound(hh);
		    list_forw(hh)) {
			lp = list_cur(hh);
			hp = (hperm_t *)lp->l_data;

			if(match_host(hp->hp_host))
				rp = hp;
		}

		if(rp == 0) 
			rp = &perm;
	}
	else
		rp = &perm;

	/* Mandatory permissions for non-server modes. */
	if(!(iface_map[iface].if_flags & IFL_CMD)) {
		if(!secure) {
			rp->hp_get = HP_GET_NO;
			rp->hp_put = HP_PUT_NO;
			rp->hp_write = HP_WRITE_NO;
			rp->hp_update = HP_UPDATE_NO;
		}
		else {
			rp->hp_get = HP_GET_OK;
			rp->hp_put = HP_PUT_OK;
			rp->hp_write = HP_WRITE_OK;
			rp->hp_update = HP_UPDATE_OK;
		}
	}

	switch(rp->hp_connect) {
	case HP_CONNECT_NO:
		if (iface == IF_HTTP)
			finish_header(0);

		/* Client not allowed. */
		printf("432 Unauthorized host: %s\r\n", rhost);

		cddbd_log(LOG_INFO | LOG_REJECT, hrejectstr,
		    iface_map[iface].if_name, rhost);

		_quit(QUIT_OK, 0);

		/* NOTREACHED */

	case HP_CONNECT_HANG:
		cddbd_log(LOG_INFO | LOG_REJECT, hrejectstr,
		    iface_map[iface].if_name, rhost);

		/* Host is nasty and must be delayed. */
		cddbd_check_nus();
		cddbd_hang();

		/* NOTREACHED */

	case HP_CONNECT_OK:
		break;

	default:
		cddbd_log(LOG_ERR | LOG_INTERN,
		    "Invalid connect permissions for %s: %d.", rp->hp_host,
		    rp->hp_connect);

		break;
	}

	modify_perms(rp);

	/* Start up timers. */
	if(!(iface_map[iface].if_flags & IFL_NOCONN))
		cddbd_timer_init(timers);

	return rp;
}


void
modify_perms(hperm_t *hp)
{
	if(postdir[0] == '\0' && WRITE_OK(*hp)) {
		if(verbose) {
			cddbd_log(LOG_ERR,
			    "No post directory - disallowing posting.");
		}

		hp->hp_write = HP_WRITE_NO;
	}

	if(dupdir[0] == '\0' && UPDATE_OK(*hp)) {
		if(verbose)
			cddbd_log(LOG_ERR, "No dup directory - discarding dups.");

		dup_ok = 0;
	}
	else if(UPDATE_OK(*hp))
		dup_ok = 1;
}


int
ck_client_perms(char *client, char *rev, int iface)
{
	int perm;
	link_t *lp;
	lhead_t *ch;
	cperm_t *cp;
	
	ch = iface_map[iface].if_client;

	/* Clients are all allowed by default. */
	perm = CP_ALLOW;

	for(list_rewind(ch), list_forw(ch); !list_rewound(ch); list_forw(ch)) {
		lp = list_cur(ch);
		cp = (cperm_t *)lp->l_data;

		/* This is always a match. */
		if(!strcmp(cp->cp_client, "-")) {
			perm = cp->cp_perm;
			continue;
		}

		if(!strcmp(cp->cp_client, client) &&
		    (!strcmp(cp->cp_lrev, "-") ||
		    match_rev(rev, cp->cp_lrev, CC_NEWER)) &&
		    (!strcmp(cp->cp_hrev, "-") ||
		    match_rev(rev, cp->cp_hrev, CC_OLDER)))
			perm = cp->cp_perm;
	}

	return(perm);
}


int
match_rev(char *srev, char *rev, int comp)
{
	int x;
	int pt1;
	int pt2;
	char pl1[CDDBBUFSIZ];
	char pl2[CDDBBUFSIZ];
	char buf1[CDDBBUFSIZ];
	char buf2[CDDBBUFSIZ];

	/* Always a match. */
	if(!strcmp(rev, "-"))
		return 1;

	reduce_rev(srev, buf1, pl1, &pt1);
	reduce_rev(rev, buf2, pl2, &pt2);

	x = comp_rev(buf1, buf2);
	if(x == 0) {
		x = pt1 - pt2;
		if(x == 0)
			x = comp_rev(pl1, pl2);
	}

	switch(comp) {
	case CC_NEWER:
		return(x >= 0);

	case CC_OLDER:
		return(x <= 0);

	default:
		cddbd_log(LOG_ERR, "Illegal match compare type: %d", comp);
		return 0;
	}
}


void
reduce_rev(char *r, char *b, char *pl, int *ptype)
{
	int x;
	int new;
	char *e;
	char buf[CDDBBUFSIZ];

	while(*r != '\0' && !isdigit(*r))
		r++;

	new = 0;
	*b = '\0';
	*pl = '\0';

	while(*r != 0 && (isdigit(*r) || *r == '.')) {
		if(*r == '.') {
			if(new)
				break;

			new = 1;
			r++;
			continue;
		}

		if(new) {
			strcat(b, ".");
			new = 0;
		}

		x = (int)strtol(r, &e, 10);

		/* This shouldn't happen, but let's be safe. */
		if(r == e)
			break;

		r = e;

		sprintf(buf, "%d", x);
		strcat(b, buf);
	}

	if(*r == '\0') {
		*ptype = ST_NONE;
	}
	else if(!cddbd_strncasecmp("develop", r, 5) ||
	    !cddbd_strncasecmp("d", r, 1)) {
		*ptype = ST_DEVELOP;

		while(*r != '\0' && !isdigit(*r))
			r++;

		sprintf(pl, "%d", atoi(r));
	}
	else if(!cddbd_strncasecmp("alpha", r, 5) ||
	    !cddbd_strncasecmp("a", r, 1)) {
		*ptype = ST_ALPHA;

		while(*r != '\0' && !isdigit(*r))
			r++;

		sprintf(pl, "%d", atoi(r));
	}
	else if(!cddbd_strncasecmp("beta", r, 4) ||
	    !cddbd_strncasecmp("b", r, 1)) {
		*ptype = ST_BETA;

		while(*r != '\0' && !isdigit(*r))
			r++;

		sprintf(pl, "%d", atoi(r));
	}
	else if(!cddbd_strncasecmp("pl", r, 2) ||
	    !cddbd_strncasecmp("patch", r, 5)) {
		*ptype = ST_PATCH;

		while(*r != '\0' && !isdigit(*r))
			r++;

		sprintf(pl, "%d", atoi(r));
	}
	else {
		*ptype = ST_UNKNOWN;
		strcpy(pl, r);
	}
}


int
comp_rev(char *r1, char *r2)
{
	int x;
	int v1;
	int v2;
	char *e1;
	char *e2;

	while(*r1 != '\0' && *r2 != '\0') {
		v1 = (int)strtol(r1, &e1, 10);
		v2 = (int)strtol(r2, &e2, 10);

		/* Should be impossible. */
		if(r1 == e1 || r2 == e2)
			return(v1 - v2);

		r1 = e1;
		r2 = e2;

		if(*r1 != '\0')
			r1++;
		if(*r2 != '\0')
			r2++;

		x = v1 - v2;
		if(x != 0)
			return x;
	}

	/* Whoever is longest wins. */
	return(strlen(r1) - strlen(r2));
}


int
cddbd_is_cgi(void)
{
	/* If this environment variable is set, we are in CGI mode. */
	return((char *)getenv(cgi_gateway_iface) != NULL);
}


static void
finish_header(char *charset)
{
	time_t t;

	t = time(0);

	if (charset)
		printf("%s: text/plain; charset=%s\n", content_type, charset);
	else
		printf("%s: text/plain\n", content_type);
	printf("%s: %s\n", expires, ctime(&t));
}


void
cddbd_cgi_cmd(void)
{
	int len;
	char *s;
	char cmd[CDDBCMDLEN];

	/* Set the interface type. */
	interface = IF_HTTP;

	/* Set up client permissions. */
	hperm = *ck_host_perms(interface);

	s = cddbd_safenv(cgi_request_method);

	/* Get the user's query. */
	if(!strcmp(s, "GET")) {
		s = cddbd_safenv(cgi_query_string);
		len = strlen(s);

		if(len < 0 || len >= sizeof(cmd)) {
			finish_header(0);
			printf("500 Invalid input length: max %d, min %d, "
			    "actual %d.\r\n", (int) (sizeof(cmd) - 1), 0, len);

			cddbd_log(LOG_ERR,
			    "CGI environment error: illegal %s: %d.",
			    cgi_query_string, len);

			_quit(QUIT_ERR, 0);
		}

		strncpy(cmd, s, len);
		cmd[len] = '\0';
	}
	else if(!strcmp(s, "POST")) {
		s = cddbd_safenv(cgi_content_len);
		len = atoi(s);

		if(len < 0 || len >= sizeof(cmd)) {
			finish_header(0);
			printf("500 Invalid input length: max %d, min %d, "
			    "actual %d.\r\n", (int) (sizeof(cmd) - 1), 0, len);

			cddbd_log(LOG_ERR,
			    "CGI environment error: illegal %s: %d.",
			    cgi_query_string, len);

			_quit(QUIT_ERR, 0);
		}

		if((len = read(0, cmd, len)) < 0) {
			finish_header(0);
			printf("402 Server error.\r\n");
			cddbd_log(LOG_ERR, "Can't read CGI POST data (%d).",
			    errno);

			_quit(QUIT_ERR, 0);
		}

		cmd[len] = '\0';
	}
	else {
		finish_header(0);
		printf("408 CGI environment error.\r\n");
		cddbd_log(LOG_ERR,
		    "CGI environment error: unknown %s: \"%s\".",
		    cgi_request_method, s);

		_quit(QUIT_OK, 0);
	}

	_quit(cddbd_async_command(cmd, 1), 0);
}


int
cddbd_async_command(char *buf, int cgi)
{
	int i;
	int pf;
	int len;
	int nlev;
	cmd_t *c;
	arg_t query;
	arg_t q[ASY_MAXPREFIX];

	/* Convert from CGI to standard format. */
	strcpy(query.buf, buf);
	cddbd_strip(query.buf, '\n');
	cddbd_strip(query.buf, '\r');
	cddbd_translate(query.buf, '&', ' ');

	/* Break up into parts. */
	cddbd_parse_args(&query, PF_NOTRUNC);

	if(query.nargs != ASY_MAXPREFIX) {
		if (cgi)
			finish_header(0);
		printf("500 Command syntax error: incorrect number of"
		    " arguments.\r\n");
		return QUIT_OK;
	}

	for(i = 0; i < ASY_MAXPREFIX; i++) {
		len = strlen(asy_prefix[i]);

		if(strncmp(query.arg[i], asy_prefix[i], len)) {
			if (cgi)
				finish_header(0);
			printf("500 Command syntax error: arg %d, expected "
			    "prefix \"%s\".\r\n", i, asy_prefix[i]);
			return QUIT_OK;
		}

		strcpy(q[i].buf, &query.arg[i][len]);
		cddbd_translate(q[i].buf, '+', ' ');
		asy_decode(q[i].buf);

		if((!charset_is_valid_latin1(q[i].buf)) &&
		   (!charset_is_valid_utf8(q[i].buf))) {
			if (cgi)
				finish_header(0);
			printf("500 Illegal character in input.\r\n");
			return QUIT_OK;
		}
	}
	
	/* Parse the inputs. */
	cddbd_parse_args(&q[ASY_PROTO], 0);

	if(q[ASY_PROTO].nargs != 1) {
		if (cgi)
			finish_header(0);
		printf("500 Command syntax error: incorrect arg count to"
		    "proto command.\r\n");
		return QUIT_OK;
	}

	nlev = atoi(q[ASY_PROTO].arg[0]);

	if(nlev < MIN_PROTO_LEVEL || nlev > MAX_PROTO_LEVEL) {
		if (cgi)
			finish_header(0);
		printf("501 Illegal CDDB protocol level: %d.\r\n", nlev);
		return QUIT_OK;
	}

	level = nlev;
	if (cgi)
		finish_header(!PROTO_ENAB(P_UTF8) ? "ISO-8859-1" : "UTF-8");

	if(PROTO_ENAB(P_QUOTE))
		pf = PF_HONOR_QUOTE | PF_REMAP_QSPC;
	else
		pf = 0;

	cddbd_log(LOG_INPUT, "Input: \"%s\"", q[ASY_COMMAND].buf);

	for(i = 0; i < ASY_MAXPREFIX; i++) {
		if(i == ASY_PROTO)
			continue;

		cddbd_parse_args(&q[i], pf);
	}

	/* Check the handshake data. */
	if(q[ASY_HELLO].nargs != 4) {
		printf("500 Command syntax error: incorrect arg count for"
		    "handshake.\r\n");

		return QUIT_OK;
	}

	/* Don't process empty commands. */
	if(q[ASY_COMMAND].nargs == 0) {
		printf("500 Empty command input.\r\n");
		return QUIT_OK;
	}

	/* Perform implied handshake. */
	if(_do_cddb_hello(&q[ASY_HELLO]) != 0)
		return QUIT_OK;

	/* Execute the command. */
	for(c = cmd; c->cmd; c++) {
		if(!cddbd_strcasecmp(q[ASY_COMMAND].arg[0], c->cmd)) {
			if(!(c->flags & CF_ASY)) {
				printf("500 Unsupported %s mode command.\r\n",
				    iface_map[interface].if_name);
				return QUIT_OK;
			}

			/* Ensure we have a handshake. */
			if((c->flags & CF_HELLO) && !hello) {
				printf("409 No handshake.\r\n");
				return QUIT_OK;
			}

			/* Disallow if not secure. */
			if((c->flags & CF_SECURE) && !secure) {
				printf("401 Permission denied.\r\n");
				return QUIT_OK;
			}

			(*(c->func))(&q[ASY_COMMAND]);

			return QUIT_OK;
		}
	}

	/* No command match found. */
	printf("500 Unrecognized command.\r\n");
	return QUIT_OK;
}


char *
cddbd_safenv(char *env)
{
	char *s;

	if((s = (char *)getenv(env)) == NULL) {
		printf("408 CGI environment error.\r\n");
		cddbd_log(LOG_ERR, "CGI environment error: no %s.", env);
		quit(QUIT_ERR);
	}

	return s;
}


void
cddbd_strip(char *p, char c)
{
	char *p2;

	p2 = p;

	while(*p2 != '\0') {
		if(*p2 != c) {
			*p = *p2;
			p++;
		}

		p2++;
	}

	*p = '\0';
}


void
cddbd_translate(char *p, char f, char t)
{
	while(*p != '\0') {
		if(*p == f)
			*p = t;
		p++;
	}
}


int
cddbd_elapse(int elapse)
{
	static int init;
	struct timeval tm;
	static struct timeval last;

	/* Reset the marker. */
	if(elapse <= 0) {
		init = 0;
		return 0;
	}

	if(gettimeofday(&tm, 0) != 0) {
		cddbd_log(LOG_ERR, "Can't get time of day (%d).\n", errno);
		quit(1);
	}

	/* Not called yet. */
	if(!init) {
		last = tm;
		init = 1;
		return 0;
	}

	/* Specified time has elapsed. */
	if(((tm.tv_sec * 1000) + (tm.tv_usec / 1000)) -
	    ((last.tv_sec * 1000) + (last.tv_usec / 1000)) >= elapse) {
		init = 0;
		return 1;
	}

	return 0;
}


void
cddbd_delay(int delay)
{
	struct timeval timeout;
	
	/* Don't delay. */
	if(delay <= 0)
		return;

	/* Delay is in ms. */
	timeout.tv_sec = delay / 1000;
	timeout.tv_usec = (delay * 1000) % 1000000;

	/* Delay. */
	(void)select(0, (fd_set *)NULL, (fd_set *)NULL, (fd_set *)NULL,
	    &timeout);
}


void
cddbd_snprintf(char *buf, int size, char *fmt, void *a0, void *a1, void *a2,
    void *a3, void *a4, void *a5, void *a6, void *a7, void *a8, void *a9)
{
	int x;
	int ano;
	int cnt;
	void *arg[10];
	char *p;
	char cbuf[CDDBBUFSIZ];
	char tbuf[CDDBBUFSIZ];

	arg[0] = a0;
	arg[1] = a1;
	arg[2] = a2;
	arg[3] = a3;
	arg[4] = a4;
	arg[5] = a5;
	arg[6] = a6;
	arg[7] = a7;
	arg[8] = a8;
	arg[9] = a9;

	p = fmt;
	cnt = 0;
	ano = 0;

	while(cnt < size) {
		switch(*p) {
		case '%':
			x = get_conv(&p, cbuf);

			if((ano + x) >= 10) {
				if(!logging) {
					cddbd_log(LOG_ERR,
					    "Too many args to snprintf, "
					    "format: \"%s\"", fmt);
				}

				buf[cnt] = '\0';
				return;
			}

			sprintf(tbuf, cbuf, arg[ano]);
			strncpy(&buf[cnt], tbuf, (size - cnt - 1));
			buf[size - 1] = '\0';
			cnt = strlen(buf);

			ano += x;

			break;

		case '\0':
			buf[cnt] = '\0';
			return;

		default:
			buf[cnt] = *p;
			cnt++;
			p++;

			break;
		}
	}

	buf[size - 1] = '\0';

	if(!logging)
		cddbd_log(LOG_ERR, "Buf overflow in snprintf: \"%s\"", buf);
}


void
cddbd_log_prim(FILE *fp, int flags, char *fmt, void *a1, void *a2, void *a3,
    void *a4, void *a5, void *a6)
{
	char buf[CDDBBUFSIZ];

	/* Log it. */
	if(!debug)
		fprintf(fp, "%s [%05d,%08X] ", get_time(0), curpid, flags);

	/* Copy fmt to a temp buffer so we can work on it. */
	strncpy(buf, fmt, sizeof(buf));
	buf[sizeof(buf) - 1] = '\0';
	strip_crlf(buf);

	/* Print the log message. */
	fprintf(fp, buf, a1, a2, a3, a4, a5, a6);
	fputc('\n', fp);
}


/* This should be the last function in the file, to avoid compile errors. */
void
cddbd_log(unsigned int flags, char *fmt, void *a1, void *a2, void *a3,
    void *a4, void *a5, void *a6)
{
	FILE *fp;
	unsigned int x;

	if((!(log_flags & flags) && !debug) || quiet)
		return;

	/* Detect recursion. */
	logging++;

	/* Lock the log file. */
	if(!debug && logging == 1)
		(void)cddbd_lock(locks[LK_LOG], 1);

	/* We can't open the log file. Continue anyway. */
	if(debug || (fp = fopen(servfile[SF_LOG], "a")) == NULL) {
		if(mode & MFL_ASY)
			fp = stdout;
		else
			fp = stderr;

		if(!debug) {
			cddbd_log_prim(fp, LOG_ERR,
			    "Warning: can't open log file: %s (%d)",
			    (void *)servfile[SF_LOG], errno_as_ptr,
			    0, 0, 0, 0);
		}
	}
	else if(logging == 1)
		(void)cddbd_fix_file(servfile[SF_LOG], file_mode, uid, gid);

	/* Make sure superflags are used properly. */
	x = flags & LOG_SUPER;

	if(!x)
		cddbd_log_prim(fp, LOG_ERR | LOG_INTERN,
		    "No log superflag in following message.", 0, 0, 0, 0, 0, 0);

	if(x & (x - 1))
		cddbd_log_prim(fp, LOG_ERR | LOG_INTERN,
		    "Multiple log superflags in following message.",
		    0, 0, 0, 0, 0, 0);

	/* Log the message. */
	cddbd_log_prim(fp, flags, fmt, a1, a2, a3, a4, a5, a6);

	if(fp != stderr && fp != stdout) {
		fclose(fp);
		if(logging == 1)
			trunc_log();
	}

	cddbd_unlock(locks[LK_LOG]);

	logging--;

	return;
}
