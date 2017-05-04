#!/bin/sh
#
# sysmgr University of Wisconsin System Manager
#
# chkconfig:   - 80 20
# description: University of Wisconsin IPMI System Manager

### BEGIN INIT INFO
# Provides: 
# Required-Start: $network $syslog
# Required-Stop: 
# Should-Start: $syslog
# Should-Stop: $network $syslog
# Default-Start: 
# Default-Stop: 
# Short-Description: Start the University of Wisconsin IPMI System Manager
# Description:      
### END INIT INFO

# Source function library.
. /etc/rc.d/init.d/functions

exec="/usr/bin/sysmgr"
prog="sysmgr"
config="/etc/sysmgr/sysmgr.conf"

[ -e /etc/sysconfig/$prog ] && . /etc/sysconfig/$prog

lockfile=/var/lock/subsys/$prog

start() {
    [ -x $exec ] || exit 5
    [ -e $config ] || exit 6
    echo -n $"Starting $prog: "
    # if not running, start it up here, usually something like "daemon $exec"
    $prog $config
    retval=$?
    echo
    [ $retval -eq 0 ] && touch $lockfile
    return $retval
}

stop() {
    echo -n $"Stopping $prog: "
    # stop it here, often "killproc $prog"
    killproc $prog
    retval=$?
    echo
    [ $retval -eq 0 ] && rm -f $lockfile
    return $retval
}

restart() {
    stop
    start
}

reload() {
    restart
}

force_reload() {
    restart
}

rh_status() {
    # run checks to determine if the service is running or use generic status
    status $prog
}

rh_status_q() {
    rh_status >/dev/null 2>&1
}


case "$1" in
    start)
        rh_status_q && exit 0
        $1
        ;;
    stop)
        rh_status_q || exit 0
        $1
        ;;
    restart)
        $1
        ;;
    reload)
        rh_status_q || exit 7
        $1
        ;;
    force-reload)
        force_reload
        ;;
    status)
        rh_status
        ;;
    condrestart|try-restart)
        rh_status_q || exit 0
        restart
        ;;
    *)
        echo $"Usage: $0 {start|stop|status|restart|condrestart|try-restart|reload|force-reload}"
        exit 2
esac
exit $?
