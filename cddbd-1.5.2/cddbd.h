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

#ifndef __CDDBD_H__
#define __CDDBD_H__

#ifndef LINT
static char *const _cddbd_h_ident_ = "@(#)$Id: cddbd.h,v 1.43.2.8 2006/04/19 16:49:05 joerg78 Exp $";
#endif

/* includes */
#include <sys/types.h>
#include <stdio.h>
#include "list.h"
#include "configurables.h"

/* OS dependencies */
#if defined __FreeBSD__ || defined __NetBSD__ || defined __OpenBSD__ || (defined __SVR4 && defined __sun)
#define HAS_UINT32_T
#endif /* defined __FreeBSD__ || defined __NetBSD__ || defined __OpenBSD__ || (defined __SVR4 && defined __sun) */

#ifdef __linux__
#include <stdint.h>
#define HAS_UINT32_T
#endif /* __linux__ */

#if defined __CYGWIN32__ || defined __host_mips || defined __hpux
#define HAS_UINT32_T
#ifndef socklen_t
#define socklen_t int
#endif
#endif /*defined __CYGWIN32__ || defined __host_mips || defined __hpux */

#ifdef _AIX
#ifndef socklen_t
/* AIX 4.2 does not know socklen_t, AIX 4.3 already has this type */
#define socklen_t size_t
#endif
void bzero (void *b, size_t len);
#endif /* _AIX */

#if defined __osf__ || defined __APPLE__
#ifndef socklen_t
#define socklen_t int
#endif
#endif /* __osf__ */

#if defined __alpha__ || defined __x86_64__
/* Oh my god, this is ugly!
 * The cddbd code uses type casts all over the place which is not
 * particularily good style.  Unfortunately, this doesn't work too
 * well on Alpha and AMD64, so these functions are provided as an
 * impedance match.
 */
/* Alpha and AMD64: (int) is 32 bit; (long) and (void *) are 64 bit */
#define ptr_to_uint32(x)         ((int) (((long) (x)) & 0x00000000FFFFFFFF))
#define to_uint32(x)             ((int) ((x) & 0x00000000FFFFFFFF))
#define errno_as_ptr             ((void *) (long) errno)
#define ptr_as_uint(x)           ((unsigned long) (x))
#define uint_as_ptr(x)           ((void *) (unsigned long) (x))
#define ino_t_as_ptr(x)          ((void *) (long) (x))
#else /* __alpha__ || __x86_64__ */
/* most other CPUs: (int), (long) and (void *) are all 32 bit */
#define ptr_to_uint32(x)         ((unsigned int) (x))
#define to_uint32(x)             ((int) x)
#define errno_as_ptr             ((void *) errno)
#define ptr_as_uint(x)           ((unsigned int) (x))
#define uint_as_ptr(x)           ((void *) (unsigned int) (x))
#define ino_t_as_ptr(x)          ((void *) (int) (x))
#endif /* __alpha__ || __x86_64__ */


/* Preprocessor definitions. */

#define TMPDIR		"/tmp"
#define LHOST		"localhost"

#define SECS_PER_MIN	60
#define SECS_PER_HOUR	(SECS_PER_MIN * 60)
#define SECS_PER_DAY	(SECS_PER_HOUR * 24)
#define SECS_PER_WEEK	(SECS_PER_DAY * 7)

#define SCOPY(p, buf) if(cc < (CDDBBUFSIZ-1)) {(buf)[cc] = **(p),(*(p))++,cc++;}
#define SASGN(c, buf) if(cc < (CDDBBUFSIZ-1)) {(buf)[cc] = c, cc++;}
#define SPUT(c, buf) (buf)[cc] = c, cc++

/* Fuzzy table header values. */
#define FUZZY_MAGIC	    0x32FD91C5
#define FUZZY_VERSION	3 

/* Define empty (default) discid */
#define DISCID_EMPTY	0x00000000

/* Protocol flags. */
#define P_QUOTE		0x00000001
#define P_SITES		0x00000002
#define P_QUERY		0x00000004
#define P_READ		0x00000008
#define P_UTF8		0x00000010

#define PROTO_ENAB(flag)	(proto_flags[level] & (flag))

/* Operation modes. */
#define MFL_SERV	0x00000001
#define MFL_ASY		0x00000002
#define MFL_QMSG	0x00000004

#define MODE_NONE	('N' << 24)			    /* No mode. */
#define MODE_SERV	(('S' << 24) | MFL_SERV | MFL_QMSG) /* CDDBP server. */
#define MODE_DB		('D' << 24)			    /* DB ops. */
#define MODE_MAIL	(('M' << 24) | MFL_SERV | MFL_ASY)  /* Mail filter . */
#define MODE_CGI	(('C' << 24) | MFL_SERV | MFL_ASY)  /* CGI program. */
#define MODE_IMMED	('I' << 24)			    /* Immediate op. */

/* Exit codes. */
#define QUIT_OK		0
#define QUIT_ERR	1
#define QUIT_RETRY	2

