/*
 *   cddbd - CD Database Protocol Server
 *
 *   Copyright (C) 1996-1997  Steve Scherf (steve@moonsoft.com)
 *   Portions Copyright (C) 2001-2003  by various authors
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
static char *const _inet_c_ident_ = "@(#)$Id: inet.c,v 1.26 2004/08/30 19:46:19 megari Exp $";
#endif

#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/time.h>
#include "patchlevel.h"
#include "access.h"
#include "cddbd.h"

#ifndef INADDR_NONE
/* Solaris does not know INADDR_NONE */
#define INADDR_NONE     -1
#endif

/* Structure definitions. */

/* Values for si_flags. */
#define SF_NOADDR	0x000000001
#define SF_NOHOST	0x000000002

/* Values for si_match. */
#define SI_MATCH_NONE		0
#define SI_MATCH_DEFAULT	1
#define SI_MATCH_DOMAIN		2
#define SI_MATCH_NET		3
#define SI_MATCH_HOST		4

/* Connected host socket info. */
typedef struct sockinfo {
	int si_flags;
	int si_match;
	int si_domain_len;
	char si_hname[CDDBHOSTNAMELEN + 1];
	struct in_addr si_haddr;
} sinfo_t;


/* Prototypes. */

int comp_domain(char *, char *);
int comp_ip(char *, char *);
int domain_len(char *);
int isipaddr(char *);
int read_waitsecs(int, int);
int sock_getc(void);
int sock_open(char *, int, int, char *);
int sock_read(char *, int, int);
int sock_serv_read(char *, int, int, char *);
int sock_wait(void);
int sock_write(char *, int, int);
int smtp_write_body(FILE *, int, int, char *);
int smtp_write_bound(char *, int);
int smtp_write_hdr(char *);
int smtp_write_hdr_crc(char *);
int smtp_write_hdr_enc(int);
int smtp_write_hdr_type(char *, char *, int);

void chldhand(int);
void sock_close(void);
extern void cddbd_translate(char *, char, char);

static char *cmd_send_subj2 = "cddb #command %.16s";    /* originally from mail.c */

/* Global variables. */

static int sock_i;
static int sock_e;
static int sock_fd = -1;
static int fp_isend = 1;

static FILE *sock_fp;

static sinfo_t sinfo;

static char cddbphost[CDDBHOSTNAMELEN + 1];

/* Default services table. */
static cservent_t servtab[] = {
	{ "cddbp",	"tcp",		8880 },
	{ "smtp",	"tcp",		25 },
	{ "http",	"tcp",		80 },
	{ 0 }
};


void
get_rmt_hostname(int fd, char *thost, char *rh)
{
	char *h;
	char *a;
	socklen_t len;
	struct hostent *hp;
	struct sockaddr_in name;
#ifdef DONT_RESOLVE_ADDRESS
	struct hostent he;
#endif

	len = (socklen_t)sizeof(name);
	memset(&name, 0, len); /* need to be sure it is "initialised" */

	switch(interface) {
	case IF_HTTP:
		/* If no remote address, something's very wrong. */
		if((a = (char *)getenv("REMOTE_ADDR")) == NULL) {
			sinfo.si_flags |= SF_NOADDR;
			hp = 0;
		}
		else {
			sinfo.si_haddr.s_addr = inet_addr(a);
#ifdef DONT_RESOLVE_ADDRESS
			he.h_name = a;
			hp = &he;
#else
			hp = gethostbyaddr((char *)&sinfo.si_haddr,
			    sizeof(sinfo.si_haddr), AF_INET);
#endif
		}

		break;

	case IF_CDDBP:
		/* If we can't get the peername, this is a tty or file. */
		if(getpeername(fd, (struct sockaddr *)&name, &len) < 0) {
			sinfo.si_flags |= SF_NOADDR;
			hp = gethostbyname(host);
		}
		else {
			sinfo.si_haddr = name.sin_addr;
			hp = gethostbyaddr((char *)&name.sin_addr,
			    sizeof(name.sin_addr), AF_INET);
		}

		break;

	case IF_EMAIL:
	case IF_SUBMIT:
	case IF_NONE:
	default:
		/* In this case, the address may be in the args. */
		sinfo.si_flags |= SF_NOADDR;
		hp = 0;
		break;
	}

	if(hp == 0) {
		if(thost != 0)
			h = thost;
		else {
			sinfo.si_flags |= SF_NOHOST;
			if(sinfo.si_flags & SF_NOADDR)
				h = LHOST;
			else
				h = inet_ntoa(name.sin_addr);
		}
	}
	else
		h = (char *)hp->h_name;

	strncpy(sinfo.si_hname, h, CDDBHOSTNAMELEN);
	sinfo.si_hname[CDDBHOSTNAMELEN] = '\0';
	strncpy(rh, h, CDDBHOSTNAMELEN);
	rh[CDDBHOSTNAMELEN] = '\0';
}


