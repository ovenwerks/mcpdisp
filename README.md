# mcpdisp
Mackie Control Protocol Display Program.

mcpdisp presents a jack midi port that will accept mackie control protocol display
messages that would normally appear on the surface "scribble strip" and displays them
on the screen. The strip LEDs and meters are displayed as well.

If -m is added to the command line, Global or Master displays are added.
 - The two charactor Assign display
 - The Timecode display (optional with -t)
 - When -t is not used the same space shows other button states.

-x and -y allow the starting position of the window to be set.
 - I have noticed that if either -x or -y are out of bounds the
	window will end up on the left display with dual monitors.
	-y -1 does not work as expected (negative numbers all seem
	to be the same as 1).

This is handy for devices such as the BCF2000 or midikb that have no
display of their own.

Home page: http://www.ovenwerks.net/software/mcpdisp.html
