#!/bin/sh
# Option 1 : 3 processes
#amixer sget Master,0 | egrep -o '([0-9]+%|\[(on|off)\])' | sed ':a;N;$!ba;s/\n/ /g'
# Option 2 : 2 processes
amixer get Master | awk -F'[]%[]' '/%/ {if ($7 == "off") { print "Master Mute" } else { print $2"%" }}'
