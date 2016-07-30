#!/bin/sh
#
#   @(#)$Id: install.sh,v 1.17.2.6 2006/07/01 17:38:10 megari Exp $
#
#   cddbd - CD Database Protocol Server
#
#   Copyright (C) 1996  Steve Scherf (steve@moonsoft.com)
#   Portions Copyright (C) 2001-2006  by various authors 
#
#   Based on the original source by Ti Kan.
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#

PATH=/bin:/usr/bin:/sbin:/usr/sbin:/etc:/usr/local/bin:/usr/5bin
export PATH

CDDBD_VER=1.5.2
ERRFILE=/tmp/cddbd.err
TMPFILE=/tmp/cddbd.$$

if [ -x cddbd.exe ]
then
    CDDBDEXEC=cddbd.exe
else
    CDDBDEXEC=cddbd
fi
SUBMITCGI=submit.cgi

MANPAGE=cddbd.1

id=`id`

# Set up access defaults.
DIRPERM=755
FILEPERM=644
PWDPERM=600
DBINPERM1=700
DBINPERM2=6711
DCDDBCGIPERM=6711
SUBMITCGIPERM=711
UMASK=022
OWNER=`expr "$id" : '.*uid=.*(\(.*\)).*gid'`
GROUP=`expr "$id" : '.*gid=[0-9]*(\([a-zA-Z0-9_]*\))'`

USERS=100
HANGS=10
LINES=1024
SIZE=102400
XMITS=4
HIWAT=10240000
LOWAT=7680000
STRIP=no
BRZRK=yes
FUZZY=900
FUZDV=4
XTIME=600
ITIME=300
ATIME=600
CTIME=3600
ETIME=500
DTIME=250
MTIME=500
HTIME=60
LTIME=250
LWAIT=60
LOGFLAGS="errors hello access"
DHOSTNAME="`uname -n`"
DSMTPHOST="localhost"
DEMAILADDR="${OWNER}@`uname -n`"
DFEEDHOST="*.freedb.org"
ADMINMAIL="info@freedb.org"
CHARSET="prefer_iso"
UTFASISO="reject"
DSUBMITADDR="freedb-submit@freedb.org"
DTESTSUBMITADDR="test-submit@freedb.org"

#
# Utility functions
#

doexit()
{
	if [ $1 -eq 0 ]
	then
		$ECHO "\nInstallation of ${CDDBDEXEC} is now complete."
	else
		$ECHO "\nErrors have occurred in the installation."
		if [ $ERRFILE != /dev/null ]
		then
			$ECHO "See $ERRFILE for an error log."
		fi
	fi
	exit $1
}

logerr()
{
	if [ "$1" = "-p" ]
	then
		$ECHO "Error: $2"
	fi
	$ECHO "$2" >>$ERRFILE
	ERROR=1
}

getstr()
{
	$ECHO "$* \c"
	read ANS
	if [ -n "$ANS" ]
	then
		return 0
	else
		return 1
	fi
}

getyn()
{
	if [ -z "$YNDEF" ]
	then
		YNDEF=y
	fi

	while :
	do
		$ECHO "$*? [${YNDEF}] \c"
		read ANS
		if [ -n "$ANS" ]
		then
			case $ANS in
			[yY])
				RET=0
				break
				;;
			[nN])
				RET=1
				break
				;;
			*)
				$ECHO "Please answer y or n"
				;;
			esac
		else
			if [ $YNDEF = y ]
			then
				RET=0
			else
				RET=1
			fi
			break
		fi
	done

	YNDEF=
	return $RET
}

dolink()
{
	# Try symlink first
	ln -s $1 $2 2>/dev/null
	if [ $? != 0 ]
	then
		# Use hard link
		ln $1 $2 2>/dev/null
	fi
	RETSTAT=$?
	if [ $RETSTAT != 0 ]
	then
		logerr -p "Cannot link $1 -> $2"
	fi
	return $RETSTAT
}

makedir()
{
	$ECHO "\t$1"
	if [ ! -d $1 ]
	then
		mkdir -p $1
	fi
	if [ $2 != default ]
	then
		chmod $2 $1 2>/dev/null
	fi
	if [ $3 != default ]
	then
		chown $3 $1 2>/dev/null
	fi
	if [ $4 != default ]
	then
		chgrp $4 $1 2>/dev/null
	fi
	return 0
}

