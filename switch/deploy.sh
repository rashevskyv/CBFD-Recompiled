#!/bin/sh
# Copies the Switch build to the console over FTP (ftpd or sys-ftpd running on it), and fetches
# the logs back. From Linux or WSL, on the same network as the console:
#   switch/deploy.sh <console IP> [push|logs]   (default: push, then nothing else)
#   SWITCH_FTP_PORT (default 5000), SWITCH_FTP_USER / SWITCH_FTP_PASS if the server asks for them.
# push: $SWITCH_BUILD_DIR (default host/build-switch)/ConkerRecomp.nro, vktest.nro (when built) and host/assets/ into
#       sdmc:/switch/cbfd/. The ROM is not copied: put it there once as conker.z64.
# logs: cbfd_log.txt and vktest_log.txt into switch/logs/ (git-ignored).
set -e

cd "$(dirname "$0")/.."
IP=$1
ACTION=${2:-push}
[ -n "$IP" ] || { sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
PORT=${SWITCH_FTP_PORT:-5000}
URL="ftp://$IP:$PORT/switch/cbfd"
AUTH=
if [ -n "$SWITCH_FTP_USER" ]; then
    AUTH="--user $SWITCH_FTP_USER:$SWITCH_FTP_PASS"
fi

case "$ACTION" in
    push)
        curl -sS $AUTH --ftp-create-dirs -T "${SWITCH_BUILD_DIR:-host/build-switch}/ConkerRecomp.nro" "$URL/ConkerRecomp.nro"
        if [ -f host/build-switch-vktest/vktest.nro ]; then
            curl -sS $AUTH -T host/build-switch-vktest/vktest.nro "$URL/vktest.nro"
        fi
        find host/assets -type f | while read -r file; do
            curl -sS $AUTH --ftp-create-dirs -T "$file" "$URL/assets/${file#host/assets/}"
        done
        echo "Copied to sdmc:/switch/cbfd/ on $IP."
        ;;
    logs)
        mkdir -p switch/logs
        for log in cbfd_log.txt vktest_log.txt; do
            curl -sS $AUTH -o "switch/logs/$log" "$URL/$log" && echo "switch/logs/$log" || true
        done
        ;;
    *)
        echo "Unknown action: $ACTION (push or logs)" >&2
        exit 1
        ;;
esac
