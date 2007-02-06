#!/bin/sh
#

#
# Start/stop asterisk
#

case "$1" in

'start')
	echo "Starting Asterisk server"
         /opt/asterisk/usr/sbin/asterisk -gnp
        ;;
'stop')
         /opt/asterisk/usr/sbin/asterisk -rx "stop now"       # kill daemon
	sleep 2
        ;;
'restart')
        /opt/asterisk/usr/sbin/asterisk -rx "stop now"
	sleep 2
	 /opt/asterisk/usr/sbin/asterisk -gnp
        ;;
*)
        echo "Usage: /etc/init.d/asterisk { start | stop | restart }"
        ;;
esac

exit 0