/* Logging flags. One superflag must be on all messages. */
#define LOG_HELLO	0x00000001	/* Superflag. */
#define LOG_INFO	0x00000002	/* Superflag. */
#define LOG_ERR		0x00000004	/* Superflag. */
#define LOG_ACCESS	0x00000008	/* Superflag. */
#define LOG_INPUT	0x00000010	/* Superflag. */
#define LOG_QUERY	0x00000020
#define LOG_UQUERY	0x00000040
#define LOG_FUZZY	0x00000080
#define LOG_HASH	0x00000100
#define LOG_LOCK	0x00000200
#define LOG_READ	0x00000400
#define LOG_WRITE	0x00000800
#define LOG_NET		0x00001000
#define LOG_UPDATE	0x00002000
#define LOG_MAIL	0x00004000
#define LOG_XMIT	0x00008000
#define LOG_REJECT	0x00010000
#define LOG_INTERN	0x00020000
#define LOG_SITES	0x00040000
#define LOG_GET		0x00080000
#define LOG_COLLIS	0x00100000
#define LOG_PASSWD	0x00200000
#define LOG_NONE	0x00000000
#define LOG_ALL		0xFFFFFFFF
#define LOG_SUPER	0x0000001F	/* Superflag mask. */

/* Values of file_charset. */
#define FC_ONLY_ISO	1
#define FC_PREFER_ISO	2
#define FC_PREFER_UTF	3
#define FC_ONLY_UTF	4

/* Values of utf_as_iso. */
#define UAI_ACCEPT	1
#define UAI_REJECT	2
#define UAI_CONVERT	3

/* Remote ops. */
#define RMT_OP_NONE	-1
#define RMT_OP_CATCHUP	0
#define RMT_OP_LOG	1
#define RMT_OP_TRANSMIT	2
#define RMT_OP_PUT	3
#define RMT_OP_CMD	4
#define NROPS		5

/* Parsing flags. */
#define PF_HONOR_QUOTE	0x00000001
#define PF_REMAP_QSPC	0x00000002
#define PF_NOTRUNC	0x00000004

/* DB function error codes. */
#define DE_NO_ERROR	0		/* DB entry is okay. */
#define DE_NOMEM	1		/* No memory for data structures. */
#define DE_FILE		2		/* File access error. */
#define DE_SYSERR	3		/* System call error. */
#define DE_INTERNAL	4		/* Internal parsing error (bug). */
#define DE_INVALID	5		/* Database entry is invalid. */

/* Mail function error codes. */
#define EE_OK		0
#define EE_ERROR	1

/* Defines for getsiteent. */
#define SITE_XMIT	0
#define SITE_LOG	1
#define SITE_INFO	2
#define SITE_GROUP	3

/* DB function flags. */
#define DF_STDIN	0x00000001	/* File is being read from stdin. */
#define DF_MAIL		0x00000002	/* File is an email submission. */
#define DF_DONE		0x00000004	/* DB entry has been read. */
#define DF_SUBMITTER	0x00000008	/* Entry has a "submitted via" field. */
#define DF_CK_SUBMIT	0x00000010	/* Check submitter. */
#define DF_ENC_LATIN1	0x00000020	/* Allow input in ISO-8859-1. */
#define DF_ENC_UTF8	0x00000040	/* Allow input in UTF-8. */
#define DF_WIN		0x00000080	/* File is a "Windows format" database file. */

/* DB parsing phases. */
#define DP_COMMENT	0
#define DP_DISCID	1
#define DP_DTITLE	2
#define DP_DYEAR	3
#define DP_DGENRE	4
#define DP_TTITLE	5
#define DP_EXTD		6
#define DP_EXTT		7
#define DP_PLAYORDER	8
#define DP_NPHASE	9

/* Mail flags. */
#define MF_NONE		0x00000000	/* nothing. */
#define MF_FAIL		0x00000001	/* Failed submission email. */
#define MF_TEST		0x00000002	/* Test submission email. */
#define MF_MULTI	0x00000004	/* Mail should be multipart. */
#define MF_ENC		0x00000008	/* Mail should be encoded. */

/* Dup entry policy. */
#define	DUP_NEVER	0
#define	DUP_ALWAYS	1
#define	DUP_COMPARE	2
#define	DUP_DEFAULT	DUP_COMPARE
#define PERM_DEF_DUP	0

/* Mail encoding types. */
#define CE_QUOTED_PRINT	0
#define CE_BASE64	1
#define CE_7BIT		2
#define CE_8BIT		3
#define CE_BINARY	4

/* Mail classes. */
#define EC_NONE		0
#define EC_SUBMIT	1
#define EC_COMMAND	2

/* Charset types (only a subset). */
/* zeke - changes on these defines... */
#define CC_US_ASCII	0
#define CC_ISO_8859	1
#define CC_UTF_8	2

/* Submitter revision types, in ranked order. */
#define ST_DEVELOP	0
#define ST_ALPHA	1
#define ST_BETA		2
#define ST_UNKNOWN	3
#define ST_NONE		4
#define ST_PATCH	5

/* Timers. */
#define INPUT_TIMER	0
#define ACCESS_TIMER	1
#define CONNECT_TIMER	2