removedir()
{
	$ECHO "\t$1"
	if [ -d $1 ]
	then
		rmdir $1
	fi
	return 0
}

instfile()
{
	TDIR=`dirname $2`

	if [ -n "$TDIR" -a -d "$TDIR" -a -w "$TDIR" ]
	then
		if [ ! -f $1 ]
		then
			$ECHO "\t$2 NOT installed"
			logerr -n "Cannot install $2: file missing."
			return 1
		fi

		$ECHO "\t$2"
		if [ -f $2 ]
		then
			rm -f $2
		fi

		cp $1 $2
		if [ $? != 0 ]
		then
			logerr -n "Cannot install $2: file copy error."
			return 1
		fi

		if [ -f $2 ]
		then
			if [ $4 != default ]
			then
				chown $4 $2 2>/dev/null
			fi
			if [ $5 != default ]
			then
				chgrp $5 $2 2>/dev/null
			fi
			if [ $3 != default ]
			then
				chmod $3 $2 2>/dev/null
			fi
		fi
		return 0
	else
		$ECHO "\t$2 NOT installed"
		logerr -n "Cannot install $2: target directory not writable."
		return 1
	fi
}

#
# Main execution starts here.
#

# Catch some signals
trap "rm -f $TMPFILE; exit 1" 1 2 3 5 15

# Use Sysv echo if possible
if [ -x /usr/5bin/echo ]
then
	ECHO=/usr/5bin/echo				# SunOS SysV echo
elif [ -z "`(echo -e a) 2>/dev/null | fgrep e`" ]
then
	ECHO="echo -e"					# GNU bash, etc.
else
	ECHO=echo					# generic SysV
fi

if [ -x /usr/sbin/sendmail ]
then
	DSENDMAIL=/usr/sbin/sendmail
elif [ -x /usr/lib/sendmail ]
then
	DSENDMAIL=/usr/sbin/sendmail
else
	DSENDMAIL=""
fi


# Remove old error log file
ERROR=0
rm -f $ERRFILE
if [ -f $ERRFILE ]
then
	$ECHO "Cannot remove old $ERRFILE: error logging not enabled."
	ERRFILE=/dev/null
fi

if [ ! -f .accessfile ]
then
	$ECHO "You must run configure and make cddbd before installing."
	doexit 1
fi

if [ ! -x ${CDDBDEXEC} ]
then
	$ECHO "You must make cddbd before installing."
	doexit 1
fi

# Check privilege
if [ "$OWNER" != "root" ]
then
	$ECHO "You are not the super-user. You should be sure to specify"
	$ECHO "install directories that you have the proper write permissions"
	$ECHO "for.\n"

	YNDEF=n
	getyn "Proceed anyway"
	if [ $? -ne 0 ]
	then
		logerr -p "Not super user: installation aborted by user"
		doexit 1
	fi
fi


$ECHO "\nInstalling \"${CDDBDEXEC}\" CDDB Protocol Server $CDDBD_VER by Steve Scherf et al."


# Determine BINDIR

if [ -z "$BINDIR" ]
then
	if [ -d /usr/local/bin ]
	then
		BINDIR=/usr/local/bin
	elif [ -d /usr/lbin ]
	then
		BINDIR=/usr/lbin
	elif [ -d /usr/bin ]
	then
		BINDIR=/usr/bin
	else
		BINDIR=/usr/local/bin
	fi
else
	BINDIR=`echo $BINDIR | sed 's/\/\//\//g'`
fi

# get the man path
MANDIR=`echo $BINDIR | sed 's/bin$/man\/man1/g'`

while :
do
	if getstr "\nEnter cddbd binary directory\n[${BINDIR}]:"
	then
		if [ -d "$ANS" ]
		then
			BINDIR=$ANS
			break
		else
			$ECHO "Error: $ANS does not exist."
		fi
	else
		break
	fi
done

while :
do
	if getstr "\nEnter cddbd man directory (for cddbd.1)\n[${MANDIR}]:"
	then
		if [ -d "$ANS" ]
		then
			MANDIR=$ANS
			break
		else
			$ECHO "Error: $ANS does not exist."
		fi
	else
		break
	fi
done


# Find other directories

BASEDIR=`grep '^ACCESSFILE' .accessfile | awk '{ print $2 }'`
WORKDIR=${BASEDIR}
CDDBDIR=`dirname ${BASEDIR}`

