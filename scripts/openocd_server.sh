#!/bin/bash
#
#   Start or stop a background OpenOCD server for debugging a Meadow (GDB port 4242).
#
#     scripts/openocd_server.sh reset    start OpenOCD with debug.cfg (resets and halts the board)
#     scripts/openocd_server.sh attach   start OpenOCD with debug-attach.cfg (no reset)
#     scripts/openocd_server.sh stop     stop the OpenOCD started by this script
#
#   "reset" and "attach" stop an OpenOCD previously started by this script, start a new one in
#   the background and return once it is listening on port 4242, so the script can run as a
#   CLion "Before launch" step ahead of the GDB attach.  OpenOCD keeps running after the debug
#   session; it is stopped by the next start, by "stop", or by scripts/deploy.sh.
#
#   The process id and the OpenOCD log are kept in the temporary directory.
#
#   Requires MEADOW_OPENOCD_DIR.
#
scriptdir="$( cd "$(dirname "$0")" ; pwd -P )"
rootdir="$( cd "$scriptdir/.." ; pwd -P )"
statedir="${TMPDIR:-/tmp}"
pidfile="$statedir/meadow-openocd.pid"
logfile="$statedir/meadow-openocd.log"
port=4242

#
#   Stop the OpenOCD this script started, if it is still running.
#
stop_server() {
  local pid
  if [ ! -f "$pidfile" ]; then
    return 0
  fi
  pid=$(cat "$pidfile")
  if [ -n "$pid" ] && ps -p "$pid" -o command= 2>/dev/null | grep -q openocd; then
    echo "Stopping OpenOCD (pid $pid)"
    kill "$pid"
    for i in $(seq 1 20); do
      if ! kill -0 "$pid" 2>/dev/null; then
        break
      fi
      sleep 0.25
    done
  fi
  : > "$pidfile"
  return 0
}

case "$1" in
  reset)
    config="$rootdir/debug.cfg"
    ;;
  attach)
    config="$rootdir/debug-attach.cfg"
    ;;
  stop)
    stop_server
    exit 0
    ;;
  *)
    echo "Usage: $0 reset|attach|stop"
    exit 1
    ;;
esac

if [ -z "$MEADOW_OPENOCD_DIR" ]; then
  echo "MEADOW_OPENOCD_DIR is not set."
  exit 1
fi

stop_server

if lsof -nP -iTCP:$port -sTCP:LISTEN > /dev/null 2>&1; then
  echo "Port $port is in use by another process (not started by this script):"
  lsof -nP -iTCP:$port -sTCP:LISTEN
  exit 1
fi

echo "Starting OpenOCD with $(basename "$config"); log: $logfile"
cd "$rootdir" || exit 1
#
#   OpenOCD is detached from this script: it runs in its own session as the child of a process
#   that exits at once.  CLion stops every process a Before launch script started when the
#   script ends, and OpenOCD then shuts down ("shutdown command invoked") before GDB connects.
#   The detached process writes its own pid to the pid file.
#
: > "$pidfile"
perl -MPOSIX -e '
  my ($pidfile, $logfile, @cmd) = @ARGV;
  exit 0 if fork();
  POSIX::setsid();
  exit 0 if fork();
  open(my $fh, ">", $pidfile) or die; print $fh "$$\n"; close($fh);
  open(STDIN, "<", "/dev/null");
  open(STDOUT, ">", $logfile);
  open(STDERR, ">&STDOUT");
  exec(@cmd) or die;
' "$pidfile" "$logfile" "$MEADOW_OPENOCD_DIR/src/openocd" -s "$MEADOW_OPENOCD_DIR/tcl" -f "$config"

pid=""
for i in $(seq 1 20); do
  pid=$(cat "$pidfile")
  if [ -n "$pid" ]; then
    break
  fi
  sleep 0.1
done
if [ -z "$pid" ]; then
  echo "OpenOCD did not start."
  exit 1
fi

for i in $(seq 1 40); do
  if lsof -nP -a -p "$pid" -iTCP:$port -sTCP:LISTEN > /dev/null 2>&1; then
    echo "OpenOCD is listening on port $port (pid $pid)"
    exit 0
  fi
  if ! kill -0 "$pid" 2>/dev/null; then
    break
  fi
  sleep 0.25
done

echo "OpenOCD did not start:"
cat "$logfile"
stop_server
exit 1