/* Asynchronous command field prefixes. */
#define ASY_COMMAND	0
#define ASY_HELLO	1
#define ASY_PROTO	2
#define ASY_MAXPREFIX	3

/* Work subdirectories. */
#define WD_LOCK		0
#define WD_SERVER	1
#define WD_TMP		2
#define WD_NUMWORKDIRS	3

/* Server files. */
#define SF_FUZZY	0
#define SF_HIST		1
#define SF_LOG		2
#define SF_NUMSERVFILES	3

/* Global lock names. */
#define LK_FUZZY	0
#define LK_MKFUZZ	1
#define LK_HIST		2
#define LK_LOG		3
#define LK_NUS		4
#define LK_PUT		5
#define LK_UPDATE	6
#define LK_NUMLOCKS	7

#ifdef DB_WINDOWS_FORMAT
/* Category-specific lock names. */
#define CATLK_MERGE	0 /* Merge lock */
#define CATLK_NUMLOCKS	1
#endif

/* Encoding errors. */
#define EN_OK		0
#define EN_ERROR	1
#define EN_INPUT	2

/* Encoding ops. */
#define EO_START 	0
#define EO_END 		1
#define EO_ENCODE 	2
#define EO_DECODE 	3

/* TEA encoding parameters. */
#define CT_PASSWD	0xDEADBEEF
#define CT_DELTA	0x9E3779B9
#define CT_SUM		0xC6EF3720
#define CT_PASSES	32
#define CT_PASSWD_LEN	32
#define CT_KEY_LEN	4
#define CT_DATA_LEN	2

/* Constants. */
#define CDDBHOSTNAMELEN	64	/* Max host name length. */
#define DBLINESIZ   	256	/* Max number of chars per line in a DB entry. */
#define CDDBBUFSIZ	2048	/* Max input buffer size. */
#define CDDBARGSIZ	128	/* Max input arg size. */
#define CDDBMAXARGS	128	/* Max # of args for a CDDBP command. */
#define CDDBCMDLEN	2048	/* Max size of a CDDBP command. */
#define CDDBPATHSIZ	128	/* Max pathname buffer size. */
#define CDDBCATEGSIZ	32	/* Max category name size. */
#define CDDBPHASESZ	12	/* Max phase name size. */
#define CDDBOUTSIZ	80	/* Max output buffer size. */
#define CDDBSERIALSIZ	16	/* Max email serial # size. */
#define CDDBPLBLSIZ	16	/* Max password label size. */
#define CDDBMAXTMP	10	/* Max # of attempts at getting a temp file. */
#define CDDBMAXTRK	99	/* Max number of tracks on a CD. */
#ifdef DB_WINDOWS_FORMAT
#define CDDBLCKNAMLEN	41	/* Length of lock file prefix. */
#else
#define CDDBLCKNAMLEN	8	/* Length of lock file prefix. */
#endif
#define CDDBUINTLEN	8	/* Length of a 32 bit ascii unsigned int. */
#define CDDBXCRCLEN	16	/* Length of a Mime ascii CRC. */
#define CDDBXECHOLEN	64	/* Length of a Mime echo string. */
#define CDDBXNOTELEN	70	/* Length of a Mime note string. */
#define CDDBCRCLEN	8	/* Length of an ascii CRC. */
#define CDDBDISCIDLEN	8	/* Length of database file name. */
#define CDDBFRAMEPERSEC	75	/* Number of frames in a second. */
#define CDDBMAXDBDIR	50	/* Max number of database subdirectories. */
#define CDDBMAXERR	3	/* Max number of allowable errors. */
#define CDDBMINREV	0	/* Min entry revision level. */
#define MAX_QUITS	3	/* Max number of reentrant quit calls. */ 
#define MAX_LOG_LIST	30	/* Max number of lines in a log list. */
#define MIN_PROTO_LEVEL	1	/* Minimum protocol level supported. */
#define MAX_PROTO_LEVEL	6	/* Protocol level this server supports. */
#define DEF_PROTO_LEVEL	1	/* Default protocol level. */
#define UNICODE_LEVEL	6	/* Lowest level that uses UTF-8. */
#define DEF_XMIT_TO	60	/* Default transmit timeout. */
#define DEF_INPUT_TO	60	/* Default input timeout. */
#define DEF_ACCESS_TO	60	/* Default access timeout. */
#define DEF_CONNECT_TO	600	/* Default connect timeout. */
#define DEF_EMAIL_TIME	500	/* Default delay between mailings (ms). */
#define DEF_MAX_LINES	1024	/* Default max post size. */
#define DEF_MAX_SIZE	102400	/* Default max put file size. */
#define DEF_MAX_USERS	100	/* Default max users. */
#define DEF_MAX_HANG	10	/* Default max concurrent hangs. */
#define DEF_MAX_XMITS	2	/* Default max transmits. */
#define DEF_LOG_HIWAT	512000	/* Default log high water mark. */
#define DEF_LOG_LOWAT	384000	/* Default log low water mark. */
#define DEF_DIR_MODE	0755	/* Default mode for directory creation. */
#define DEF_FILE_MODE	0644	/* Default mode for file creation. */
#define DEF_FUZZ_FACT	900	/* Default fuzzy factor. */
#define DEF_FUZZ_DIV	4	/* Default fuzzy divisor. */
#define DEF_ELAPSE_TIME	500	/* Default elapse time (ms). */
#define DEF_DELAY_TIME	250	/* Default delay time (ms). */
#define DEF_HANG_TIME	60	/* Default hang time (sec). */
#define DEF_LOCK_TIME	250	/* Default lock retry time (ms). */
#define DEF_LOCK_WAIT	60	/* Default lock wait time (sec). */
#define DEF_STRIP_EXT	0	/* Default extended data strip value. */
#define DEF_CK_BERZERK	1	/* Default "check berzerk" value. */ 

