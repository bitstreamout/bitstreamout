#!/bin/bash
#
# Run this e.g. with `vdr -a audio.sh'
# and search afterward for /tmp/audio.sh.*
#

TMPFILE=$(mktemp /tmp/${0##*/}.XXXXXX) || exit 1
exec -a xlist xlist -b -o $TMPFILE -
