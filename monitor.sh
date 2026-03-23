#!/bin/bash
# Persistent serial monitor — survives device resets
# Usage: ./monitor.sh [start|stop|logs|tail]

LOGDIR="$(cd "$(dirname "$0")" && pwd)/logs"
PIDFILE="/tmp/guitar_monitor.pid"

mkdir -p "$LOGDIR"

monitor_port() {
    local port="$1"
    local name=$(basename "$port")
    local log="$LOGDIR/$name.log"
    while true; do
        if [ -e "$port" ]; then
            cat "$port" >> "$log" 2>/dev/null
        fi
        sleep 1
    done
}

start() {
    stop 2>/dev/null
    rm -f "$LOGDIR"/*.log

    for port in /dev/cu.usbmodem101 /dev/cu.usbmodem1101; do
        monitor_port "$port" &
        echo $! >> "$PIDFILE"
    done
    echo "Monitor started (logs in $LOGDIR/)"
}

stop() {
    if [ -f "$PIDFILE" ]; then
        while read pid; do
            kill "$pid" 2>/dev/null
        done < "$PIDFILE"
        rm -f "$PIDFILE"
        echo "Monitor stopped"
    fi
}

case "${1:-start}" in
    start) start ;;
    stop)  stop ;;
    logs)  cat "$LOGDIR"/*.log 2>/dev/null ;;
    tail)  tail -f "$LOGDIR"/*.log 2>/dev/null ;;
    *)     echo "Usage: $0 [start|stop|logs|tail]" ;;
esac