#define MIN_CHECK_LEVEL	1	/* Min DB checking level. */
#define MAX_CHECK_LEVEL	1	/* Max DB checking level. */
#define MIN_FIX_LEVEL	1	/* Min DB fix level. */
#define MAX_FIX_LEVEL	3	/* Max DB fix level. */
#define FL_REPAIR	2	/* DB checking level for repairing entries. */
#define FL_REMOVE	3	/* DB checking level for removing entries. */

#ifdef DB_WINDOWS_FORMAT
#define WINFILEIDLEN	6		/* filename length in windows db e.g. 01to23 */ 
#define STARTTAG	"#FILENAME="	/* discid tag in "windows" file */
#define STARTTAGLEN	10		/* length of the tag */
#define WINMAXPOSTRECORDS 1000		/* maximum number of records when posting before a merge */
#endif /* DB_WINDOWS_FORMAT */



/* Structure definitions. */

typedef int db_errno_t;
typedef int proto_t;

#define CT_INPUT_RST	0x00000001	/* Reset timer on input. */
#define CT_ACCESS_RST	0x00000002	/* Reset timer on successful access. */
#define CT_WRITE_DIS	0x00000004	/* Disable timer if write allowed. */

/* Timer struct definition. */
typedef struct ctimer {
	char 	*name;		/* Timer name. */
	void	(*func)(void);	/* Routine to invoke at timeout. */
	int	flags;		/* Timer flags. */
	long	def_seconds;	/* Default seconds until a timeout. */
	long	seconds;	/* Seconds until a timeout. */
	long	left;		/* Seconds left until next timeout. */
} ctimer_t;


/* Services table indices. Must match the table. */
#define SERV_CDDBP	0
#define SERV_SMTP	1
#define SERV_HTTP	2

/* Local servent structure. */
typedef struct cddbd_servent {
	char *s_name;
	char *s_proto;
	int s_port;
} cservent_t;


/* Log struct definition. */

#define L_NOSHOW	0x00000001

typedef struct log {
	unsigned int	flag;		/* Flag. */
	int		cnt;		/* Message count. */
	int		dflag;		/* Display flags. */
	char		*name;		/* Log flag name. */
	char		*banr;		/* Banner. */
} log_t;


/* Command struct definition. */
#define CF_HELLO	0x00000001	/* Hello is necessary first. */
#define CF_SUBCMD	0x00000002	/* This command has subcommands. */
#define CF_ACCESS	0x00000004	/* This command accesses the database. */
#define CF_ASY		0x00000008	/* This command supported as async. */
#define CF_SECURE	0x00000010	/* The server is configured securely. */

typedef struct cmd {
	char	*cmd;			/* Command string. */
	void	(*func)();		/* Command function. */
	char	**help;			/* Help string. */
	int	flags;			/* Command flags. */
} cmd_t;


/* Access field type definition. */
#define AC_PATH		0		/* This is a pathname. */
#define AC_NUMERIC	1		/* This is numeric. */
#define AC_USER		2		/* This is a user name. */
#define AC_GROUP	3		/* This is a group name. */
#define AC_STRING	4		/* This is a generic string. */
#define AC_MODE		5		/* This is an octal file mode. */
#define AC_BOOL		6		/* This is a boolean. */

#define AF_ZERO		0x00000001	/* Numeric arg has a min 0 value. */
#define AF_NONZ		0x00000002	/* Numeric arg has a min 1 value. */
#define AF_NODEF	0x00000004	/* No default value. */

typedef void *ad_t;

typedef struct access {
	char	*at_fname;		/* Field name. */
	void	*at_addr;		/* Where to put the value. */
	int	at_type;		/* Field type. */
	int	at_flags;		/* Access flags. */
	ad_t	at_def;
} access_t;

/* Argument structure definition. */
#define AF_TRUNC	0x00000001
#define AF_MAXARG	0x00000002

typedef struct arg {
	int flags;
	int nargs;
	int nextarg;
	char *arg[CDDBMAXARGS];
	char buf[CDDBCMDLEN];
} arg_t;


/* Month struct definition. */
#define NMONTH		12
#define LEAPDAY		29
#define LEAPMONTH	1
/* #define LEAPYEAR(y)	(!(y % 4)) wrong! Y2K */
#define LEAPYEAR(y)	(((!(y % 4)) && (y % 100)) || (!(y % 400)))

