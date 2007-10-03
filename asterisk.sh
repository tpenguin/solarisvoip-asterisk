#!/bin/sh
#
# Start/stop asterisk
#

if [ -f /lib/svc/share/smf_include.sh ]; then
	. /lib/svc/share/smf_include.sh
fi

if [ ! -x /opt/asterisk/usr/sbin/asterisk ]; then
	echo "/opt/asterisk/usr/sbin/asterisk is missing or not executable."
	exit $SMF_EXIT_ERR_CONFIG
fi

start_asterisk()
{
	/opt/asterisk/usr/sbin/asterisk -gnp
	ASTPID=`cat /var/opt/asterisk/run/asterisk.pid`
	sleep 5
	ps -p $ASTPID >/dev/null 2>&1
	if [ $? -ne 0 ]; then
		logger -p daemon.notice "Could not start Asterisk. Check Asterisk log files."
		exit $SMF_EXIT_ERR_CONFIG
	fi
	logger -p daemon.notice "Asterisk started as PID $ASTPID."
}

case "$1" in
'start')
		start_asterisk
		;;

'stop')
		/opt/asterisk/usr/sbin/asterisk -rx "stop now"       # kill daemon
		sleep 2
        ;;
'restart')
		/opt/asterisk/usr/sbin/asterisk -rx "stop now"
		sleep 2
		start_asterisk
        ;;
*)
        echo "Usage: /etc/init.d/asterisk { start | stop | restart }"
        ;;
esac

exit 0