if [ $CDDBDIR = "/" ]
then
	CDDBDIR=""
fi
CDDBDIR=${CDDBDIR}/cddb

while :
do
	if getstr "\nEnter cddbd work directory\n[${WORKDIR}]:"
	then
		if [ -d `dirname "$ANS"` ]
		then
			WORKDIR=$ANS
			break
		else
			$ECHO "Error: `dirname $ANS` does not exist."
		fi
	else
		break
	fi
done

while :
do
	if getstr "\nEnter CD database directory\n[${CDDBDIR}]:"
	then
		if [ -d "$ANS" ]
		then
			if [ "$ANS" = "$WORKDIR" ]
			then
				$ECHO "The database directory cannot be the \c"
				$ECHO "same as the work directory."
			else
				CDDBDIR=$ANS
				break
			fi

		else
			CDDBTMP=$ANS
			YNDEF=n
			if getyn "Warning: $ANS does not exist. Continue anyway"
			then
				CDDBDIR=$CDDBTMP
				break
			fi
		fi
	else
		break
	fi
done

YNDEF=y
if getyn "\nDo you want to enable server access via HTTP"
then
	if [ -d "$HOME/public_html" ]
	then
		CGIDIR="$HOME/public_html"
	else
		CGIDIR=""
	fi

	while :
	do
		if getstr "\nEnter cddbd CGI directory\n[${CGIDIR}]:"
		then
			if [ -d "$ANS" ]
			then
				CGIDIR=$ANS
				break
			else
				$ECHO "Error: $ANS does not exist."
			fi
		else
			if [ "$CGIDIR" != "" ]
			then
				break
			else
				$ECHO: "Error: a directory must be entered."
			fi
		fi
	done
	$ECHO "\nSubmissions via http are handled by the submit.cgi. The submit.cgi"
	$ECHO "forwards the submissions it receives to the central submit e-mail"
	$ECHO "address. To do that, it needs sendmail (or a sendmail wrapper) installed."
	if getyn "\nDo you want to accept submissions via http"
	then
		instsubmitcgi=true
		rm -f submit.cgi
		cat ${SUBMITCGI}.template > ${SUBMITCGI}
		while :
		do
			if getstr "\nEnter the path to sendmail.\n[${DSENDMAIL}]:"
			then
				if [ -x "$ANS" -a -f "$ANS" ]
				then
					ANS=`echo $ANS | sed 's/\//\\\\\//g'`
					sed "s/\/usr\/sbin\/sendmail/$ANS/" ${SUBMITCGI} > ${SUBMITCGI}.tmp && mv ${SUBMITCGI}.tmp ${SUBMITCGI}
					break
				else
					$ECHO "Error: $ANS does not exist."
				fi
			else
				if [ "$DSENDMAIL" != "" ]
				then
					DSENDMAIL=`$ECHO $DSENDMAIL | sed 's/\//\\\\\//'`
					sed "s/\/usr\/sbin\/sendmail/$DSENDMAIL/" ${SUBMITCGI} > ${SUBMITCGI}.tmp && mv ${SUBMITCGI}.tmp ${SUBMITCGI}
					break
				else
					$ECHO "The path to sendmail must be entered."
				fi
			fi
		done
		if getstr "\nEnter submit e-mail address.\n[${DSUBMITADDR}]:"
		then
			sed "s/${DSUBMITADDR}/$ANS/" ${SUBMITCGI} > ${SUBMITCGI}.tmp && mv ${SUBMITCGI}.tmp ${SUBMITCGI}
		fi
		if getstr "\nEnter test-submit e-mail address.\n[${DTESTSUBMITADDR}]:"
		then
			sed "s/${DTESTSUBMITADDR}/$ANS/" ${SUBMITCGI} > ${SUBMITCGI}.tmp && mv ${SUBMITCGI}.tmp ${SUBMITCGI}
		fi
	fi
fi

# Make directory names.

SERVDIR="${WORKDIR}/server"

# Make file names.

ACCESS="${BASEDIR}/access"
ALTACC="${WORKDIR}/access.alt"
MOTD="${WORKDIR}/motd"
SITE="${WORKDIR}/sites"
PASSWD="${WORKDIR}/passwd"

# Create the access file.