typedef struct month {
	char *name;
	int ndays;
} month_t;


/* zeke - mini email hdr for parsing in db struct */
typedef struct email_hdr_db {
	int eh_valid;
	int eh_flags;
	int eh_charset;
	int eh_encoding;
	char eh_to[CDDBBUFSIZ];
	char eh_rcpt[CDDBBUFSIZ];
	char eh_subj[CDDBBUFSIZ];
	char eh_host[CDDBBUFSIZ];
} email_hdr_db_t;


/* Defines for db_t flags. */
#define DB_DISCLEN	0x00000001
#define DB_OFFSET	0x00000002
#define DB_REVISION	0x00000004
#define DB_SUBMITTER	0x00000008
#define DB_PROCESSOR	0x00000010
#define DB_STRIP	0x00000020
#define DB_EMPTY	0x00000040
#define DB_ENC_ASCII	0x00000080	/* Data valid as US-ASCII. */
#define DB_ENC_LATIN1	0x00000100	/* Data valid as ISO-8859-1. */
#define DB_ENC_UTF8	0x00000200	/* Data valid as UTF-8. */

/* Defines for db_t.db_rev. */
#define DB_MAX_REV	-1

/* Database entry struct. */
typedef struct db {
	email_hdr_db_t db_eh;		/* zeke - added for eh transfer... */
	int db_flags;
	int db_rev;
	int db_offset[CDDBMAXTRK];
	short db_disclen;
	char db_trks;
	lhead_t *db_phase[DP_NPHASE];
} db_t;


#define PF_MULTI	0x00000001	/* Multiple lists in this list. */
#define PF_STRIP	0x00000002	/* Strip on input. */
#define PF_OSTRIP	0x00000004	/* Optionally strip on output. */
#define PF_NUMERIC	0x00000008	/* disc-id */
#define PF_REQUIRED	0x00000010	/* Required field. */
#define PF_IGNORE	0x00000020	/* Ignore when parsing (comment). */
#define PF_ONEREQ	0x00000040	/* At least one field must be filled. */
#define PF_CKTIT	0x00000080	/* Check title content. */
#define PF_CKTRK	0x00000100	/* Check track content. */
#define PF_CKEMPTY	0x00000200	/* Check if it's empty. */ 
#define PF_OPTIONAL	0x00000400	/* Entry is optional */
#define PF_NUMBER	0x00000800	/* Check for a number */

/* DB field descriptions. */
typedef struct db_parse {
	char *dp_str;
	char *dp_name;
	char *dp_pstr;
	int dp_flags;
} db_parse_t;


#define CF_BLANK	0x00000001
#define CF_INVALID	0x00000002

typedef struct db_content {
	int dc_flags;
} db_content_t;


/* Lock struct. */
typedef struct clck {
	char lk_name[CDDBLCKNAMLEN + 1];
	int lk_refcnt;
} clck_t;


/* World coordinate structure. */
typedef struct coord {
	char co_compass;
	int co_degrees;
	int co_minutes;
} coord_t;


/* Remote site file entry struct. */
#define ST_PROTO_NONE	0
#define ST_PROTO_CDDBP	1
#define ST_PROTO_SMTP	2
#define ST_PROTO_GROUP	3

#define ST_FLAG_NOXMIT	0x00000001 /* do not transmit to this site */
#define ST_FLAG_PWDLBL	0x00000002 /* need to use passwd for authorizing transmissions */
#define ST_FLAG_OMIME	0x00000004 /* do not use multipart e-mails (QP & BASE64) for transmission */
#define ST_FLAG_OFIELDS	0x00000008 /* send only original xmcd file format fields (no DGENRE/DYEAR) */
#define ST_FLAG_ONLYISO	0x00000010 /* send transmission e-mails in utf-8 */


typedef struct csite {
	char st_name[CDDBHOSTNAMELEN + 1];
	char st_desc[CDDBBUFSIZ];
	char st_pname[CDDBBUFSIZ];
	char st_addr[CDDBBUFSIZ];
	char st_pwdlbl[CDDBPLBLSIZ + 1];
	int st_proto;
	int st_port;
	int st_flags;
	coord_t st_lat;
	coord_t st_long;
} csite_t;


/* MIME encoding. */
typedef struct encode {
	char *en_type;
	int (*en_encode)(int, unsigned char *, unsigned char **, int *);
	int (*en_decode)(int, unsigned char *, unsigned char **, int *);
} encode_t;


/* Charset info. */
typedef struct charset {
	char *name;
	int df_flags;
} charset_t;


/* Fuzzy index table header. */
typedef struct fuzzy_hdr {
	int hd_magic;
	int hd_version;
	int hd_count;
	int hd_tcount[CDDBMAXTRK];
	int hd_addr[CDDBMAXTRK];
	char hd_categ[CDDBMAXDBDIR][CDDBCATEGSIZ];
} fhdr_t;


