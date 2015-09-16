/*
 * Copyright (C) 2015 Len Ovens
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */

#include <unistd.h>
#include <signal.h>
#include <iostream>

//Jack includes
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>

//fltk includes
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Output.H>


using namespace std;


jack_client_t *client;

/* only need input to display things */
jack_port_t *input_port;
/* need two ring buffers for ease of reading */
/* One for LEDs */
jack_ringbuffer_t *ledbuffer = 0;
/* One for text */
jack_ringbuffer_t *textbuffer = 0;

// text globals
char line1_in[57];
char line2_in[57];

// fltk globals
Fl_Output line1(5, 5, 580, 20, "");
Fl_Output line2(5, 25, 580, 20, "");

int process(jack_nframes_t nframes, void *arg)
{
	uint i;
	void* port_buf = jack_port_get_buffer(input_port, nframes);
	jack_midi_event_t in_event;
	jack_nframes_t event_count = jack_midi_get_event_count(port_buf);
	if(event_count > 0)
	{
		for(i=0; i<event_count; i++)
		{
			jack_midi_event_get(&in_event, port_buf, i);
			/* The events we need are keyon 0-31, and sysexec */
			if( (*(in_event.buffer)) == 0xf0 )
			{
				/* sysex, looking for display info */
				if( (*(in_event.buffer + 5)) == 0x12)
				{
					int availableWrite = jack_ringbuffer_write_space(textbuffer);
					if (availableWrite >= 8) {
						int written = jack_ringbuffer_write( textbuffer, (const char*) (in_event.buffer + 6), 8 );
						if (written != 8 ) {
							/* only for debug
							printf("ERROR! Partial textbuffer write");*/
						}
					} else {
					/* only for debug
						printf("textbuffer full skipping");*/

					}
				}
			}
			else if( (*(in_event.buffer)) == 0x90 ) {
				/* button press LED returns */
				if ((*(in_event.buffer+1)) < 32)
				{
					int availableWrite = jack_ringbuffer_write_space(ledbuffer);
					if (availableWrite >= 2)
					{
						int written = jack_ringbuffer_write( ledbuffer, (const char*) (in_event.buffer + 1), 2 );
						if (written != 2 ) {
							/* only use this for debug
							printf("ERROR! Partial textbuffer write"); */
						}
					} else {
						/* there is no real reason to mess up our display with this text
						 * unless we are debugging */
						// printf("textbuffer full skipping");

					}
				}
			}
		}
	}


	return 0;
}


/* Allow SIGTERM to cause graceful termination */
/* I don't know which of these are actually needed, but it ends nice */
void on_term(int signum) {
	jack_port_unregister(client, input_port);
	jack_deactivate(client);
	jack_ringbuffer_free(ledbuffer);
	jack_ringbuffer_free(textbuffer);
	jack_client_close(client);
	exit(0);

	return;
}

void jack_shutdown(void *arg)
{
	exit(1);
}