umask $UMASK
rm -f access
rm -f access.alt
cp access.hdr access
chmod $FILEPERM access

# Create the sites file.

rm -f sites
cat sites.hdr > sites
chmod $FILEPERM sites

# Create the passwd file.

rm -f passwd
cat passwd.hdr > passwd
chmod $PWDPERM passwd

$ECHO "motdfile:     ${MOTD}" >> access
$ECHO "sitefile:     ${SITE}" >> access
$ECHO "pwdfile:      ${PASSWD}" >> access
$ECHO "workdir:      ${WORKDIR}" >> access
$ECHO "cddbdir:      ${CDDBDIR}" >> access

YNDEF=n
if getyn "\nDo you want configure your site as a master database hub"
then
	master=true
	logging=post

	POSTDIR=${WORKDIR}/post
	DUPDIR=${WORKDIR}/dup

	$ECHO "postdir:      ${POSTDIR}" >> access
	$ECHO "dupdir:       ${DUPDIR}" >> access
	$ECHO "dup_policy:   compare" >> access
	$ECHO "transmits:    ${XMITS}" >> access

	if getstr "\nEnter your SMTP server host\n[${DSMTPHOST}]:"
	then
		SMTPHOST=$ANS
	else
		SMTPHOST=$DSMTPHOST
	fi

	$ECHO "smtphost:     ${SMTPHOST}" >> access

	if getstr "\nEnter your full email address\n[${DEMAILADDR}]:"
	then
		EMAILADDR=$ANS
	else
		EMAILADDR=$DEMAILADDR
	fi

	$ECHO "admin_email:  ${EMAILADDR}" >> access
	$ECHO "bounce_email: ${EMAILADDR}" >> access
	$ECHO "test_email:   ${EMAILADDR}" >> access
else
	logging=norm
	master=false
	$ECHO "postdir:      ${CDDBDIR}" >> access
	$ECHO "dup_policy:   always" >> access
fi

if getstr "\nEnter the hostname used for banner message etc.\n[${DHOSTNAME}]:"
then
	$ECHO "apphost:      $ANS" >> access
fi


case $logging in
	norm)
		$ECHO "logging:      ${LOGFLAGS}" >> access
		;;
	post)
		$ECHO "logging:      ${LOGFLAGS} post" >> access
		;;
	*)
		$ECHO "logging:      none" >> access
		;;
esac

$ECHO "post_lines:   ${LINES}" >> access
$ECHO "put_size:     ${SIZE}"  >> access
$ECHO "users:        ${USERS}" >> access
$ECHO "fuzzy_factor: ${FUZZY}" >> access
$ECHO "fuzzy_div:    ${FUZDV}" >> access
$ECHO "xmit_time:    ${XTIME}" >> access
$ECHO "input_time:   ${ITIME}" >> access
$ECHO "access_time:  ${ATIME}" >> access
$ECHO "connect_time: ${CTIME}" >> access
$ECHO "elapse_time:  ${ETIME}" >> access
$ECHO "delay_time:   ${DTIME}" >> access
$ECHO "email_time:   ${MTIME}" >> access
$ECHO "lock_time:    ${LTIME}" >> access
$ECHO "lock_wait:    ${LWAIT}" >> access
$ECHO "hang_time:    ${HTIME}" >> access
$ECHO "max_hangs:    ${HANGS}" >> access
$ECHO "log_hiwat:    ${HIWAT}" >> access
$ECHO "log_lowat:    ${LOWAT}" >> access
$ECHO "strip_ext:    ${STRIP}" >> access
$ECHO "ck_berzerk:   ${BRZRK}" >> access
$ECHO "file_charset: ${CHARSET}" >> access
$ECHO "utf_as_iso:   ${UTFASISO}" >> access

$ECHO "\nEnter user and permission information. In general, you will want"
$ECHO "the server and database user/group/perms to be the same. They MUST"
$ECHO "be the same if you are not root. Note that the server binary perms"
$ECHO "should be setuid/setgid unless you can guarantee that the owner will"
$ECHO "be the only user to run it."

if getstr "\nEnter the user who will own the server directory\n[${OWNER}]:"
then
	OWNID=$ANS
else
	OWNID=$OWNER
fi

if getstr "\nEnter the group of the server directory\n[${GROUP}]:"
then
	GRPID=$ANS
else
	GRPID=$GROUP
fi