/* Fuzzy table entry data. */
typedef struct fuzzy_data {
	unsigned int fd_discid;	/* Disc ID. */
	unsigned int fd_trks : 7;		/* Track count. */
	unsigned int fd_disclen : 13;	/* Length of disc in seconds. */
	unsigned int fd_catind : 4;	/* Category index. */
} fdata_t;


#define OFFSET_BIT_WIDTH 19
/* Fuzzy table entry. */
typedef struct fuzzy_hash {
	fdata_t fh_s;
	int fh_offset[1];		/* Track offsets. Must be at end. */
} fhash_t;

#define fh_discid	fh_s.fd_discid
#define fh_trks		fh_s.fd_trks
#define fh_disclen	fh_s.fd_disclen
#define fh_catind	fh_s.fd_catind


/* Fuzzy match list entry. */
typedef struct fuzzy_match {
	fdata_t fm_s;
	char *fm_dtitle;
} fmatch_t;

#define fm_discid	fm_s.fd_discid
#define fm_trks		fm_s.fd_trks
#define fm_disclen	fm_s.fd_disclen
#define fm_catind	fm_s.fd_catind


/* In-core fuzzy table list entry. */
typedef struct fuzzy_list {
	struct fuzzy_list *fl_next;
	fhash_t fl_fhash;		/* This field must be last. */
} flist_t;


/* Client permission rule struct. */
#define CP_ALLOW	0
#define CP_DISALLOW	1
#define CP_HANG		2

#define CC_NEWER	0
#define CC_OLDER	1

typedef struct client_perm {
	char *cp_client;
	char *cp_lrev;
	char *cp_hrev;
	int cp_perm;
} cperm_t;


/* Host permission rule struct. */

#define PASSWD_OK(hperm) \
	((hperm).hp_passwd == HP_PASSWD_OK)

#define PASSWD_REQ(hperm) \
	((hperm).hp_passwd == HP_PASSWD_REQ)

#define WRITE_OK(hperm) \
	(PASSWD_OK(hperm) && (hperm).hp_write == HP_WRITE_OK)

#define CONNECT_OK(hperm) \
	(PASSWD_OK(hperm) && (hperm).hp_connect == HP_CONNECT_OK)

#define UPDATE_OK(hperm) \
	(PASSWD_OK(hperm) && (hperm).hp_update == HP_UPDATE_OK)

#define GET_OK(hperm) \
	(PASSWD_OK(hperm) && (hperm).hp_get == HP_GET_OK)

#define PUT_OK(hperm) \
	(PASSWD_OK(hperm) && (hperm).hp_put == HP_PUT_OK)

#define HP_GET_OK	0
#define HP_GET_NO	1

#define HP_PUT_OK	0
#define HP_PUT_NO	1

#define HP_WRITE_OK	0
#define HP_WRITE_NO	1

#define HP_UPDATE_OK	0
#define HP_UPDATE_NO	1

#define HP_CONNECT_OK	0
#define HP_CONNECT_NO	1
#define HP_CONNECT_HANG	2

#define HP_PASSWD_OK	0
#define HP_PASSWD_NO	1
#define HP_PASSWD_REQ	2

typedef struct host_perm {
	char *hp_host;
	char hp_pwdlbl[CDDBPLBLSIZ + 1];
	int hp_get;
	int hp_put;
	int hp_write;
	int hp_update;
	int hp_connect;
	int hp_passwd;
} hperm_t;


/*
 * Interface property structure.
 * These defs must be in the same order as iface_map[].
 */
#define IF_SUBMIT	0
#define IF_EMAIL	1
#define IF_CDDBP	2
#define IF_HTTP		3
#define IF_UNKNOWN	4
#define IF_NONE		5
#define IF_NIFACE	6

#define IFL_NOHEAD	0x00000001
#define IFL_NOCOUNT	0x00000002
#define IFL_NOCONN	0x00000004
#define IFL_NOLOG	0x00000008
#define IFL_CMD		0x00000010
#define IFL_COOK	0x00000020

typedef struct interface {
	char *if_name;
	char if_char;
	int if_flags;
	lhead_t *if_host;
	lhead_t *if_client;
} iface_t;


/* Log statistics data. */
typedef struct logdata {
	int ld_count;
	char *ld_comp;
	lhead_t *ld_list;
} logdata_t;


/* Email header struct. */
#define EH_RCPT		0x00000001
#define EH_SUBJECT	0x00000002
#define EH_BOUNDARY	0x00000004
#define EH_CRC		0x00000008
#define EH_ECHO		0x00000010
#define EH_NOTE		0x00000020

typedef struct email_hdr {
	int eh_flags;
	int eh_class;
	int eh_charset;
	int eh_encoding;
	unsigned int eh_discid;
	char eh_to[CDDBBUFSIZ];
	char eh_rcpt[CDDBBUFSIZ];
	char eh_subj[CDDBBUFSIZ];
	char eh_host[CDDBBUFSIZ];
	char eh_category[CDDBBUFSIZ];
	char eh_boundary[CDDBBUFSIZ];
	char eh_serial[CDDBSERIALSIZ + 1];
	char eh_crc[CDDBXCRCLEN + 1];
	char eh_echo[CDDBXECHOLEN + 1];
	char eh_note[CDDBXNOTELEN + 1];
} email_hdr_t;


