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
#include <FL/Fl_Pack.H>

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
Fl_Output line1(5, 5, 564, 20, "");
Fl_Output line2(5, 25, 564, 20, "");

class ChLed : public Fl_Pack
{
private:
int wx, wy;
Fl_Output *led_R;
Fl_Output *led_S;
Fl_Output *led_M;
Fl_Output *led_W;
public:

		ChLed(int wx, int wy) :
		Fl_Pack(wx, wy, 60, 20, "")
		{
		// Set color of group to dark green
		color(57);
		type(Fl_Pack::HORIZONTAL);
		// Begin adding children to this group
		begin();
			led_R = new Fl_Output(5, 50, 15, 20, "");
			led_R->color(57);
			led_S = new Fl_Output(20, 50, 15, 20, "");
			led_S->color(57);
			led_M = new Fl_Output(35, 50, 15, 20, "");
			led_M->color(57);
			led_W = new Fl_Output(50, 50, 15, 20, "");
			led_W->color(57);

		// Stop adding children to this group
		end();
		// Display the window
		show();
		}

void rec (bool rst) {
	if (rst == true) {
		led_R->color(FL_RED);
	} else {
		led_R->color(57);
	}
	led_R->redraw();
}

void sol (bool sst) {
	if (sst == true) {
		led_S->color(FL_GREEN);
	} else {
		led_S->color(57);
	}
	led_S->redraw();
}

void mute (bool mst) {
	if (mst == true) {
		led_M->color(FL_YELLOW);
	} else {
		led_M->color(57);
	}
	led_M->redraw();
}

void sel (bool sest) {
	if (sest == true) {
		led_W->color(FL_WHITE);
	} else {
		led_W->color(57);
	}
	led_W->redraw();
}

};

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

// Clean up if someone closes the window
void close_cb(Fl_Widget*, void*) {
	printf("Killing child processes..\n");
	jack_port_unregister(client, input_port);
	jack_deactivate(client);
	jack_ringbuffer_free(ledbuffer);
	jack_ringbuffer_free(textbuffer);
	jack_client_close(client);

	printf("Done.\n");
	exit(0);
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
{
	int c;
	char textbit[9];
	int texoff, texi;
	line1_in[56] = 0x00;
	line2_in[56] = 0x00;
	ChLed *chled[8];
	//char *wname;

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


	char *jname = jack_get_client_name (client);
	//strcpy (wname,"Mackie Control Display Emulator - ");
	//strcat (wname, jname);

		// lets make a window
	Fl_Window win (4000, 4000, 575, 75, jname);
	win.callback(close_cb);
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
			for ( int x=0; x < 8; x++) {
				ChLed *led = new ChLed((8 + (x * 70)), 45);
				chled[x] = led;
			}

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
					for (texi = 0; texi < 7; texi++) {
						line1_in[texoff - 1 + texi] = textbit[texi + 1];
					}
					line1.value(line1_in);
				} else {
					/* bottom row */
					texoff = texoff - 56;
					for (texi = 0; texi < 7; texi++) {
						line2_in[texoff - 1 + texi] = textbit[texi + 1];
					}
					line2.value(line2_in);
				}
			}
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
					if (textbit[1] == 0) {
						chled[(int) textbit[0]]->rec(false);
					} else {
						chled[(int)textbit[0]]->rec(true);
					}
				} else if ( textbit[0] < 16 ) {
					/* Lamps 8 - 15 are PFL (Solo?) buttons */
					if (textbit[1] == 0) {
						chled[(int) textbit[0] - 8]->sol(false);
					} else {
						chled[(int) textbit[0] - 8]->sol(true);
					}
				} else if ( textbit[0] < 24 ) {
					/* Lamps 16 - 23 are Mute buttons */
					if (textbit[1] == 0) {
						chled[(int) textbit[0] - 16]->mute(false);
					} else {
						chled[(int) textbit[0] - 16]->mute(true);
					}
				} else if ( textbit[0] < 32 ) {
					/* Lamps 24 - 31 are channel select indicators */
					if (textbit[1] == 0) {
						chled[(int) textbit[0] - 24]->sel(false);
					} else {
						chled[(int) textbit[0] - 24]->sel(true);
					}
				}
			}
		}
	}
	std::cout << "after while\n";
	cout.flush();
	sleep(10);
	jack_client_close(client);
	exit (0);
}
