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
static char *const _help_c_ident_ = "@(#)$Id: help.c,v 1.9.2.1 2006/04/17 13:22:21 joerg78 Exp $";
#endif


/* Help string definitions. */

char *help_info[] = {
	"",
	"This is a CDDB protocol (CDDBP) CD Database Server, used for",
	"finding and retrieving CDDB format Compact Disc data.",
	"",
	"Cddbd, the \"CD Database Server\" copyright (c) 1996-1997 by ",
	"Steve Scherf. CDDB specification copyright (c) 1996-1997 by Ti Kan,",
	"portions copyright (c) 1996-1997 by Steve Scherf.",
	"Some modifications (c) 1999-2006 by various contributors.",
	"",
	"If you have questions or comments, send email to: info@freedb.org.",
	0
};

char *help_help[] = {
	"HELP [command [subcmd]]",
	"    Prints help information for the given server command.",
	"    With no argument, prints a list of server commands.",
	"    Arguments are:",
	"        command:  command to print help for.",
	"        subcmd:   subcommand of command to print help for.",
	0
};

char *cddb_help[] = {
	"CDDB <subcmd> (valid subcmds: HELLO LSCAT QUERY READ UNLINK WRITE)",
	"    Performs a CD database operation.",
	"    Arguments are:",
	"        subcmd:  CDDB subcommand to print help for.",
	"    Subcommands are:",
	"",
	0
};

char *get_help[] = {
	"GET <file>",
	"    Gets a server system file.",
	0
};

char *log_help[] = {
	"LOG [-l lines] [get [-f flag]] [start_time [end_time]] | [day [days]]",
	"    Shows message log statistics.",
	"    Arguments are:",
	"        lines:       max number of lines of list info to print.",
	"        get:         the string \"get\", to return the log itself.",
	"        flag:        the flag to filter messages by.",
	"        start_time:  date to count messages after.",
	"        end_time:    date to count messages up to.",
	"                     Format: hh[mm[ss[MM[DD[YY]]]]]",
	"        day:         the string \"day\", to count messages generated",
	"                     within the specified # of days (default 1 day).",
	"        days:        the number of days worth of messages to count.",
	0
};

char *motd_help[] = {
	"MOTD",
	"    Displays the message of the day.",
	0
};

char *proto_help[] = {
	"PROTO [level]",
	"    Displays the current and supported protocol level, and sets",
	"    the current level.",
	"    Arguments are:",
	"        level:  the protocol level to set.",
	0
};

char *put_help[] = {
	"PUT <file>",
	"    Puts a server system file.",
	0
};

char *quit_help[] = {
	"QUIT",
	"    Close database server connection.",
	0
};

char *validate_help[] = {
	"VALIDATE",
	"    Perform user validation.",
	0
};

char *ver_help[] = {
	"VER",
	"    Print cddbd version information.",
	0
};

char *whom_help[] = {
	"WHOM",
	"    List connected server users.",
	0
};

char *stat_help[] = {
	"STAT",
	"    Print server statistics.",
	0
};

char *sites_help[] = {
	"SITES",
	"    Print a list of known server sites.",
	0
};

char *update_help[] = {
	"UPDATE",
	"    Update the database with new entries.",
	0
};

char *hello_help[] = {
	"    HELLO <username> <hostname> <clientname> <version>",
	"        Register necessary information with CD database server.",
	"        Arguments are:",
	"            username:    login name of user.",
	"            hostname:    host name of client system.",
	"            clientname:  name of client software.",
	"            version:     version number of client software.",
	0
};

char *query_help[] = {
	"    QUERY <discid> <ntrks> <off_1> <off_2> <...> <off_n> <nsecs>",
	"        Perform a search for database entries that match parameters.",
	"        Arguments are:",
	"            discid:  CD disc ID number.",
	"            ntrks:   total number of tracks on CD.",
	"            off_X:   frame offset of track X.",
	"            nsecs:   total playing length of the CD in seconds.",
	0
};

/*
char *srch_help[] = {
	"    SRCH  <keyword> <search_type> ... <search_type>",
	"        Perform a keyword search.",
	"        Arguments are:",
	"            keyword:      pseudo-regular expression to match on.",
	"            search_type:  what parts of the database to search.",
	"                          At least one of the following must be",
	"                          specified:",
	"                artist:   signifies a CD artist search.",
	"                ext:      signifies a CD extended info search.",
	"                title:    signifies a CD title search.",
	"                text:     signifies a CD track extended info search.",
	"                trk:      signifies a CD track title search.",
	0
};
*/

char *read_help[] = {
	"    READ  <category> <discid>",
	"        Retrieve the database entry for the specified CD.",
	"        Arguments are:",
	"            category:  CD category.",
	"            discid:    CD disk ID number.",
	0
};

char *write_help[] = {
	"    WRITE <category> <discid>",
	"        Submit a CD database file for for inclusion in the database.",
	"        Arguments are:",
	"            category:  CD category.",
	"            discid:    CD disk ID number.",
	0
};

char *lscat_help[] = {
	"    LSCAT",
	"        List all database categories.",
	0
};

char *discid_help[] = {
	"DISCID <ntrks> <off_1> <off_2> <...> <off_n> <nsecs>",
	"    Calculate a CDDB disc ID for a given set of CD TOC information.",
	"    Arguments are:",
	"        ntrks:  total number of tracks on CD.",
	"        off_X:  frame offset of track X.",
	"        nsecs:  total playing length of the CD in seconds.",
	0
};


char *unlink_help[] = {
	"    UNLINK <category> <discid>",
	"        Delete a database entry or link.",
	"        Arguments are:",
	"            category:  CD category.",
	"            discid:    CDDB disc ID to be deleted.",
	0
};