/* TEA key strucure. */
typedef struct ct_key {
	int ck_key[CT_KEY_LEN];
} ct_key_t;

typedef struct ct_data {
	int cd_data[CT_DATA_LEN];
} ct_data_t;

/* CRC function definitions. */
#define CRC_START	0
#define CRC_END		1
#define CRC_COMPUTE	2
#define CRC_ONCE	3
#define CRC_STRING	4
#define CRC_FILE	5
#define CRC_DESC	6

/* Assumes 32-bit integers. */
#ifndef HAS_UINT32_T
typedef unsigned int uint32_t;
#endif


/* External prototypes. */

void _quit(int, int);
void cddbd_bcopy(char *, char *, int);
void cddbd_build_fuzzy(void);
void cddbd_bzero(char *, int);
void cddbd_check_access(void);
void cddbd_check_db(int, int);
void cddbd_delay(int);
void cddbd_freetemp(char *);
void cddbd_hang(void);
void cddbd_lock_free(clck_t *);
void cddbd_log();
void cddbd_mail(int);
void cddbd_match(void);
void cddbd_parse_args(arg_t *, int);
void cddbd_rmt_op(int, char *, char *, char *);
void cddbd_cddbp_cmd(void);
void cddbd_setuid(void);
void cddbd_snprintf();
void cddbd_stand(int);
void cddbd_tea(int, ct_data_t *, ct_key_t *);
void cddbd_unlock(clck_t *);
void cddbd_update(void);
void cddbp_close(void);
void asy_remap(char *);
void copy_coord(char *, coord_t *);
void cvt_time(time_t, char *);
void db_free(db_t *);
void db_link(db_t *, char *, char *, int);
void db_strcpy(db_t *, int, int, char *, int);
void db_unlink(char *, char *);
void do_cddb_query_fuzzy(arg_t *);
void endsiteent(void);
void endsitegrp(void);
void fp_copy(FILE *, FILE *);
void fp_copy_init(void);
void generate_key(void);
void generate_pwd(void);
void get_rmt_hostname(int, char *, char *);
void quit(int);
void setsiteent(void);
void sighand(int);
void smtp_close(void);
void strip_crlf(char *);
void fmatch_free(void *p);
void convert_db_charset(db_t *);
void convert_db_charset_proto(db_t *, int);

int asy_encode(char *, char **);
int asy_mappable(unsigned char);
int *cddbd_count(void);
int categ_index(char *);
int cddbd_async_command(char *, int);
int cddbd_charcasecmp(char *, char *);
int cddbd_count_history(char *);
int cddbd_close_history(void);
int cddbd_elapse(int);
int cddbd_fix_file(char *, int, uid_t, gid_t);
int cddbd_fork(void);
int cddbd_getchar(void);
int cddbd_link(char *, char *);
int cddbd_lock(clck_t *, int);
int cddbd_merge_ulist(char *, int);
int cddbp_put(FILE *, char *);
int cddbd_strcasecmp(char *, char *);
int cddbd_strncasecmp(char *, char *, int);
int cddbd_timer_sleep(ctimer_t *);
int cddbd_write_ulist(char *, char *);
int cddbp_log_stats(void);
int cddbp_command(char *, int);
int cddbp_open(csite_t *);
int cddbp_setproto(void);
int cddbp_transmit(db_t *, char *, unsigned int, int);
int cddbp_update(void);
int ck_client_perms(char *, char *, int);
int crc_email(uint32_t *, FILE *, char *, char *, char *);
int db_post(db_t *, char *, unsigned int, char *);
int db_strip(db_t *);
#ifdef DB_WINDOWS_FORMAT
int db_write(FILE *fp, db_t *, int level, unsigned int discid);
#else
int db_write(FILE *fp, db_t *, int level);
#endif
int get_serv_index(char *);
int fp_copy_buf(FILE *, char *);
int is_blank(char *, int);
int is_crlf(int);
int is_dot(char *);
int is_dbl_dot(char *);
int is_fuzzy_match(int *, int *, int, int, int);
int is_instr(char, char *);
int is_DblHash(char *);
int is_parent_dir(char *);
int is_qp_hex(unsigned char);
int is_rfc_1521_mappable(unsigned char *, int, int);
int is_rfc_1521_print(unsigned char);
int is_space(char);
int is_valid_ascii(char *);
int is_valid_latin1(char *);
int is_valid_utf8(char *);
int is_wspace(int);
int match_host(char *);
int octet_to_char(unsigned char *, char);
int read_wait(int);
int rfc_1521_base64_encode(int, unsigned char *, unsigned char **, int *);
int rfc_1521_base64_decode(int, unsigned char *, unsigned char **, int *);
int rfc_1521_qp_encode(int, unsigned char *, unsigned char **, int *);
int rfc_1521_qp_decode(int, unsigned char *, unsigned char **, int *);
int setsitegrp(char *);
int smtp_command(csite_t *, char *, int, char *, char *);
int smtp_open(void);
int smtp_transmit(FILE *, int, char *, char *, char *, char *, char *, int, int,
    		  ct_key_t *);
