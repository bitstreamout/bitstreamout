#!/bin/sh
#
# mute/umute	script for SBLive controlled by amixer
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

#
# Howto do this with sound blaster??
# # For MP2 audio over PCI/bitstreamout you should change this
# # to off to be able to hear the resulting PCM of libmad
# : ${loop:=on}
#
card="-c 0"

#
# Please configure your special soundblaster card.
# Note that if sonething is missed or is not available
# for your special soundblaster card you may modify this
# script by adding more ``has_feature'' variables.
# Also it would be a good extension to know how todo
# the loop through feature (for redirecting SPDIF out of
# the DVB card to a SPDIF in of the soundblaster).
#
has_3d=yes
has_surround=yes


#
# For more information see manual page of amixer
# Try `amixer controls| grep IEC958'
#
x=$1
case "$x" in
    on|out|unmute)
	amixer -q $card sset 'Wave'			90%,90%
	amixer -q $card sset 'IEC958 Optical Raw'	0%,0% mute
	amixer -q $card sset 'Aux'			90%,90% unmute,capture
	amixer -q $card sset 'Capture'			75%,75% unmute,capture
	amixer -q $card sset 'AC97'			90%,90%

	type -p usleep &> /dev/null && usleep 1000 ;;
    off|mute)
	amixer -q $card sset 'Wave'			0%,0%
	amixer -q $card sset 'IEC958 Optical Raw'	0%,0% unmute
	amixer -q $card sset 'Aux'			0%,0% unmute,capture
	amixer -q $card sset 'Capture'			0%,0% unmute,capture
	amixer -q $card sset 'AC97'			0%,0%
esac

amixer -q $card sset 'Master'		0%,0% mute
amixer -q $card sset 'Master Mono'	0%,0% mute
amixer -q $card sset 'Tone'		0%,0% mute
amixer -q $card sset 'Bass'		0%,0%
amixer -q $card sset 'Treble'		0%,0%

if test "$has_3d" = "yes" ; then
    amixer -q $card sset '3D Control - Switch'		0%,0% mute
    amixer -q $card sset '3D Control Sigmatel - Depth'	0%,0%
    amixer -q $card sset '3D Control Sigmatel - Rear Depth'	0%,0%
fi

amixer -q $card sset 'PCM'		0%,0% mute
if test "$has_surround" = "yes" ; then
    amixer -q $card sset 'Surround'		0%,0% mute
fi
amixer -q $card sset 'Surround Digital'	0%,0% capture
amixer -q $card sset 'Center'		0%,0%
amixer -q $card sset 'LFE'		0%,0%

amixer -q $card sset 'Music'		0%,0%
amixer -q $card sset 'CD'		0%,0% mute
amixer -q $card sset 'Mic'		0%,0% mute
amixer -q $card sset 'Video'		0%,0% mute
amixer -q $card sset 'Phone'		0%,0% mute

amixer -q $card sset 'External Amplifier Power Down'		0%,0% mute
amixer -q $card sset 'SB Live Analog/Digital Output Jack'	0%,0% unmute &> /dev/null
amixer -q $card sset 'Audigy Live Analog/Digital Output Jack'	0%,0% unmute &> /dev/null

amixer -q $card cset iface=MIXER,name='PC Speaker Playback Volume'	off

exit 0
