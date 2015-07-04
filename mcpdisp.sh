#!/bin/bash
#script to run mcpdisp in a 5 line xterm


xterm +sb -uc -fn *-fixed-*-*-*-20-* -title "Mackie Control Display" -geometry 56x3+2000-1 -e mcpdisp