int
match_host(char *ahost)
{
	char *p;
	char **ap;
	unsigned long in;
	struct hostent *hp;

	#ifndef __CYGWIN32__
	char net[CDDBBUFSIZ];
	struct netent *np;	
	struct in_addr neta;	
	#endif /* __CYGWIN32__ */


	/* Try and match the default definition. */
	if(!strcmp(ahost, "default")) {
		if(sinfo.si_match <= SI_MATCH_DEFAULT) {
			sinfo.si_match = SI_MATCH_DEFAULT;
			return 1;
		}

		return 0;
	}

	/* Try and match the domain. */
	p = ahost + 2;

	/* Check even if we already have a domain match. */
	if(sinfo.si_match <= SI_MATCH_DOMAIN && !strncmp("*.", ahost, 2) &&
	    (sinfo.si_domain_len <= domain_len(p)) && !(sinfo.si_flags &
	    SF_NOHOST) && !isipaddr(sinfo.si_hname)) {
		hp = gethostbyname(sinfo.si_hname);

		if(hp == 0) {
			/* Compare the primary host name with the domain. */
			if(!comp_domain((char *)sinfo.si_hname, p)) {
				sinfo.si_domain_len = domain_len(p);
				sinfo.si_match = SI_MATCH_DOMAIN;
				return 1;
			}
		}
		else {
			/* Compare the primary host name with the domain. */
			if(!comp_domain((char *)hp->h_name, p)) {
				sinfo.si_domain_len = domain_len(p);
				sinfo.si_match = SI_MATCH_DOMAIN;
				return 1;
			}

			/* Compare the secondary host names with the domain. */
			for(ap = hp->h_aliases; *ap; ap++)
				if(!comp_domain(*ap, p)) {
					sinfo.si_domain_len = domain_len(p);
					sinfo.si_match = SI_MATCH_DOMAIN;
					return 1;
				}
		}
	}

	/* If we know this is a domain search, return. */
	if(!strncmp("*.", ahost, 2))
		return 0;

	/* Try and match the host. */
	if(sinfo.si_match < SI_MATCH_HOST && !(sinfo.si_flags & SF_NOHOST)) {
		if(isipaddr(ahost)) {
			/* Try and match the host (IP format). */
			if(!(sinfo.si_flags & SF_NOADDR) &&
			    !strcmp(inet_ntoa(sinfo.si_haddr), ahost)) {
				sinfo.si_match = SI_MATCH_HOST;
				return 1;
			}

			in = inet_addr(ahost);
			hp = gethostbyaddr((char *)&in, sizeof(in),
			    AF_INET);
		}
		else
			hp = gethostbyname(ahost);

		if(hp != 0) {
			/* Check the primary host name. */
			if(!strcmp(sinfo.si_hname, hp->h_name)) {
				sinfo.si_match = SI_MATCH_HOST;
				return 1;
			}

			/* Check the secondary host names. */
			for(ap = hp->h_aliases; *ap; ap++)
				if(!strcmp(sinfo.si_hname, *ap)) {
					sinfo.si_match = SI_MATCH_HOST;
					return 1;
				}
		}
	}

	/* Try to match the network. */
	if(sinfo.si_match < SI_MATCH_NET && !(sinfo.si_flags & SF_NOADDR)) {
		/* Try and match the network (IP format). */
		if(!comp_ip(inet_ntoa(sinfo.si_haddr), ahost)) {
			sinfo.si_match = SI_MATCH_NET;
			return 1;
		}
#ifndef __CYGWIN32__
		/* Try to match the net name. */
		np = getnetbyname(ahost);

		if(np != 0) {
			neta.s_addr = htonl(np->n_net);
			strcpy(net, inet_ntoa(neta));

			if(!comp_ip(inet_ntoa(sinfo.si_haddr), net)) {
				sinfo.si_match = SI_MATCH_NET;
				return 1;
			}
		}
#endif /* __CYGWIN32__ */
	}

	return 0;
}


int
comp_ip(char *ip, char *domain)
{
	int len;
	char *p;
	char tdom[CDDBBUFSIZ];
	
	if(!isipaddr(ip) || !isipaddr(domain))
		return 1;

	len = strlen(domain);
	if(len >= sizeof(tdom))
		return 1;

	strcpy(tdom, domain);

	p = &tdom[len];
	do {
		p--;

		while(p != tdom && *p != '.') {
			if(*p != '0')
				return(strncmp(ip, tdom, strlen(tdom)));
			p--;
		}

		*p = '\0';
	} while(p != tdom);

	return 1;
}


int
comp_domain(char *host, char *domain)
{
	int len;

	len = strlen(host) - strlen(domain);
	if(len < 0)
		return 1;

	if(len > 0) {
		host = &host[len];
		if(*(host - 1) != '.')
			return 1;
	}

	return(strcmp(host, domain));
}


int
domain_len(char *domain)
{
	int len;

	while(*domain == '.')
		domain++;

	if(*domain == '\0')
		return 0;

	for(len = 1; *domain != '\0'; domain++)
		if(*domain == '.')
			len++;

	return len;
}


int
isipaddr(char *ip)
{
	int i;

	for(i = 0; i < 4 && *ip != '\0'; i++) {
		while(*ip != '\0' && isdigit(*ip))
			ip++;

		if(*ip != '\0') {
			if(*ip == '.' && i < 3)
				ip++;
			else
				break;
		}
	}

	if(i == 4 && *ip == '\0')
		return 1;

	return 0;
}


void
cddbd_stand(int port)
{
	int s;
	int sock;
	int opt;
	socklen_t len;
	pid_t f;
	struct sockaddr_in sin;
	struct sockaddr_in from;

	/* Read the access file. */
	cddbd_check_access();

	sin.sin_port = get_serv_port(port, SERV_CDDBP);

	if((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		exit(QUIT_ERR);
	}

	opt = 1;
	if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)))
		fprintf(stderr,
		    "Warning: setsockopt(SO_REUSEADDR) failed (%d).", errno);

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);

	if(bind(s, (struct sockaddr *)&sin, sizeof(sin))) {
		perror("bind");
		exit(QUIT_ERR);
	}

	if(listen(s, 5)) {
		perror("listen");
		exit(QUIT_ERR);
	}

	signal(SIGCHLD, chldhand);

	f = cddbd_fork();
	if(f < 0) {
		perror("fork");
		exit(QUIT_ERR);
	}

	if(f > 0)
		exit(QUIT_OK);

	if(verbose) {
		printf("Running standalone at port %d.\n", port);
		fflush(stdout);
	}

	if(setsid() == -1)
		fprintf(stderr, "Warning: Can't set process group.\n");

	len = (socklen_t)sizeof(from);

	for(;;) {
		/* Wait for connections or SIGHUP to reread the access file. */
		signal(SIGHUP, sighand);
		sock = accept(s, (struct sockaddr *)&from, &len);
		signal(SIGHUP, SIG_IGN);

		if(sock < 0) {
			if(errno == EINTR || errno == ECHILD) /* "No children" is no real error state! */
				continue;

			perror("accept");
			quit(QUIT_ERR);
		}

		f = cddbd_fork();

		if(f < 0) {
			perror("fork");
			quit(QUIT_ERR);
		}

		/* We're the child - crank. */
		if(f == 0) {
			dup2(sock, 0);
			close(sock);
			return;
		}

		close(sock);
	}
}


