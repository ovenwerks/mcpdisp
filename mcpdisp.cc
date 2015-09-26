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
#include <getopt.h>

//Jack includes
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>

//fltk includes
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Pack.H>
#include <FL/Fl_Progress.H>

#define VERSION "pre-0.0.5"

using namespace std;


jack_client_t *client;

// only need input to display things
jack_port_t *input_port;
// need three ring buffers for ease of reading
// One for LEDs
jack_ringbuffer_t *ledbuffer = 0;
// One for text
jack_ringbuffer_t *textbuffer = 0;
// One for meters
jack_ringbuffer_t *meterbuffer = 0;

// state globals
bool master (false);
bool shotime (false);

// text globals
char line1_in[57];
char line2_in[57];
char disp2_in[3];
char time1_in[14];
char tm_bt;

// fltk globals
Fl_Output line1(5, 5, 564, 20, "");
Fl_Output line2(5, 25, 564, 20, "");
Fl_Output disp2(570, 5, 59, 40, "");
Fl_Output time1(630, 5, 330,40, "");

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
		color(57);
		type(Fl_Pack::HORIZONTAL);
		begin();
			led_R = new Fl_Output(5, 50, 15, 20, "");
			led_R->color(57);
			led_S = new Fl_Output(20, 50, 15, 20, "");
			led_S->color(57);
			led_M = new Fl_Output(35, 50, 15, 20, "");
			led_M->color(57);
			led_W = new Fl_Output(50, 50, 15, 20, "");
			led_W->color(57);
			led_W->textcolor(FL_RED);
		end();
		show();
		}

// turn record enable LED on/off
void rec (bool rst) {
	if (rst == true) {
		led_R->color(FL_RED);
	} else {
		led_R->color(57);
	}
	led_R->redraw();
}

// turn solo LED on/off
void sol (bool sst) {
	if (sst == true) {
		led_S->color(FL_GREEN);
	} else {
		led_S->color(57);
	}
	led_S->redraw();
}

// Turn mute LED on/off
void mute (bool mst) {
	if (mst == true) {
		led_M->color(FL_YELLOW);
	} else {
		led_M->color(57);
	}
	led_M->redraw();
}

// Turn Selected LED on/off
void sel (bool sest) {
	if (sest == true) {
		led_W->color(FL_WHITE);
	} else {
		led_W->color(57);
	}
	led_W->redraw();
}

// Set or unset peak detected
void peak (bool pk) {
	if (pk == true) {
		led_W->value("*");
	} else {
		led_W->value(" ");
	}
	led_W->redraw();
}

};

// channel combines a channel LED pack with a meter
class Chan : public Fl_Pack
{
private:
int wx, wy;
char old_lv;
Fl_Progress *meter;
ChLed *chled;
public:
		Chan(int wx, int wy) :
		Fl_Pack(wx, wy, 60, 25, "")
		{
		color(57);
		begin();
			chled = new ChLed(0, 0);
			chled->color(57); // do I need this?
			meter = new Fl_Progress(0, 20, 60, 10, "");
			meter->color(57);
			meter->maximum(12.0);
			meter->minimum(0.0);
			meter->value(12.0);
			old_lv = 12;
		end();
		show();
		}

// these just map chled stuff straight through
// There is probably a better way :)
		void rec (bool rst) {chled->rec(rst);}
		void sol (bool sst) {chled->sol(sst);}
		void mute (bool mst) {chled->mute(mst);}
		void sel (bool sest) {chled->sel(sest);}
		void peak (bool pk) {chled->peak(pk);}

// This sets the meter level
		void level (char lv) {
			if (lv >= old_lv) {
				if (lv < 13) {
					meter->value((float) lv);
					old_lv = lv;
				}
			} else {
				decr();
			}
		}

// This decrements the meter to provide fall off
		void decr (void) {
			if (old_lv > 0) { old_lv--; }
			meter->value((float) old_lv);
		}
};

static int usage() {
	printf(
	"mcpdisp Version %s\n"
	"Usage: mcpdisp [options]\n"
	"    Options are as follows:\n"
	"        -h, --help              Show this help text\n"
	"        -m, --master            Show master portion of display\n"
	"        -t, --time              Show Clock if master enabled\n"
	"        -V, --version           Show version information\n\n"
	, VERSION);

    return 0;
}