if getstr "\nEnter the server file creation mode\n[${FILEPERM}]:"
then
	FPERM=$ANS
else
	FPERM=$FILEPERM
fi

if getstr "\nEnter the server directory creation mode\n[${DIRPERM}]:"
then
	DPERM=$ANS
else
	DPERM=$DIRPERM
fi

if getstr "\nEnter the user who will own the database\n[${OWNID}]:"
then
	DBOWNID=$ANS
else
	DBOWNID=$OWNID
fi

if getstr "\nEnter the group of the database\n[${GRPID}]:"
then
	DBGRPID=$ANS
else
	DBGRPID=$GRPID
fi

if getstr "\nEnter the database file mode\n[${FPERM}]:"
then
	DBFPERM=$ANS
else
	DBFPERM=$FPERM
fi

if getstr "\nEnter the database directory mode\n[${DPERM}]:"
then
	DBDPERM=$ANS
else
	DBDPERM=$DPERM
fi

SECUSERS=`grep '^SECUSERS' .accessfile | awk '{ print $2 }'`

if [ "$OWNID" = "$DBOWNID" -a "$GRPID" = "$DBGRPID" -a $SECUSERS -eq 0 ]
then
	BINPERM=$DBINPERM1
else
	BINPERM=$DBINPERM2
fi

if getstr "\nEnter the server file binary mode\n[${BINPERM}]:"
then
	BINPERM=$ANS
fi

if [ "$CGIDIR" != "" ]
then
	$ECHO "\nThe cddb.cgi file binary mode required depends on your web server settings."
	$ECHO "The cddb.cgi should be executed as the user who owns the server directory."
	$ECHO "If you are installing cddb.cgi in the public_html directory of that user"
	$ECHO "and executing the cgi suid is already taken care of by Apache suexec, then"
	$ECHO "you should specify 711 here, otherwise leave the default of 6711."
	if getstr "\nEnter the cddb.cgi binary mode\n[${DCDDBCGIPERM}]:"
	then
		CDDBCGIPERM=$ANS
	else
		CDDBCGIPERM=$DCDDBCGIPERM
	fi
fi


$ECHO "user:         ${OWNID}" >> access
$ECHO "group:        ${GRPID}" >> access
$ECHO "file_mode:    ${FPERM}" >> access
$ECHO "dir_mode:     ${DPERM}" >> access
$ECHO "db_user:      ${DBOWNID}" >> access
$ECHO "db_group:     ${DBGRPID}" >> access
$ECHO "db_file_mode: ${DBFPERM}" >> access
$ECHO "db_dir_mode:  ${DBDPERM}" >> access

PERMACC=access

if [ $master = true ]
then
	$ECHO "\n# Beginning of permissions.\n" >> access
	$ECHO "host_perms: ches default connect nopost noupdate noget \c" >> access
	$ECHO "noput nopasswd" >> access
else
	YNDEF=n
	if getyn "\nDo you want to allow automatic database updates"
	then
		YNDEF=n
		if getyn "\nDo you want your server remotely administered"
		then
			$ECHO "altaccfile:   ${ALTACC}" >> access

			PERMACC=access.alt
			PUT=put
			touch access.alt
			chmod $FILEPERM access.alt
			$ECHO "\n# Beginning of permissions.\n" >> access
		else
			PUT=noput
		fi

		$ECHO "\n# Beginning of permissions.\n" >> $PERMACC

		if getstr "\nEnter database feed site\n[${DFEEDHOST}]:"
		then
			FEEDHOST=$ANS
		else
			FEEDHOST=$DFEEDHOST
		fi

		$ECHO "host_perms: sc ${FEEDHOST} connect post update \c" >> access
		$ECHO "get $PUT nopasswd" >> access

		$ECHO "\nIn order for automatic updates to be initiated, you must"
		$ECHO "contact the cddbd administrator at \c"

		if [ "${FEEDHOST}" = "${DFEEDHOST}" ]
		then
			$ECHO "${ADMINMAIL}.\n"
		else
			$ECHO "the site you have specified.\n"
		fi
	else
		$ECHO "\n# Beginning of permissions.\n" >> access
	fi

	$ECHO "host_perms: che default connect nopost noupdate noget \c" >> $PERMACC
	$ECHO "noput nopasswd" >> $PERMACC
	$ECHO "host_perms: s default noconnect nopost noupdate noget \c" >> $PERMACC
	$ECHO "noput nopasswd" >> $PERMACC