int
cddbp_open(csite_t *sp)
{
	int port;
	ct_key_t *key;
	uint32_t salt;
	char *p;
	char *site;
	char buf[CDDBBUFSIZ];
	char errstr[CDDBBUFSIZ];

	site = sp->st_name;
	port = sp->st_port;

	if(verbose)
		cddbd_log(LOG_INFO, "Opening server: %s", site);

	if(!sock_open(site, SERV_CDDBP, port, errstr)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to open %s server: %s.", site, errstr);
		return 0;
	}

	if(!sock_read(buf, sizeof(buf), 1)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to init %s server: connection closed.", site);
		sock_close();
		return 0;
	}

	/* Good codes start with 2. */
	if(buf[0] != '2') {
		strip_crlf(buf);
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to init %s server: unexpected server "
		    "response: %s.", site, buf);

		sock_close();
		return 0;
	}

	cddbd_snprintf(buf, sizeof(buf), "cddb hello %s %s cddbd %sPL%d\n",
	    user, host, VERSION, PATCHLEVEL);

	if(!sock_write(buf, 0, 1)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to handshake with %s server (%s).",
		    site, errno);
		sock_close();
		return 0;
	}

	if(!sock_serv_read(buf, sizeof(buf), 200, errstr)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to open %s server: %s", site, errstr);

		sock_close();
		return 0;
	}

	strncpy(cddbphost, site, sizeof(cddbphost));
	cddbphost[sizeof(cddbphost) - 1] = '\0';

	/* If we don't need to validate, return now. */
	if(!(sp->st_flags & ST_FLAG_PWDLBL))
		return 1;

	if((key = getpasswd(sp->st_pwdlbl)) == NULL) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "No password label \"%s\" found for site: %s",
		    sp->st_pwdlbl, sp->st_name);

		sock_close();
		return 0;
	}

	if(!sock_write("validate\n", 0, 1)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to perform validation with server: %s",
		    site);

		sock_close();
		return 0;
	}

	if(!sock_serv_read(buf, sizeof(buf), 320, errstr)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to perform validation with server: %s",
		    site);

		sock_close();
		return 0;
	}
	
	if((p = strstr(buf, saltstr)) == NULL) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Malformed %s response from %s server: %s", site, buf);

		(void)sock_write("\n", 0, 1);
		sock_close();
		return 0;
	}

	p += strlen(saltstr);

	if(sscanf(p, "%08x", &salt) != 1) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Malformed %s response from %s server: %s", site, buf);

		(void)sock_write("\n", 0, 1);
		sock_close();
		return 0;
	}

	p = crctostr(CT_PASSWD, salt, key);

	cddbd_snprintf(buf, sizeof(buf), "%s\n", p);

	if(!sock_write(buf, 0, 1)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to perform validation with server: %s",
		    site);

		sock_close();
		return 0;
	}

	if(!sock_serv_read(buf, sizeof(buf), 200, errstr)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to perform validation with server: %s",
		    site);

		sock_close();
		return 0;
	}

	return 1;
}


int
cddbp_command(char *command, int lev)
{
	char *p;
	char code[4];
	char buf[CDDBBUFSIZ];
	/* char errstr[CDDBBUFSIZ]; */

	cddbd_snprintf(buf, sizeof(buf), "proto %d\n", lev);

	if(!sock_write(buf, 0, 1)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to set protocol level (%d) on %s server (%d)",
		    lev, cddbphost, errno);
		return 0;
	}

	if(!sock_read(buf, sizeof(buf), 1) ||
	    (sscanf(buf, "%03s", code) != 1) ||
	    (strcmp(code, "201") && strcmp(code, "502"))) {
		strip_crlf(buf);
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to set protocol level (%d) on %s server: %s",
		    lev, cddbphost, buf);

		return 0;
	}

	cddbd_snprintf(buf, sizeof(buf), "%s\n", command);

	if(!sock_write(buf, 0, 1)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to send command \"%s\" to %s server (%d)",
		    command, cddbphost, errno);
		return 0;
	}

	if(!sock_read(buf, sizeof(buf), 1) ||
	    (sscanf(buf, "%03s", code) != 1) || code[0] != '2') {
		strip_crlf(buf);
		cddbd_log(LOG_ERR | LOG_NET,
		    "Unexpected response from server %s for command \"%s\": %s",
		    cddbphost, command, buf);

		return 0;
	}

	switch(code[1]) {
	case '0':
		p = buf;

		while(*p != '\0' && !isspace(*p))
			p++;
		while(*p != '\0' && isspace(*p))
			p++;

		strip_crlf(p);
		printf("%s\n", p);

		return 1;

	case '1':
		while(sock_read(buf, sizeof(buf), 0)) {
			if(is_dot(buf))
				return 1;

			p = buf;

			if(is_dbl_dot(p))
				p++;

			strip_crlf(p);
			printf("%s\n", p);
		}

		return 0;

	default:
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to send command \"%s\" to %s server: %s (%d)",
		    command, cddbphost, code, errno);

		return 0;
	}
}


int
cddbp_log_stats(void)
{
	char *p;
	char buf[CDDBBUFSIZ];
	char errstr[CDDBBUFSIZ];

	/* Try to get a long list first, but not all servers support this. */
	if(!sock_write("log -l 200 day\n", 0, 1)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to send log command to %s server (%d)",
		    cddbphost, errno);
		return 0;
	}

	/* If it didn't work, try a more generic log command. */
	if(!sock_serv_read(buf, sizeof(buf), 210, errstr)) {
		if(!sock_write("log day\n", 0, 1)) {
			cddbd_log(LOG_ERR | LOG_NET,
			    "Failed to send log command to %s server (%d)",
			    cddbphost, errno);
			return 0;
		}

		if(!sock_serv_read(buf, sizeof(buf), 210, errstr)) {
			cddbd_log(LOG_ERR | LOG_NET,
			    "Failed to get log stats from %s server: %s",
			    cddbphost, errstr);
			return 0;
		}
	}

	while(sock_read(buf, sizeof(buf), 0)) {
		if(is_dot(buf))
			return 1;

		p = buf;

		if(is_dbl_dot(p))
			p++;

		strip_crlf(p);
		printf("%s\n", p);
	}

	cddbd_log(LOG_ERR | LOG_NET,
	    "Failed to get log stats from %s server: "
	    "connection closed (%d)", cddbphost, errno);

	return 0;
}

