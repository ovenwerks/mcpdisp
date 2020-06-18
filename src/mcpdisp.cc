/*
 * Copyright (C) 2015 - 2020 Len Ovens
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
#include <FL/Fl_Box.H>
#include <FL/Fl_Pack.H>
#include <FL/Fl_Progress.H>

#define VERSION "0.1.0"

using namespace std;


jack_client_t *client;

// only need input to display things
jack_port_t *input_port;
// well, lets add a thru port to feed the surface
jack_port_t *thru_port;
// need ring buffer to go from real time to not
jack_ringbuffer_t *midibuffer = 0;

// state globals
bool master (false);
bool shotime (false);
unsigned int siz (3);

// text globals
char line1_in[57];
char line2_in[57];
char disp2_in[3];
char time1_in[14];
char tm_bt;
char midichunk[120];

class ChLed : public Fl_Pack
{
private:
int wx, wy;
Fl_Output *led_R;
Fl_Output *led_S;
Fl_Output *led_M;
Fl_Output *led_W;
Fl_Box *space;
public:

		ChLed(int wx, int wy) :
		Fl_Pack(wx, wy, siz * 23, siz * 7, "")
		{
		color(57);
		type(Fl_Pack::HORIZONTAL);
		begin();
			space = new Fl_Box (0, 0, siz + 1, siz * 7, "");
			space->color (56);
			led_R = new Fl_Output (0, 0, siz * 5, siz * 7, "");
			led_R->color(57);
			led_S = new Fl_Output (0, 0, siz * 5, siz * 7, "");
			led_S->color(57);
			led_M = new Fl_Output (0, 0, siz * 5, siz * 7, "");
			led_M->color(57);
			led_W = new Fl_Output (0, 0, siz * 5, siz * 7, "");
			led_W->color(57);
			led_W->textcolor (FL_RED);
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

// channel combines a channel LED pack with a meter and chanel display
class Chan : public Fl_Pack
{
private:
int wx, wy;
int loopcount = 0;
char old_lv;
Fl_Progress *meter;
Fl_Output *top_disp;
Fl_Output *low_disp;
ChLed *chled;
public:
	Chan(int wx, int wy) :
	Fl_Pack(wx, wy, siz * 23, siz * 8, "")
	{
		color(57);
		begin();
			top_disp = new Fl_Output (0, 0, siz * 23, siz * 7, "");
			top_disp->color(57);
			top_disp->textfont(4);
			top_disp->textcolor(181);
			top_disp->textsize(siz * 5);
			low_disp = new Fl_Output (0, 0, siz * 23, siz * 7, "");
			low_disp->color(57);
			low_disp->textfont(4);
			low_disp->textcolor(181);
			low_disp->textsize(siz * 5);
			chled = new ChLed(0, 0);
			chled->color(57); // do I need this?
			meter = new Fl_Progress(0, 0, siz * 20, siz * 4, "");
			meter->color(57);
			meter->maximum(12.0);
			meter->minimum(0.0);
			meter->value(12.0);
			old_lv = 12;
		end();
		show();
	}

	void top (char* line)
	{
		top_disp->value(line);
	}

	void low (char* line)
	{
		low_disp->value(line);
	}

	// these just map chled stuff straight through
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
		if (old_lv) {
			if (++loopcount > 10) {
				loopcount = 0;
				if (old_lv > 0) { old_lv--; }
				meter->value((float) old_lv);
				if (!old_lv) {
					peak (false);
				}
			}
		}
	}
};

// transport
class Transport : public Fl_Pack
{
private:
int wx, wy;
Fl_Output *RW;
Fl_Output *FF;
Fl_Output *Stop;
Fl_Output *Play;
Fl_Output *Rec;
Fl_Output *Solo;
Fl_Output *Assign;
Fl_Output *Flip;
Fl_Output *View;
public:

		Transport(int wx, int wy) :
		Fl_Pack(wx, wy, siz * 83, siz * 10, "")
		{
		color(57);
		type(Fl_Pack::HORIZONTAL);
		begin();
			RW = new Fl_Output(1, 1, siz * 8, siz * 10, "");
			RW->color(58);
			RW->textsize(siz * 6);
			RW->textcolor(57);
			RW->value("◂◂");
			FF = new Fl_Output(0, 1, siz * 8, siz * 10, "");
			FF->color(58);
			FF->textsize(siz * 6);
			FF->textcolor(57);
			FF->value("▸▸");
			Stop = new Fl_Output(0, 1, siz * 8, siz * 10, "");
			Stop->color(58);
			Stop->textsize(siz * 6);
			Stop->textcolor(57);
			Stop->value("■");
			Play = new Fl_Output(0, 1, siz * 8, siz * 10, "");
			Play->color(58);
			Play->textsize(siz * 6);
			Play->textcolor(57);
			Play->value("▶");
			Rec = new Fl_Output(0, 1, siz * 8, siz * 10, "");
			Rec->color(105);
			Rec->textsize(siz * 5);
			Rec->textcolor(106);
			Rec->value("⬤");
			Solo = new Fl_Output(0, 1, siz * 17, siz * 10, "");
			Solo->color(105);
			Solo->textsize(siz * 6);
			Solo->textcolor(106);
			Solo->value("Solo");
			Assign = new Fl_Output(0, 1, siz * 38, siz * 10, "");
			Assign->color(58);
			Assign->textsize(siz * 6);
			Assign->textcolor(61);
			Assign->value("");
			Flip = new Fl_Output(0, 1, siz * 14, siz * 10, "");
			Flip->color(58);
			Flip->textsize(siz * 6);
			Flip->textcolor(57);
			Flip->value("Flip");
			View = new Fl_Output(0, 1, siz * 17, siz * 10, "");
			View->color(58);
			View->textsize(siz * 6);
			View->textcolor(57);
			View->value("View");
		end();
		show();
		}

// turn record enable LED on/off
void rec (bool rst) {
	if (rst == true) {
		Rec->textcolor(1);
	} else {
		Rec->textcolor(106);
	}
	Rec->redraw();
}

// turn SOLO warning LED on/off
void solo (bool rst) {
	if (rst == true) {
		Solo->textcolor(1);
	} else {
		Solo->textcolor(106);
	}
	Solo->redraw();
}

// turn RW LED on/off
void rw (bool rst) {
	if (rst == true) {
		RW->textcolor(61);
	} else {
		RW->textcolor(57);
	}
	RW->redraw();
}

// turn FF LED on/off
void ff (bool rst) {
	if (rst == true) {
		FF->textcolor(61);
	} else {
		FF->textcolor(57);
	}
	FF->redraw();
}

// turn Stop LED on/off
void stop (bool rst) {
	if (rst == true) {
		Stop->textcolor(61);
	} else {
		Stop->textcolor(57);
	}
	Stop->redraw();
}

// turn Play LED on/off
void play (bool rst) {
	if (rst == true) {
		Play->textcolor(61);
	} else {
		Play->textcolor(57);
	}
	Play->redraw();
}


// turn Flip LED on/off
void flip (bool rst) {
	if (rst == true) {
		Flip->textcolor(61);
	} else {
		Flip->textcolor(57);
	}
	Flip->redraw();
}

// turn View LED on/off
void view (bool rst) {
	if (rst == true) {
		View->textcolor(61);
	} else {
		View->textcolor(57);
	}
	View->redraw();
}

// Set vpot assignment -> track
void track (bool pk) {
	if (pk == true) {
		Assign->value("  Track");
	}
	Assign->redraw();
}

// Set vpot assignment -> send
void send (bool pk) {
	if (pk == true) {
		Assign->value("  Send");
	}
	Assign->redraw();
}


// Set vpot assignment -> pan
void pan (bool pk) {
	if (pk == true) {
		Assign->value("   Pan");
	}
	Assign->redraw();
}

// Set vpot assignment -> plugin
void plug (bool pk) {
	if (pk == true) {
		Assign->value("  Plugin");
	}
	Assign->redraw();
}


// Set vpot assignment -> EQ
void eq (bool pk) {
	if (pk == true) {
		Assign->value("     EQ");
	}
	Assign->redraw();
}

// Set vpot assignment -> Instrument
void inst (bool pk) {
	if (pk == true) {
		Assign->value("Instrument");
	}
	Assign->redraw();
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
	"        -s, --small             Make it smaller\n"
	"        -x <x>                  Place mcpdisp at x position\n"
	"        -y <y>                  place mcpdisp at y position\n"
	"        -V, --version           Show version information\n\n"
	, VERSION);

    return 0;
}



// Jack RT process function
int process(jack_nframes_t nframes, void *arg)
{
	uint i;
	char sendstring[127];
	void* port_buf = jack_port_get_buffer(input_port, nframes);
	void* thru_buf = jack_port_get_buffer(thru_port, nframes);
	unsigned char* buffer;
	jack_midi_clear_buffer(thru_buf);

	jack_midi_event_t in_event;
	jack_nframes_t event_count = jack_midi_get_event_count(port_buf);
	if(event_count > 0)
	{
		for(i=0; i<event_count; i++)
		{
			jack_midi_event_get(&in_event, port_buf, i);
			// send event to through here
			buffer = jack_midi_event_reserve(thru_buf, 0, in_event.size);

			unsigned int availableWrite = jack_ringbuffer_write_space(midibuffer);
			if (availableWrite > in_event.size) {
				sendstring[0] = in_event.size;
				memcpy (&sendstring[1], in_event.buffer, in_event.size);
				unsigned int written = jack_ringbuffer_write( midibuffer, (const char*) sendstring, in_event.size + 1);
				if (written != in_event.size + 1) {
					// only for debug
					//cout << "ERROR! Partial midibuffer write\n";
				}
			} else {
				// only for debug
				// cout << "midibuffer full skipping\n";
			}
			memcpy (buffer, in_event.buffer, in_event.size);
		}
	}
	return 0;
}

// Clean up if someone closes the window
void close_cb(Fl_Widget*, void*) {
	printf("Killing child processes..\n");
	jack_port_unregister(client, input_port);
	jack_port_unregister(client, thru_port);
	jack_deactivate(client);
	jack_ringbuffer_free(midibuffer);
	jack_client_close(client);

	printf("Done.\n");
	exit(0);
}
/* Allow SIGTERM to cause graceful termination */
/* I don't know which of these are actually needed, but it ends nice */
void on_term(int signum) {
	jack_port_unregister(client, input_port);
	jack_port_unregister(client, thru_port);
	jack_deactivate(client);
	jack_ringbuffer_free(midibuffer);
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
	char jackname[16];
	int winsz;
	int win_x = 2000; // default to lower right corner of a single screen
	int win_y = 1000;
	bool help (false);
	bool version (false);
	line1_in[56] = 0x00;
	line2_in[56] = 0x00;
	disp2_in[2] = 0x00;
	time1_in[13] = 0x00;
//	strcpy(time1_in, "000|00| 0|000");
	strcpy(time1_in, "             ");
	tm_bt = '|';
	Chan *chan[8];
	char wname[64];
	Transport *transport;


    struct option options[] = {
	{ "help", no_argument, 0, 'h' },
	{ "master", no_argument, 0, 'm' },
	{ "time", no_argument, 0, 't' },
	{ "small", no_argument, 0, 's' },
	{ "xpos", required_argument, 0, 'x' },
	{ "ypos", required_argument, 0, 'y' },
	{ "version", no_argument, 0, 'V' },
	};

	while (1) {
	int c, option_index = 0;

	c = getopt_long (argc, argv, "hmtsx:y:V", options, &option_index);
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
		case 's':
			siz = 2;
			break;
		case 'x':
			if (optarg) {
				win_x = stoi(strdup(optarg), 0, 10);
			} else {
				usage();
				return -1;
			}
			break;
		case 'y':
			if (optarg) {
				win_y = stoi(strdup(optarg), 0, 10);
			} else {
				usage();
				return -1;
			}
			break;
		case 'V':
			version = true;
			break;
	    default:
			usage();
			return -1;
		}
	}

	Fl_Output disp2(siz * 184, 0, siz * 20, siz * 14, "");
	Fl_Output time1(siz * 204, 0, siz * 110,siz * 14, "");

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

	char *jname = jack_get_client_name (client);
	char pname[16];
	strcpy (pname, jname);
	strcat (pname, "_in");

	input_port = jack_port_register (client, pname, JACK_DEFAULT_MIDI_TYPE, (JackPortIsInput | JackPortIsTerminal | JackPortIsPhysical), 0);
	strcpy (pname, jname);
	strcat (pname, "_thru");
	thru_port = jack_port_register (client, pname, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

	/* set up midi buffer */
	midibuffer = jack_ringbuffer_create( 16384 );
	int res = jack_ringbuffer_mlock(midibuffer);
	if ( res ) {
		std::cout << "Error locking midi memory!\n";
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


	strcpy (wname,"Mackie Control Display Emulator - ");
	// add jack port name to window title
	strcat (wname, jname);

	// lets make a window
	if(master) {
		winsz = siz * 314;
	} else {
		winsz = siz * 184;
	}
	Fl_Window win (win_x, win_y, winsz, siz * 25, wname);
	win.callback(close_cb);
	win.color(56);
		win.begin();
			for ( int x=0; x < 8; x++) {
				Chan *led = new Chan((x * siz * 23), 0);
				chan[x] = led;
			}
			if(master) {
				// Two char display
				win.add(disp2);
				disp2.color(64);
				disp2.textfont(5);
				disp2.textcolor(88);
				disp2.textsize(siz * 14 - 1);
				disp2.value(disp2_in);
				transport = new Transport(siz * 186, siz * 14);
				if(shotime) {
					// timecode/bar display
					win.add(time1);
					time1.color(64);
					time1.textfont(5);
					time1.textcolor(88);
					time1.textsize(siz * 14 - 1);
					time1.value(time1_in);
				} else {
					// stuff that only shows when time doesn't
				}
			}

		win.end();
	win.show ();


	/* run until interrupted */
	while(1)
	{
		// .03 gives about the right delay for
		// meters to go from FS to 0 in 1.8 seconds
		// but we now count to 100 so we can run this loop more often
		Fl::wait(.003);
		// need to make sure we get all the the midi events
		int availableMidi (1);
		while (availableMidi) {
		// make sure there is at least one byte available
		availableMidi = jack_ringbuffer_read_space(midibuffer);
		if( availableMidi > 0 ) {
			char bytes (0);
			// just peek at it in case read space is < bytes
			int read = jack_ringbuffer_peek(midibuffer, &bytes, 1 );
			if (read) {
				// this is painful need to make function calls for each event type
				availableMidi = jack_ringbuffer_read_space(midibuffer);
				if( availableMidi >= bytes ) {
					// first read one byte to to set pointer then read data
					int read = jack_ringbuffer_read(midibuffer, &bytes, 1 );
					read = jack_ringbuffer_read(midibuffer, (char*)midichunk, bytes);
					if (read == bytes) {
						// parse events next
						switch ((unsigned char) midichunk[0]) {
						case 0xf0:
							if (midichunk[5] == 0x12) {
								// display stuff (should be a function)
								bool line1 = false;
								bool line2 = false;
								int offset = midichunk[6];
								if (offset < 56) {
									if ((bytes - 8 + offset) < 57) {
										memcpy(&line1_in[offset], &midichunk[7], bytes - 8);
										line1 = true;
									} else {
										memcpy(&line1_in[offset], &midichunk[7], 56 - offset);
										line1 = true;
										memcpy(&line2_in[0], &midichunk[7 + (55 - offset)], bytes - (56 - offset));
										line2 = true;
									}

								} else {
									offset = offset - 56;
									if ((bytes - 8 + offset) < 57) {
										memcpy(&line2_in[offset], &midichunk[7], bytes - 8);
										line2 = true;
									} else {
										memcpy(&line2_in[offset], &midichunk[7], 56 - offset);
										line2 = true;
									}
								}
								if (line1) {
									for ( int x=0; x < 8; x++) {
										char text[8];
										memcpy (text, &line1_in[x * 7], 7);
										text[7] = 0x00;
										chan[x]->top(text);
									}
								}
								if (line2) {
									for ( int x=0; x < 8; x++) {
										char text[8];
										memcpy (text, &line2_in[x * 7], 7);
										text[7] = 0x00;
										chan[x]->low(text);
									}
								}
							} else if (midichunk[5] == 0x10) {
								// time code all at once Should be a function
								int p = 12;
								for (int i = 6; i < 16; i++) {
									time1_in[p] = midichunk[i] & 0x03;
									if (time1_in[p] == 0x00) {
										time1_in[p] = 0x20;
									}
									if (p == 10 || p == 7 || p == 4) {
										// skip position 9,6 and 3
										p--;
									}
									p--;
								}
							}
							break;
						case 0x90:
							// now display "Lamps"
							//these are button events (make function)
							if( midichunk[1] < 8 ) {
								if (midichunk[2] == 0) {
									chan[(int) midichunk[1]]->rec(false);
								} else {
									chan[(int)midichunk[1]]->rec(true);
								}
							} else if ( midichunk[1] < 16 ) {
								/* Lamps 8 - 15 are PFL (Solo?) buttons */
								if (midichunk[2] == 0) {
									chan[(int) midichunk[1] - 8]->sol(false);
								} else {
									chan[(int) midichunk[1] - 8]->sol(true);
								}
							} else if ( midichunk[1] < 24 ) {
								/* Lamps 16 - 23 are Mute buttons */
								if (midichunk[2] == 0) {
									chan[(int) midichunk[1] - 16]->mute(false);
								} else {
									chan[(int) midichunk[1] - 16]->mute(true);
								}
							} else if ( midichunk[1] < 32 ) {
								/* Lamps 24 - 31 are channel select indicators */
								if (midichunk[2] == 0) {
									chan[(int) midichunk[1] - 24]->sel(false);
								} else {
									chan[(int) midichunk[1] - 24]->sel(true);
								}
							} else if (master) { // anything else is a master

								switch ((unsigned char) midichunk[1]) {
								case 0x5b:
									// rewind «⏪
									transport->rw(midichunk[2]);
									break;
								case 0x5c:
									// fwd »⏩
									transport->ff(midichunk[2]);
									break;
								case 0x5d:
									transport->stop(midichunk[2]);
									// stop∎■⬛
									break;
								case 0x5e:
									// play‣▶
									transport->play(midichunk[2]);
									break;
								case 0x5f:
									// master record enable
									transport->rec(midichunk[2]);
									break;
								case 0x73:
									transport->solo(midichunk[2]);
									// solo
									break;
								case 0x32:
									transport->flip(midichunk[2]);
									// Flip
									break;
								case 0x33:
									// global view
									transport->view(midichunk[2]);
									break;
								case 0x28:
									// track (Trim)
									transport->track(midichunk[2]);
									break;
								case 0x29:
									// Send
									transport->send(midichunk[2]);
									break;
								case 0x2a:
									// Pan
									transport->pan(midichunk[2]);
									break;
								case 0x2b:
									// Plug-in
									transport->plug(midichunk[2]);
									break;
								case 0x2c:
									// EQ
									transport->eq(midichunk[2]);
									break;
								case 0x2d:
									// Instrument
									transport->inst(midichunk[2]);
									break;
								// these next two are really shotime only, but don't hurt anything
								case 0x72:	// time display is time
									if(midichunk[2] == 0) tm_bt = ':';
									break;
								case 0x71:	// time display is beats and bars
									if(midichunk[2] == 0) tm_bt = '|';
									break;
								default:
								break;
							}
							if (!shotime) {
								// if time is off we have room for more lamps
								// some day I might even add them  :)
								switch ((int) midichunk[1]) {
								case 0x4a:
									// read/off
								case 0x4b:
									// write
								case 0x4c:
									// trim (not trim pot)
								case 0x4d:
									// touch
								case 0x4e:
									// latch
								case 0x4f:
									// group
								case 0x50:
									//save
								case 0x51:
									// undo
								case 0x54:
									// marker
								case 0x55:
									// nudge
								case 0x56:
									// cycle
								case 0x57:
									// drop
								case 0x58:
									// replace
								case 0x59:
									// click
								case 0x64:
									// zoom
								case 0x65:
									// scrub
								default:
									break;
								}
							}
						}
							break;
						case 0xd0:
							// this is meters (make function)
							int chm; // meter channel
							int mval; // meter value
							// divide into chm and mval
							mval = midichunk[1] & 0x0f;
							chm = midichunk[1] >> 4;
							if (mval == 0x0e) {
								chan[(int)chm]->peak(true);
								chan[(int)chm]->level(0x0c);
							} else if (mval == 0x0f) {
								chan[(int)chm]->peak(false);
							} else {
								chan[(int)chm]->level(mval);
							}
							break;
						case 0xb0:
							// make function
							if (master) {
								// timecode
								if (midichunk[1] & 0x40) {
									char data1 = 0x20;
									if( midichunk[2] < 0x20 ) {
										data1 = midichunk[2] + 0x40;
										// cludge because some DAWs send @ instead of space
										if(data1 == 0x40) data1 = 0x20;
									} else {
										data1 = midichunk[2];
									}
									switch (midichunk[1]) {
									case 0x4b:	// left assign char
										disp2_in[0] = data1;
										disp2.value(disp2_in);
										break;
									case 0x4a:	// left assign char
										disp2_in[1] = data1;
										disp2.value(disp2_in);
										break;
									// timecode stuff, should maybe not be checked
									// for if not used, if time turned off, no data sent.
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

						default:
							break;
						}
					}
				}
			}
		}
		} // too lazy to reindent all  :)

		//tell meters to decrement
		for (int i = 0; i < 8; i++) {
			chan[i]->decr();
		}


	}
	std::cout << "after while\n";
	cout.flush();
	sleep(10);
	jack_client_close(client);
	exit (0);
}
