# mcpdisp
Mackie Control Protocol Display Program.

mcpdisp presents a jack midi port that will accept mackie control protocol display
messages that would normally appear on the surface "scribble strip" and displays them
on the screen. The strip LEDs and meters are displayed as well.

If -m is added to the command line, Global or Master displays are added.
 - The two charactor Assign display
 - The Timecode display (optional with -t)
 - When -t is not used the same space shows other button states.

This is handy for devices such as the BCF2000 or midikb that have no
display of their own.

Home page: http://www.ovenwerks.net/software/mcpdisp.html