/* determines the highest common protocol level between local and remote     */
/* and sets the protocol level on the remote accordingly. Returns that level */
int
cddbp_setproto(void) {
	char buf[CDDBBUFSIZ];
	char errstr[CDDBBUFSIZ];
	int max_send_proto; /* highest protocol level the remote server understands */
	int use_proto; /* protocol level to be used for transmission */

	/* let's find out the highest protocol level the remote server supports */
	if(!sock_write("proto\n", 0, 1)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to send proto command to %s server.", cddbphost);
		sock_close();
		return 0;
	}

	if(!sock_serv_read(buf, sizeof(buf), 200, errstr)) {
		cddbd_log(LOG_ERR | LOG_NET,
			"Failed to send proto command to %s server: %s.",cddbphost, errstr);
		sock_close();
		return 0;
	}
	
	if(sscanf(buf, "200 CDDB protocol level: current %*d, supported %d", &max_send_proto) != 1) {
			cddbd_log(LOG_ERR | LOG_NET,
			"Malformed response to proto command: %s",
			buf);
		sock_close();
		return 0;
	}
	
	/***************************************************************/
	/* highest protocol level of remote determined - compare to    */
	/* the hightest level we support and determine highest common  */
	/* protocol level                                              */
	/***************************************************************/
	if (MAX_PROTO_LEVEL > max_send_proto)
		use_proto=max_send_proto;
	else
		use_proto=MAX_PROTO_LEVEL;
		
	/* set the protocol level on remote server */
	
	cddbd_snprintf(buf, sizeof(buf), "proto %d\n", use_proto);
	if(!sock_write(buf, 0, 1)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to set protocol level on %s server.",cddbphost);
		sock_close();
		return 0;
	}
	
	if(!sock_serv_read(buf, sizeof(buf), 201, errstr)) {
		cddbd_log(LOG_ERR | LOG_NET,
			"Failed to set protocol level on %s server: %s.",cddbphost, errstr);
		sock_close();
		return 0;
	}
	return use_proto;
}

int
cddbp_transmit(db_t *db, char *category, unsigned int discid, int use_proto)
{
	char buf[CDDBBUFSIZ];
	char errstr[CDDBBUFSIZ];
	
	/* convert charset of entry to be transmitted according to used protocol level */
	convert_db_charset_proto(db,use_proto);
	
	cddbd_snprintf(buf, sizeof(buf), "cddb write %s %08x\n",
	    category, discid);

	if(!sock_write(buf, 0, 1)) {
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Failed to send write command to %s server (%s).",
		    cddbphost, errno);
		return 0;
	}

	if(!sock_serv_read(buf, sizeof(buf), 320, errstr)) {
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Failed to send write command to %s server: %s.",
		    cddbphost, errstr);
		return 0;
	}

#ifdef DB_WINDOWS_FORMAT
	if(!db_write(sock_fp, db, use_proto, 0)) {
#else
	if(!db_write(sock_fp, db, use_proto)) {
#endif
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Failed to write DB entry to %s server (%d).",
		    cddbphost, errno);
		return 0;
	}

	fflush(sock_fp);

	if(!sock_write(".\n", 0, 1)) {
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Failed to write DB entry to %s server (%d).",
		    cddbphost, errno);
		return 0;
	}

	buf[0] = '\0';

	if(!sock_serv_read(buf, sizeof(buf), 200, errstr)) {
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Failed to write DB entry to %s server: %s.",
		    cddbphost, errstr);

		/* Was the DB entry bad, or was there a server error? */
		if(!strncmp(buf, "501", 3))
			return -1;
		else
			return 0;
	}

	return 1;
}


int
cddbp_put(FILE *fp, char *file)
{
	char buf[CDDBBUFSIZ];
	char errstr[CDDBBUFSIZ];

	cddbd_snprintf(buf, sizeof(buf), "put %s\n", file);

	if(!sock_write(buf, 0, 1)) {
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Failed to send put command to %s server (%s).",
		    cddbphost, errno);
		return 0;
	}

	if(!sock_serv_read(buf, sizeof(buf), 320, errstr)) {
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Failed to send put command to %s server: %s",
		    cddbphost, errstr);
		return 0;
	}

	fp_copy(fp, sock_fp);
	fflush(sock_fp);
	
	if(!sock_write(".\n", 0, 1)) {
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Failed to write %s to %s server (%d).",
		    file, cddbphost, errno);
		return 0;
	}

	buf[0] = '\0';

	if(!sock_serv_read(buf, sizeof(buf), 200, errstr)) {
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Failed to write %s to %s server: %s.",
		    file, cddbphost, errstr);

		/* Was the DB entry bad, or was there a server error? */
		if(!strncmp(buf, "501", 3))
			return -1;
		else
			return 0;
	}

	return 1;
}


int
cddbp_update(void)
{
	int err;
	int code;
	char buf[CDDBBUFSIZ];
	char errstr[CDDBBUFSIZ];

	if(!sock_write("update\n", 0, 1)) {
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Failed to initiate update on %s server (%d).",
		    cddbphost, errno);
		return 0;
	}

	if(!sock_serv_read(buf, sizeof(buf), 200, errstr)) {
		/* If we're denied permission, that's okay. */
		if((sscanf(buf, "%d", &code) != 1) || code != 401)
			err = LOG_ERR | LOG_XMIT;
		else
			err = LOG_INFO;

		cddbd_log(err, "Failed to initiate update on %s server: %s.",
		    cddbphost, errstr);

		return 0;
	}

	return 1;
}


