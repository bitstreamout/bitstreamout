#!/bin/sh
#
# mute/umute	script for C-Media PCI CMI8738 controlled by amixer
#		the command-line mixer for ALSA soundcard driver
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
# Or, point your browser to http://www.gnu.org/copyleft/gpl.html
#
# Copyright (C) 2002,2003 Werner Fink, <werner@suse.de>
#

# For MP2 audio over PCI/bitstreamout you should change this
# to off to be able to hear the resulting PCM of libmad
: ${loop:=on}
card="-c 0"

if test -z "$1" ; then
    x=${0##*/}
    x=${x%.*}
else
    x=$1
fi

#
# For more information see manual page of amixer
# Try `amixer controls| grep IEC958'
#
case "$x" in
    on|out|unmute)
	amixer -q $card cset iface=MIXER,name='IEC958 Output Switch' on
	amixer -q $card cset iface=MIXER,name='IEC958 Loop'	     $loop
	type -p usleep &> /dev/null && usleep 1000 ;;
    off|mute)
	amixer -q $card cset iface=MIXER,name='IEC958 Output Switch' off
	amixer -q $card cset iface=MIXER,name='IEC958 Loop'	     off
esac
amixer -q $card cset iface=MIXER,name='PC Speaker Playback Volume'   off
amixer -q $card cset iface=MIXER,name='Four Channel Mode'	     off
amixer -q $card cset iface=MIXER,name='IEC958 Copyright'	     off

exit 0
