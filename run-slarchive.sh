#!/bin/sh
# slarchive client - connette a ringserver su RUT956 e archivia miniSEED
#
# Ringserver: 192.168.1.188:18000 (SeedLink)
# Stream:     XX_MEDA1 (rete XX, stazione MEDA1)
# Archivio:   /tmp/mseed/ in formato SDS
# State file: /tmp/slarchive.state (riprende dall'ultimo pacchetto ricevuto)

#    -S XX_MEDA1 \


RING_HOST="192.168.1.188"
RING_PORT="18000"
ARCHIVE_DIR="/tmp/mseed"
STATE_FILE="/tmp/slarchive.state"

mkdir -p "$ARCHIVE_DIR"

exec slarchive \
    -v \
    -SDS "$ARCHIVE_DIR" \
    -x "$STATE_FILE":100 \
    -nd 10 \
    -k 30 \
    "${RING_HOST}:${RING_PORT}"