void
cddbp_close(void)
{
	(void)sock_write("quit\n", 0, 1);
	sock_close();
}


int
smtp_open()
{
	int code;
	char buf[CDDBBUFSIZ];
	char errstr[CDDBBUFSIZ];

	if(verbose)
		cddbd_log(LOG_INFO, "Opening SMTP server: %s", smtphost);

	if(!sock_open(smtphost, SERV_SMTP, 0, errstr)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to open %s SMTP server: %s", smtphost, errstr);
		return 0;
	}

	if(!sock_serv_read(buf, sizeof(buf), 220, errstr)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to init %s SMTP server: %s", smtphost, errstr);
		sock_close();
		return 0;
	}

	if(!sock_write("helo\n", 0, 1)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to handshake with %s SMTP server (%s)",
		    smtphost, errno);

		sock_close();
		return 0;
	}

	if(!sock_serv_read(buf, sizeof(buf), 0, errstr)) {
		if(sscanf(buf, "%d", &code) != 1) {
			cddbd_log(LOG_ERR | LOG_NET,
			    "Failed to handshake with %s SMTP "
			    "server: %s", smtphost, errstr);

			sock_close();
			return 0;
		}

		if(code == 250)
			return 1;
	}

	/* Try the helo with an argument to see if that helps. */
	cddbd_snprintf(buf, sizeof(buf), "helo %s\n", host);

	if(!sock_write(buf, 0, 1)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to handshake with %s SMTP server (%s)",
		    smtphost, errno);

		sock_close();
		return 0;
	}

	if(!sock_serv_read(buf, sizeof(buf), 0, errstr)) {
		if(sscanf(buf, "%d", &code) != 1 || code != 250) {
			cddbd_log(LOG_ERR | LOG_NET,
			    "Failed to handshake with %s SMTP "
			    "server: %s", smtphost, errstr);

			sock_close();
			return 0;
		}
	}

	return 1;
}


void
smtp_close(void)
{
	(void)sock_write("quit\n", 0, 1);
	sock_close();
}


/* return values: 0..error; 1..ok */
int
smtp_command(csite_t *sp, char *rcmd, int lev, char *hto, char *mfrom)
{
	FILE *fp;
	char *tcmd;
	char *tfile;
	char buf[CDDBBUFSIZ];
	char buf2[CDDBBUFSIZ];
	ct_key_t *key;

	tfile = cddbd_mktemp();

	if((fp = fopen(tfile, "w+")) == NULL) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to open command temp file: %s (%d)", tfile, errno);

		cddbd_freetemp(tfile);
		return 0;
	}

	if(!asy_encode(rcmd, &tcmd)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed encode command: %s (%d)", rcmd, errno);

		fclose(fp);
		cddbd_freetemp(tfile);

		return 0;
	}

	cddbd_translate(tcmd, ' ', '+');

	if(fprintf(fp, "cmd=%s&hello=%s+%s+cddbd+%sPL%d&proto=%d\n",
	    tcmd, user, host, VERSION, PATCHLEVEL, lev) <= 0) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to open command temp file: %s (%d)", tfile, errno);

		fclose(fp);
		cddbd_freetemp(tfile);

		return 0;
	}

	fflush(fp);
	rewind(fp);

	if(sp->st_flags & ST_FLAG_PWDLBL) {
		if((key = getpasswd(sp->st_pwdlbl)) == NULL) {
			cddbd_log(LOG_ERR | LOG_XMIT, "No password "
			    "label \"%s\" found for site: %s",
			    sp->st_pwdlbl, sp->st_name);

			fclose(fp);
			cddbd_freetemp(tfile);

			return 0;
		}
	}
	else
		key = 0;

	cddbd_snprintf(buf2, sizeof(buf2), "cmd_%08x", cddbd_rand());
	cddbd_snprintf(buf, sizeof(buf), cmd_send_subj2, buf2);

	if(!smtp_transmit(fp, -1, buf, hto, hto, mfrom, 0, MF_MULTI, 0, key)) {
		cddbd_log(LOG_ERR | LOG_XMIT,
		    "Failed to send command \"%s\" to %s server (%d)",
		    rcmd, smtphost, errno);

		fclose(fp);
		cddbd_freetemp(tfile);

		return 0;
	}

	fclose(fp);
	cddbd_freetemp(tfile);
	return 1;
}