int validate_email(FILE *, email_hdr_t *, char *);
int return_mail(FILE *, email_hdr_t *, int, char *);
int parse_utf8(const char **ps);


struct tm *date_to_tm(char *);

char *cddbd_dirname(char *);
#ifdef DB_WINDOWS_FORMAT
char *cddbd_basename(char *path);
#endif
char *cddbd_gets(char *, int);
char *cddbd_mktemp(void);
char *cddbd_strcasestr(char *, char *);
char *char_to_octet(unsigned char, char);
char *get_time(int);
char *keytostr(ct_key_t *);
char *make_time(struct tm *);
char *make_time2(struct tm *);
char *crctostr(uint32_t, uint32_t, ct_key_t *);

long cddbd_rand(void);

short get_serv_port(int, int);

uint32_t strtocrc(char *, uint32_t, ct_key_t *);

ct_key_t *strtokey(char *);
ct_key_t *getpasswd(char *);

csite_t *getsiteent(int);
csite_t *getsitenam(char *, int);

lhead_t *fuzzy_search(int, int *, int);

FILE * db_prepare_unix_read(char *);
int db_skip(FILE *, char *);

db_t *db_read(FILE *, char *, int);

clck_t *cddbd_lock_alloc(char *);

hperm_t *ck_host_perms(int);

/* crc.c */
int crc32(int, void *, uint32_t *, uint32_t *);



/* External definitions. */
extern unsigned int db_curr_discid;
extern int categcnt;
extern int cgi;
extern int db_dir_mode;
extern int db_errno;
extern int db_file_mode;
extern int delay_time;
extern int debug; /* need to export it as well */
extern int dir_mode;
extern int dup_ok;
extern int dup_policy;
extern int elapse_time;
extern int email_time;
extern int file_charset;
extern int file_df_flags;
extern int file_mode;
extern int fuzzy_div;
extern int fuzzy_factor;
extern int hdrlen;
extern int interface;
extern int max_lines;
extern int max_xmits;
extern int prclen;
extern int strip_ext;
extern int sublen;
extern int trklen;
extern int utf_as_iso;
extern int verbose;
extern int xmit_time;

extern char *asy_prefix[];
extern char *boundary;
extern char *categlist[];
extern char *cddb_help[];
extern char *charset;
extern char *content_encoding;
extern char *content_len;
extern char *content_type;
extern char *db_errmsg[];
extern char *expires;
extern char *from;
extern char *get_help[];
extern char *hdrstr;
extern char *hello_help[];
extern char *help_help[];
extern char *help_info[];
extern char *log_help[];
extern char *lscat_help[];
extern char *mime_ver;
extern char *motd_help[];
extern char *multi_alt;
extern char *prcstr;
extern char *proto_help[];
extern char *put_help[];
extern char *query_help[];
extern char *quit_help[];
extern char *read_help[];
extern char *rpath;
extern char *saltstr;
extern char *secure_users[];
extern char *servfile[];
extern char *sites_help[];
/* extern char *srch_help[]; */
extern char *stat_help[];
extern char *subj;
extern char *substr;
extern char *text_plain;
extern char *to;
extern char *trkstr;
extern char *unlink_help[];
extern char *update_help[];
extern char *validate_help[];
extern char *ver_help[];
extern char *verstr2;
extern char *whom_help[];
extern char *discid_help[];
extern char *write_help[];
extern char *x_cddbd_crc;
extern char *x_cddbd_echo;
extern char *x_cddbd_from;
extern char *x_sender;
extern char admin_email[];
extern char bounce_email[];
extern char cddbdir[];
extern char dupdir[];
extern char host[];
extern char postdir[];
extern char pwdfile[];
extern char rhost[];
extern char sitefile[];
extern char smtphost[];
extern char test_email[];
extern char user[];
extern char workdir[];

extern gid_t db_gid;
extern gid_t gid;

extern uid_t db_uid;
extern uid_t uid;

extern clck_t *locks[LK_NUMLOCKS];
#ifdef DB_WINDOWS_FORMAT
extern clck_t ***categlocks;
#endif

extern proto_t proto_flags[];
extern ctimer_t timers[];
extern encode_t encoding_types[];
extern charset_t charsets[];

extern hperm_t hperm;

/* String charset conversion functions defined in charset.c. */
void charset_latin1_utf8(const char *s, char **p);
int charset_utf8_latin1(const char *s, char **p);
int charset_is_utf8(const char *s);
int charset_is_valid_utf8(const char *s);
int charset_is_valid_ascii(const char *s);
int charset_is_valid_latin1(const char *s);

/* DB charset conversion functions defined in db.c. */
int db_utf8_latin1(db_t *db);
int db_utf8_latin1_exact(db_t *db);
void db_latin1_utf8(db_t *db);
int db_looks_like_utf8(db_t *db);
int db_disam_charset(db_t *db);

#endif