int main(int argc, char** argv)
//int main()
{
	char on;
	int c;
	char textbit[9];
	int texoff, texi;
	char esc = 0x1b;
	line1_in[56] = 0x00;
	line2_in[56] = 0x00;

	if ((client = jack_client_open ("mcpdisp", JackNullOption, NULL)) == 0)
	{
		std::cout << "Jack server not running?" << std::endl;
		return 1;
	}

	jack_set_process_callback (client, process, 0);

	jack_on_shutdown (client, jack_shutdown, 0);

	input_port = jack_port_register (client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

	/* setup LED buffer - All of these may change at once
		maybe twice close together. */
	ledbuffer = jack_ringbuffer_create( 1024 );
	int res = jack_ringbuffer_mlock(ledbuffer);
	if ( res ) {
		std::cout << "Error locking LED memory!\n";
		return -1;
	}

	/* set up display buffer */
	textbuffer = jack_ringbuffer_create( 4096 );
	res = jack_ringbuffer_mlock(textbuffer);
	if ( res ) {
		std::cout << "Error locking text memory!\n";
        return -1;
	}


	if (jack_activate (client)) {
		std::cout << "Error cannot activate client\n";
		return 1;
	}

	/* try to end nice on anything
		one of these makes window close work */
	signal(SIGTERM, on_term);
	signal(SIGINT, on_term);
	signal(SIGPIPE, on_term);
	signal(SIGHUP, on_term);
	signal(SIGQUIT, on_term);
	signal(SIGKILL, on_term);
	signal(SIGABRT, on_term);

	/* clear screen, park the cursor and hide it. Then flush */
	std::cout << esc << "[3;56H" << esc << "[?25l";
	cout.flush();

	// lets make a window
	Fl_Window win (600, 75, "Mackie Control Display Emulator");
	win.color(56);
		win.begin();
			win.add(line1);
			line1.color(57);
			line1.textfont(4);
			line1.textcolor(181);
			line1.textsize(16);
			line1.value(line1_in);
			win.add(line2);
			line2.color(57);
			line2.textfont(4);
			line2.textcolor(181);
			line2.textsize(16);
			line2.value(line2_in);
		win.end();
	win.show ();

	/* run until interrupted */
	while(1)
	{
		Fl::wait(0);
		usleep(1000);
		/* first do text fields */
		int availableRead = jack_ringbuffer_read_space(textbuffer);
		if( availableRead >= 8 ) {
			textbit[8] = '\x00';
			int lp = availableRead / 8;
			for(c=0; c<lp; c++) {
				int textres = jack_ringbuffer_read(textbuffer, (char*)textbit, 8 );
				if ( textres != 8 ) {
					/* this should not happen as we don't try to write
					to ring buff if there is not 8 bytes of space */
					std::cout << "\n\nWARNING! didn't read full event!\n";
					return -1;
				}
				texoff = textbit[0]+1;
				/* top row */
				if(texoff < 56) {
					std::cout << esc << "[1;" << texoff << "H" << esc << "[1;37m" << &textbit[1];
					for (texi = 0; texi < 7; texi++) {
						line1_in[texoff - 1 + texi] = textbit[texi + 1];
					}
					line1.value(line1_in);
				} else {
					/* bottom row */
					texoff = texoff - 56;
					std::cout << esc << "[2;" << texoff << "H" << esc << "[1;37m" << &textbit[1];
					for (texi = 0; texi < 7; texi++) {
						line2_in[texoff - 1 + texi] = textbit[texi + 1];
					}
					line2.value(line2_in);
				}
			}
			/* park the cursor and hide it, then flush */
			std::cout << esc << "[3;56H" << esc << "[?25l";
			cout.flush();
		}

		/* now display "Lamps" */
		availableRead = jack_ringbuffer_read_space(ledbuffer);
		if( availableRead >= 2 ) {
			int lp = availableRead / 2;
			for(c=0; c<lp; c++) {
				int ledres = jack_ringbuffer_read(ledbuffer, (char*)textbit, 2 );
				if ( ledres != 2 ) {
					std::cout << "WARNING! didn't read full event!/n";
					return -1;
				}
				/* Lamps 0 - 7 are Record enable */
				if( textbit[0] < 8 ) {
					texoff = (textbit[0] * 7) +2;
					if (textbit[1] == 0) {
						on = ' ';
					} else {
						on = 'R';
					}
					std::cout << esc << "[3;" << texoff << "H" << esc << "[1;31m" << on;
				} else if ( textbit[0] < 16 ) {
					/* Lamps 8 - 15 are PFL (Solo?) buttons */
					texoff = ((textbit[0] - 8) * 7) +3;
					if (textbit[1] == 0) {
						on = ' ';
					} else {
						on = 'P';
					}
					std::cout << esc << "[3;" << texoff << "H" << esc << "[1;32m" << on;
				} else if ( textbit[0] < 24 ) {
					/* Lamps 16 - 23 are Mute buttons */
					texoff = ((textbit[0] - 16) * 7) +4;
					if (textbit[1] == 0) {
						on = ' ';
					} else {
						on = 'M';
					}
					std::cout << esc << "[3;" << texoff << "H" << esc << "[1;33m" << on;
				} else if ( textbit[0] < 32 ) {
					/* Lamps 24 - 31 are channel select indicators */
					texoff = ((textbit[0] - 24) * 7) +1;
					if (textbit[1] == 0) {
						on = ' ';
					} else {
						on = 'S';
					}
					std::cout << esc << "[3;" << texoff << "H" << esc << "[1;37m" << on;
				}
			}
			/* park cursor, hide it and flush buffer */
			std::cout << esc << "[3;56H" << esc << "[?25l";
			cout.flush();
		}
	}
	std::cout << "after while\n";
	cout.flush();
	sleep(10);
	jack_client_close(client);
	exit (0);
}