int
smtp_transmit(FILE *fp, int set, char *subject, char *rcpt, char *hto, char *mfrom,
    char *echo, int flags, int len, ct_key_t *key)
{
	char *crcstr;
	char buf[CDDBBUFSIZ];
	char bound[CDDBBUFSIZ];
	char errstr[CDDBBUFSIZ];
	uint32_t crc;

	cddbd_snprintf(buf, sizeof(buf), "mail from: %s\n", mfrom);

	if(crc_email(&crc, fp, admin_email, subject, errstr) != EE_OK) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to generate email crc: %s", errstr);

		return 0;
	}

	crcstr = crctostr(crc, 0, key);

	if(!sock_write(buf, 0, 1)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to specify return addr to %s SMTP server (%d)",
		    smtphost, errno);
		return 0;
	}

	if(!sock_serv_read(buf, sizeof(buf), 250, errstr)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to handshake with %s SMTP server: %s",
		    smtphost, errstr);
		return 0;
	}

	cddbd_snprintf(buf, sizeof(buf), "rcpt to: %s\n", rcpt);

	if(!sock_write(buf, 0, 1)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to specify recipient to %s SMTP server (%d)",
		    smtphost, errno);
		return 0;
	}

	if(!sock_serv_read(buf, sizeof(buf), 250, errstr)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to handshake with %s SMTP server: %s",
		    smtphost, errstr);
		return 0;
	}

	strcpy(buf, "data\n");

	if(!sock_write(buf, 0, 1)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to send data to %s SMTP server (%d)",
		    smtphost, errno);
		return 0;
	}

	if(!sock_serv_read(buf, sizeof(buf), 354, errstr)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed \"data\" command on %s SMTP server (%d)",
		    smtphost, errno);
		return 0;
	}

	cddbd_snprintf(buf, sizeof(buf), "%s: %s\n", rpath, mfrom);

	if(!smtp_write_hdr(buf))
		return 0;

	cddbd_snprintf(buf, sizeof(buf), "%s: %s\n", from, mfrom);

	if(!smtp_write_hdr(buf))
		return 0;

	cddbd_snprintf(buf, sizeof(buf), "%s: %s\n", x_cddbd_from, admin_email);

	if(!smtp_write_hdr(buf))
		return 0;

	if(echo != 0) {
		cddbd_snprintf(buf, sizeof(buf), "%s: %s\n", x_cddbd_echo,
		    echo);

		if(!smtp_write_hdr(buf))
			return 0;
	}

	cddbd_snprintf(buf, sizeof(buf), "%s: CDDBD v%sPL%d\n",
	    x_sender, VERSION, PATCHLEVEL);

	if(!smtp_write_hdr(buf))
		return 0;

	cddbd_snprintf(buf, sizeof(buf), "%s: %s\n", to, hto);

	if(!smtp_write_hdr(buf))
		return 0;

	cddbd_snprintf(buf, sizeof(buf), "%s: %s\n", subj, subject);

	if(!smtp_write_hdr(buf))
		return 0;

	cddbd_snprintf(buf, sizeof(buf), "%s: %s\n", mime_ver, "1.0");

	if(!smtp_write_hdr(buf))
		return 0;

	if(flags & MF_MULTI) {
		cddbd_snprintf(bound, sizeof(bound), "---=====cddbd_%08X%08X_",
		    cddbd_rand(), cddbd_rand());

		if(!smtp_write_hdr_type(multi_alt, bound, set))
			return 0;

		/* Separate the header from the body. */
		if(!sock_write("\n", 0, 0)) {
			cddbd_log(LOG_ERR | LOG_NET,
			    "Failed to write email header to %s SMTP server (%d)",
			    smtphost, errno);
			return 0;
		}

		if(!smtp_write_bound(bound, 0))
			return 0;

		if(!smtp_write_body(fp, CE_BASE64, set, crcstr))
			return 0;

		if(!smtp_write_bound(bound, 0))
			return 0;

		if(!smtp_write_body(fp, CE_QUOTED_PRINT, set, crcstr))
			return 0;

		if(!smtp_write_bound(bound, 1))
			return 0;
	}
	else if(flags & MF_ENC) {
		if(!smtp_write_body(fp, CE_QUOTED_PRINT, set, crcstr))
			return 0;
	}
	else {
		/* If we have a length, print it. */
		if(len > 0) {
			cddbd_snprintf(buf, sizeof(buf), "%s: %d\n",
			    content_len, len);

			if(!smtp_write_hdr(buf))
				return 0;
		}

		if(!smtp_write_body(fp, CE_7BIT, CC_US_ASCII, crcstr))
			return 0;
	}

	if(!sock_write(".\n", 0, 0)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to send data to %s SMTP server (%d)",
		    smtphost, errno);
		return 0;
	}

	if(!sock_serv_read(buf, sizeof(buf), 250, errstr)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to send data to %s SMTP server (%d)",
		    smtphost, errno);
		return 0;
	}

	return 1;
}


int
smtp_write_body(FILE *fp, int enc, int set, char *str)
{
	int op;
	int len;
	char *p;
	char *p2;
	char buf[CDDBBUFSIZ];

	/* in some cases, the file needs to be encoded */
	int encflg= 0;
	char *tfile= (char *) 0;
	FILE *tfp= (FILE *) 0;
	
	if(!smtp_write_hdr_type(text_plain, 0, set))
		return 0;

	if(!smtp_write_hdr_enc(enc))
		return 0;

	if(!smtp_write_hdr_crc(str))
		return 0;

	/* Separate the header from the body. */
	if(!sock_write("\n", 0, 0)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to write email header to %s SMTP server (%d)",
		    smtphost, errno);
		return 0;
	}

	/* Initiate encoding. */
	if(encoding_types[enc].en_encode != 0) {
		tfile = cddbd_mktemp();

		if((tfp = fopen(tfile, "w+")) == NULL) {
			cddbd_log(LOG_ERR | LOG_NET,
			    "Can't open SMTP tmp file %s (%d)", tfile, errno);

			cddbd_freetemp(tfile);
			return 0;
		}

		if((encoding_types[enc].en_encode)(EO_START, 0, 0, 0)
		    != EN_OK) {
			cddbd_log(LOG_ERR | LOG_NET,
			    "Failed to initiate email encoding.");

			cddbd_freetemp(tfile);
			fclose(tfp);

			return 0;
		}

		rewind(fp);

		do {
			p2 = fgets(buf, sizeof(buf), fp);

			if(p2 == NULL)
				op = EO_END;
			else {
				op = EO_ENCODE;
				len = strlen(buf);
			}

			if((encoding_types[enc].en_encode)(op,
			    (unsigned char *)buf, (unsigned char **)&p, &len)
			    != EN_OK) {
				cddbd_log(LOG_ERR | LOG_NET,
				    "Failed to perform email encoding.");

				cddbd_freetemp(tfile);
				fclose(tfp);

				return 0;
			}

			if(len > 0 && fwrite(p, 1, len, tfp) != len) {
				cddbd_log(LOG_ERR | LOG_NET,
				    "Can't write SMTP tmp file %s (%d)",
				    tfile, errno);

				cddbd_freetemp(tfile);
				fclose(tfp);

				return 0;
			}
		} while(p2 != NULL);

		encflg = 1;
		fflush(tfp);
		fp = tfp;
	}

	rewind(fp);

	while(fgets(buf, sizeof(buf), fp) != NULL) {
		if(read_waitsecs(sock_fd, 0) > 0) {
			if(!sock_read(buf, sizeof(buf), 0)) {
				cddbd_log(LOG_ERR | LOG_NET,
				    "Premature output from %s SMTP server.",
				    smtphost);
			}
			else {
				cddbd_log(LOG_ERR | LOG_NET,
				    "Premature output from %s SMTP server: %s",
				    smtphost, buf);
			}

			if(encflg) {
				cddbd_freetemp(tfile);
				fclose(tfp);
			}

			return 0;
		}

		len = strlen(buf);

		/* Escape standalone dots in the mail body. */
		if(is_dbl_dot(buf) && !sock_write(".", 0, 0)) {
			cddbd_log(LOG_ERR | LOG_NET,
			    "Failed to write mail to %s SMTP server "
			    "(%d)", smtphost, errno);

			if(encflg) {
				cddbd_freetemp(tfile);
				fclose(tfp);
			}

			return 0;
		}

		if(!sock_write(buf, 0, 0)) {
			cddbd_log(LOG_ERR | LOG_NET,
			    "Failed to write mail to %s SMTP server (%d)",
			    smtphost, errno);

			if(encflg) {
				cddbd_freetemp(tfile);
				fclose(tfp);
			}

			return 0;
		}
	}

	/* Make sure the last line ends with a newline. */
	if(buf[strlen(buf) - 1] != '\n' && !sock_write("\n", 0, 0)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to write mail to %s SMTP server (%d)",
		    smtphost, errno);

		if(encflg) {
			cddbd_freetemp(tfile);
			fclose(tfp);
		}

		return 0;
	}

	if(encflg) {
		cddbd_freetemp(tfile);
		fclose(tfp);
	}

	return 1;
}