fi

YNDEF=y
if getyn "\nDo you want to create the motd file now"
then
	if [ "$EDITOR" = "" ]
	then
		ED=/usr/bin/vi
	else
		ED=$EDITOR
	fi

	if getstr "\nEnter editor you wish to use\n[${ED}]:"
	then
		ED=$ANS
	fi

	if [ -f $MOTD ]
	then
		cp $MOTD motd
	fi

	$ED motd
else
	$ECHO "\nYou will have to edit $MOTD by hand if you"
	$ECHO "wish to create a message of the day."
	rm -f motd
fi

# Make all necessary directories

$ECHO "\nMaking directories..."

makedir $WORKDIR $DPERM $OWNID $GRPID
makedir $SERVDIR $DPERM $OWNID $GRPID

# Save old important files

$ECHO "\nPreserving old files..."

if [ -f $ACCESS ]
then
	$ECHO "\t$ACCESS -> ${ACCESS}.old"
	mv $ACCESS ${ACCESS}.old

	err=$?
	if [ $err -ne 0 ]
	then
		logerr -n "Failed to preserve $ACCESS."
	fi
fi

if [ -f $ALTACC -a -f access.alt ]
then
	$ECHO "\t$ALTACC -> ${ALTACC}.old"
	mv $ALTACC ${ALTACC}.old

	err=$?
	if [ $err -ne 0 ]
	then
		logerr -n "Failed to preserve $ALTACC."
	fi
fi

if [ -f $SITE ]
then
	$ECHO "\t$SITE"

	grep "End of header" $SITE > /dev/null
	if [ $? -eq 0 ]
	then
		sed -e "1,/End of header/d" $SITE >> sites
	else
		$ECHO "" >> sites
		cat $SITE >> sites
	fi
fi

if [ -f $PASSWD ]
then
	$ECHO "\t$PASSWD"

	grep "End of header" $PASSWD > /dev/null
	if [ $? -eq 0 ]
	then
		sed -e "1,/End of header/d" $PASSWD >> passwd
	else
		$ECHO "" >> passwd
		cat $PASSWD >> passwd
	fi
fi

if [ -f $MOTD -a -f motd ]
then
	$ECHO "\t$MOTD -> ${MOTD}.old"
	mv $MOTD ${MOTD}.old

	err=$?
	if [ $err -ne 0 ]
	then
		logerr -n "Failed to preserve $MOTD."
	fi
fi

# Install files
$ECHO "\nInstalling files..."

# Binaries
instfile ${CDDBDEXEC} ${BINDIR}/${CDDBDEXEC} $BINPERM $OWNID $GRPID

if [ -f access.alt ]
then
	instfile access.alt $ALTACC $FPERM $OWNID $GRPID
fi

# Man pages
mkdir -p $MANDIR
instfile ${MANPAGE} ${MANDIR}/${MANPAGE} $FILEPERM $OWNID $GRPID

if [ "$CGIDIR" != "" ]
then
	instfile ${CDDBDEXEC} ${CGIDIR}/cddb.cgi $CDDBCGIPERM $OWNID $GRPID
	if [ $instsubmitcgi = true ]
	then
		instfile ${SUBMITCGI} ${CGIDIR}/submit.cgi $SUBMITCGIPERM $OWNID $GRPID
	fi
fi

# Configuration files
instfile access $ACCESS $FPERM $OWNID $GRPID
instfile sites $SITE $FPERM $OWNID $GRPID
instfile passwd $PASSWD $PWDPERM $OWNID $GRPID

if [ -f motd ]
then
	instfile motd $MOTD $FPERM $OWNID $GRPID
fi

YNDEF=y
if getyn "\nDo you want to create the fuzzy matching hash file now"
then
	$ECHO "\nPlease wait while the hash file is created."
	$ECHO "This will take a few minutes.\n"

	${BINDIR}/${CDDBDEXEC} -fd

	err=$?
	if [ $err -ne 0 ]
	then
		logerr -n "Failed to build the fuzzy matching hash file."
	fi
else
	$ECHO "\nIf you wish to enable fuzzy matching in the future, you will"
	$ECHO "have to run \"${CDDBDEXEC} -fd\"."
fi

doexit $ERROR