// Jack RT process function
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

			} else if( (*(in_event.buffer)) == 0x90 ) {
				// button press LED returns
				// Channels LEDs are all under 0x20
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
				} else if(master) { // catch Master/Global LED returns of interest
					switch ((*(in_event.buffer+1))) {
						case 0x72:	// time display is time
							if((*(in_event.buffer+2)) == 0) tm_bt = ':';
							break;
						case 0x71:	// time display is beats and bars
							if((*(in_event.buffer+2)) == 0) tm_bt = '|';
							break;
						default:
							break;
					}
				}
			} else if( (*(in_event.buffer)) == 0xd0 ) {
				// we have meter info
				int availableWrite = jack_ringbuffer_write_space(meterbuffer);
				if (availableWrite >= 1)
				{
					int written = jack_ringbuffer_write( meterbuffer, (const char*) (in_event.buffer + 1), 1 );
					if (written != 1 ) {
						/* only use this for debug - should be impossible, only one byte
						printf("ERROR! Partial meterbuffer write"); */
					}
				} else {
					/* there is no real reason to mess up our display with this text
					 * unless we are debugging */
					// printf("meterbuffer full skipping");
				}

			} else if( (*(in_event.buffer)) == 0xb0 ) {
				if(master) { // master uses b0 for 7 segment info
							// also used for vpot return, we should
							// actively filter it.
					if ( (*(in_event.buffer+1)) & 0x40) {
						char data1 = 0x20;
						if( (*(in_event.buffer+2)) < 0x20 ) {
							data1 = (*(in_event.buffer+2)) + 0x40;
							// cludge because some DAWs send @ instead of space
							if(data1 == 0x40) data1 = 0x20;
						} else {
							data1 = (*(in_event.buffer+2));
						}
						switch ((*(in_event.buffer+1))) {
							case 0x4b:	// left assign char
								disp2_in[0] = data1;
								break;
							case 0x4a:	// left assign char
								disp2_in[1] = data1;
								break;
							// timecode stuff, should maybe not be checked
							// for if not used
							case 0x49:	// time digit 10 (msb)
								time1_in[0] = data1;
								break;
							case 0x48:	// time digit 9
								time1_in[1] = data1;
								break;
							case 0x47:	// time digit 8
								time1_in[2] = data1;
								break;
							case 0x46:	// time digit 7
								time1_in[4] = data1;
								break;
							case 0x45:	// time digit 6
								time1_in[5] = data1;
								break;
							case 0x44:	// time digit 5
								time1_in[7] = data1;
								break;
							case 0x43:	// time digit 4
								time1_in[8] = data1;
								break;
							case 0x42:	// time digit 3
								time1_in[10] = data1;
								break;
							case 0x41:	// time digit 2
								time1_in[11] = data1;
								break;
							case 0x40:	// time digit 1 (lsb)
								time1_in[12] = data1;
								break;
							default:
								break;
						}
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
	char jackname[16];
	int texoff, texi, winsz;
	bool help (false);
	bool version (false);
	line1_in[56] = 0x00;
	line2_in[56] = 0x00;
	disp2_in[2] = 0x00;
	time1_in[13] = 0x00;
	tm_bt = '|';
	Chan *chan[8];
	char wname[64];


    struct option options[] = {
	{ "help", no_argument, 0, 'h' },
	{ "master", no_argument, 0, 'm' },
	{ "time", no_argument, 0, 't' },
	{ "version", no_argument, 0, 'V' },
	};

	while (1) {
	int c, option_index = 0;

	c = getopt_long (argc, argv, "hmtV", options, &option_index);
	if (c == -1)
		break;

	switch (c) {
		case 'h':
			help = true;
			break;
		case 'm':
			master = true;
			break;
		case 't':
			shotime = true;
			break;
		case 'V':
			version = true;
			break;
	    default:
			usage();
			return -1;
		}
	}

	if (help) {
		return usage();
	}
	if (version) {
		printf("midikb Version %s\n\n", VERSION);
		return 0;
	}
	if(!master) { // if we are not showing master, can't show time either
		shotime = false;
		strcpy(jackname, "mcpdisp-ext"); // use different name when extender
	} else {
		strcpy(jackname, "mcpdisp");
	}

	if ((client = jack_client_open (jackname, JackNullOption, NULL)) == 0)
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

	// buffer for meter events (single byte each)
	meterbuffer = jack_ringbuffer_create( 1024 );
	res = jack_ringbuffer_mlock(meterbuffer);
	if ( res ) {
		std::cout << "Error locking meter memory!\n";
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
	strcpy (wname,"Mackie Control Display Emulator - ");
	// add jack port name to window title
	strcat (wname, jname);

	// lets make a window
	if(master) {
		winsz = 965;
	} else {
		winsz = 574;
	}
	Fl_Window win (4000, 4000, winsz, 75, wname);
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
				Chan *led = new Chan((8 + (x * 70)), 45);
				chan[x] = led;
			}
			if(master) {
				// Two char display
				win.add(disp2);
				disp2.color(64);
				disp2.textfont(5);
				disp2.textcolor(88);
				disp2.textsize(42);
				disp2.value(disp2_in);
				if(shotime) {
					// timecode/bar display
					win.add(time1);
					time1.color(64);
					time1.textfont(5);
					time1.textcolor(88);
					time1.textsize(42);
					time1.value(time1_in);
				}
			}

		win.end();
	win.show ();


	/* run until interrupted */
	while(1)
	{
		// .03 gives about the right delay for
		// meters to go from FS to 0 in 1.8 seconds
		Fl::wait(.03);
		//Fl::wait(0);
		//usleep(30000); //Fl::wait already has a timer use it instead
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
						chan[(int) textbit[0]]->rec(false);
					} else {
						chan[(int)textbit[0]]->rec(true);
					}
				} else if ( textbit[0] < 16 ) {
					/* Lamps 8 - 15 are PFL (Solo?) buttons */
					if (textbit[1] == 0) {
						chan[(int) textbit[0] - 8]->sol(false);
					} else {
						chan[(int) textbit[0] - 8]->sol(true);
					}
				} else if ( textbit[0] < 24 ) {
					/* Lamps 16 - 23 are Mute buttons */
					if (textbit[1] == 0) {
						chan[(int) textbit[0] - 16]->mute(false);
					} else {
						chan[(int) textbit[0] - 16]->mute(true);
					}
				} else if ( textbit[0] < 32 ) {
					/* Lamps 24 - 31 are channel select indicators */
					if (textbit[1] == 0) {
						chan[(int) textbit[0] - 24]->sel(false);
					} else {
						chan[(int) textbit[0] - 24]->sel(true);
					}
				}
			}
		}

		//metering should go here
		availableRead = jack_ringbuffer_read_space(meterbuffer);
		if( availableRead >= 1 ) {
			int lp = availableRead;
			int chm = 0;
			int mval = 0;
			for(c=0; c<lp; c++) {
				int metres = jack_ringbuffer_read(meterbuffer, (char*)textbit, 1 );
				if ( metres != 1 ) {
					std::cout << "WARNING! didn't read full event!/n";
					return -1;
				}
				// divide into chm and mval
				mval = textbit[0] & 0x0f;
				chm = textbit[0] >> 4;
				if (mval == 0x0e) {
					chan[(int)chm]->peak(true);
					chan[(int)chm]->level(0x0c);
				} else if (mval == 0x0f) {
					chan[(int)chm]->peak(false);
				} else {
					chan[(int)chm]->level(mval);
				}
			}
		} else {
			//tell meters to decrement
			for (int i = 0; i < 8; i++) {
				chan[i]->decr();
			}
		}

		if(master) {
			// display assign value
			disp2.value(disp2_in);
			if(shotime) {
				// insert | for beats or : for time.
				// this is odd, we should only do this when mode switches
				time1_in[3] = tm_bt;
				time1_in[6] = tm_bt;
				time1_in[9] = tm_bt;
				// diplay time.
				time1.value(time1_in);
			}
		}

	}
	std::cout << "after while\n";
	cout.flush();
	sleep(10);
	jack_client_close(client);
	exit (0);
}