int
smtp_write_hdr(char *hdr)
{
	if(!sock_write(hdr, 0, 0)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to write email header to %s SMTP server (%d)",
		    smtphost, errno);
		return 0;
	}

	return 1;
}


int
smtp_write_hdr_crc(char *str)
{
	char buf[CDDBBUFSIZ];

	cddbd_snprintf(buf, sizeof(buf), "%s: %s\n", x_cddbd_crc, str);

	return(smtp_write_hdr(buf));
}


int
smtp_write_hdr_enc(int enc)
{
	char buf[CDDBBUFSIZ];

	cddbd_snprintf(buf, sizeof(buf), "%s: %s\n", content_encoding,
	    encoding_types[enc].en_type);

	return(smtp_write_hdr(buf));
}


int
smtp_write_bound(char *bound, int end)
{
	char buf[CDDBBUFSIZ];

	if(end)
		cddbd_snprintf(buf, sizeof(buf), "--%s--\n", bound);
	else
		cddbd_snprintf(buf, sizeof(buf), "--%s\n", bound);

	if(!sock_write(buf, 0, 0)) {
		cddbd_log(LOG_ERR | LOG_NET,
		    "Failed to write mail boundary to %s SMTP server (%d)",
		    smtphost, errno);

		return 0;
	}

	return 1;
}


int
smtp_write_hdr_type(char *type, char *bound, int set)
{
	char buf[CDDBBUFSIZ];
	char *setname = (set == -1) ? "x-unknown" : charsets[set].name;

	if(bound != 0) {
		cddbd_snprintf(buf, sizeof(buf), "%s: %s; %s=%s;\n",
		    content_type, type, charset, setname);

		if(!smtp_write_hdr(buf))
			return 0;

		cddbd_snprintf(buf, sizeof(buf), "\t%s=\"%s\"\n",
		    boundary, bound);
	}
	else {
		cddbd_snprintf(buf, sizeof(buf), "%s: %s; %s=%s\n",
		    content_type, type, charset, setname);
	}

	return(smtp_write_hdr(buf));
}


int
sock_open(char *shost, int ind, int rport, char *errstr)
{
	int port;
	struct in_addr ad;
	struct hostent *hp;
	struct sockaddr_in sin;

	if(sock_fd >= 0)
		sock_close();

	port = get_serv_port(rport, ind);

	if((hp = gethostbyname(shost)) != 0) {
		cddbd_bcopy(hp->h_addr, (char *)&sin.sin_addr, hp->h_length);
	}
	else {
		/* T2D: inet_addr() should be replaced by inet_aton() which
		 * allows a cleaner error handling.
		 */
		if(isipaddr(shost) && (ad.s_addr = inet_addr(shost)) != INADDR_NONE) {
			cddbd_bcopy((char *)&ad.s_addr, (char *)&sin.sin_addr,
			    sizeof(ad.s_addr));
		}
		else {
			strcpy(errstr, "unknown host");
			return 0;
		}
	}

	sin.sin_family = AF_INET;
	sin.sin_port = (short)port;

	if((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		cddbd_snprintf(errstr, CDDBBUFSIZ, "can't allocate socket (%d)",
		     errno);
		return 0;
	}

	if(connect(sock_fd, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
		cddbd_snprintf(errstr, CDDBBUFSIZ, "connect failed (%d)",errno);
		close(sock_fd);
		sock_fd = -1;

		return 0;
	}

	if((sock_fp = fdopen(sock_fd, "r+")) == NULL) {
		cddbd_snprintf(errstr, CDDBBUFSIZ,
		    "failed to allocate FILE pointer (%d)", errno);
		close(sock_fd);
		return 0;
	}

	return 1;
}


void
sock_close(void)
{
	if(sock_fd < 0)
		return;

	shutdown(sock_fd, 2);
	fclose(sock_fp);

	sock_i = 0;
	sock_e = 0;
	sock_fd = -1;
}

/*********************************************************/
/* reads a server response from socket and checks if     */
/* the response code matches the expected code           */
/*-------------------------------------------------------*/
/* buf: array to be read to                              */
/* size: maximum number of bytes to be read from socket  */
/* expect: expected response code                        */
/* errstr: array to be filled with error message, if any */
/* returns: 1 on success (successful read                */
/*          & expected matched), 0 on error              */
/*-------------------------------------------------------*/
int
sock_serv_read(char *buf, int size, int expect, char *errstr)
{
	int code;
	char dash;

	while(sock_read(buf, size, 1)) {
		if((sscanf(buf, "%d%c", &code, &dash) != 2) ||
		    (code && code != expect)) {
			/* Don't overflow. Assume CDDBBUFSIZ error string. */
			strcpy(errstr, "unexpected server response: ");
			strip_crlf(buf);
			strncat(errstr, buf, (CDDBBUFSIZ - strlen(errstr)));
			errstr[CDDBBUFSIZ - 1] = '\0';

			return 0;
		}

		if(dash != '-')
			return 1;
	}

	cddbd_snprintf(errstr, CDDBBUFSIZ, "socket read error (%d)", errno);
	return 0;
}

/******************************************************/
/* writes a string to a socket                        */
/*----------------------------------------------------*/
/* buf: string to be written to the socket            */
/* cnt: maximum number of bytes to be written         */
/*      if cnt==0, then cnt is set to length of buf   */
/* flag: specifies, whether sent data shall be logged */
/* returns: 1 on success, 0 on error                  */
/*----------------------------------------------------*/
int
sock_write(char *buf, int cnt, int flag)
{
	int cc; /* number of bytes written to socket, -1 on error */
	char *p;
	char *pbuf;

	if(cnt == 0)
		cnt = strlen(buf);

	p = buf;

	/* write data to socket */
	while(cnt > 0) {
		cc = write(sock_fd, p, cnt);
		if(cc < 0) /* an error occurred writing to socket */
			return 0; 

		p += cc;
		cnt -= cc;
	}

	/* if in verbose mode and flag for logging set, log sent data */
	if(verbose && flag) {
		pbuf = strdup(buf);

		if(pbuf == NULL)
			cddbd_log(LOG_ERR, "Can't malloc pbuf.");
		else {
			strip_crlf(pbuf);
			cddbd_log(LOG_INFO, "-> %s", pbuf);
			free(pbuf);
		}
	}

	return 1;
}


/*******************************************************/
/* reads a string from socket                          */
/*-----------------------------------------------------*/
/* buf: array to be read to                            */
/* cnt: maximum number of bytes to be read from socket */
/* flag: specifies, whether read data shall be logged  */
/* returns: 1 on success, 0 on error                   */
/*-----------------------------------------------------*/
int
sock_read(char *buf, int cnt, int flag)
{
	int c;
	char *p;
	char *pbuf;

	p = buf;

	while((c = sock_getc()) >= 0) {
		*buf = (char)c;

		buf++;
		cnt--;

		if(c == '\n' || cnt == 1) {
			*buf = '\0';
			
			/* if in verbose mode and flag for logging set, log read data */
			if(verbose && flag) {
				pbuf = strdup(p);

				if(pbuf == NULL)
					cddbd_log(LOG_ERR, "Can't malloc buf.");
				else {
					strip_crlf(pbuf);
					cddbd_log(LOG_INFO, "<- %s", pbuf);
					free(pbuf);
				}
			}

			return 1;
		}
	}

	return 0;
}


int
sock_getc(void)
{
	static unsigned char buf[CDDBBUFSIZ];

	if(sock_i == sock_e) {
		if(!sock_wait())
			return -1;

		sock_e = read(sock_fd, buf, sizeof(buf));
		if(sock_e < 0) {
			sock_e = sock_i;
			return 0;
		}

		sock_i = 1;
	}
	else
		sock_i++;

	return((int)buf[sock_i - 1]);
}


int
sock_wait(void)
{
	return read_wait(sock_fd);
}


int
read_wait(int fd)
{
	int n;

	if(xmit_time == 0)
		return 1;

	do {
		n = read_waitsecs(fd, xmit_time);
		if(n > 0)
			return 1;
	} while(n < 0 && errno == EINTR);

	if(n == 0) {
		errno = ETIMEDOUT;
		cddbd_log(LOG_ERR | LOG_XMIT, "Read timeout after %d seconds.",
		    xmit_time);
	}

	return 0;
}


int
read_waitsecs(int fd, int secs)
{
	int n;
	fd_set readfds;
	struct timeval tv;

	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);

	tv.tv_sec = secs;
	tv.tv_usec = 0;

	n = select((fd + 1), &readfds, (fd_set *)NULL, (fd_set *)NULL, &tv);

	return n;
}


void
cddbd_bcopy(char *from, char *to, int size)
{
	while(--size >= 0)
		to[size] = from[size];
}


void
cddbd_bzero(char *addr, int size)
{
	while(--size >= 0)
		addr[size] = 0;
}


void
fp_copy(FILE *ifp, FILE *ofp)
{
	char buf[CDDBBUFSIZ];

	fp_copy_init();

	/* Copy until input is exhausted. */
	while(fgets(buf, sizeof(buf), ifp))
		if(fp_copy_buf(ofp, buf) != 0)
			break;
}


void
fp_copy_init(void)
{
	fp_isend = 1;
}


int
fp_copy_buf(FILE *ofp, char *p)
{
	if((fp_isend && is_dbl_dot(p) && fputc('.', ofp) == EOF) ||
	    fputs(p, ofp) == EOF) {
		fp_isend = 1;
		return 1;
	}

	if(p[strlen(p) - 1] == '\n')
		fp_isend = 1;
	else
		fp_isend = 0;

	return 0;
}


int
get_serv_index(char *sname)
{
	int i;

	for(i = 0; servtab[i].s_name != 0; i++)
		if(!cddbd_strcasecmp(servtab[i].s_name, sname))
			return i;

	return -1;
}


short
get_serv_port(int port, int serv)
{
	short sport;
	struct servent *sp;

	if(port == 0) {
		if((sp = getservbyname(servtab[serv].s_name,
		    servtab[serv].s_proto)) == 0)
			sport = htons((short)servtab[serv].s_port);
		else
			sport = sp->s_port;
	}
	else
		sport = htons((short)port);

	return sport;
}


/* ARGSUSED */

void
chldhand(int sig)
{
	while(waitpid(-1, 0, WNOHANG) != -1)
		continue;

	signal(SIGCHLD, chldhand);
}
