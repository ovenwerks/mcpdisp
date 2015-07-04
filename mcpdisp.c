#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>


#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>


jack_client_t *client;

/* only need input to display things */
jack_port_t *input_port;
/* need two ring buffers for ease of reading */
/* One for LEDs */
jack_ringbuffer_t *ledbuffer = 0;
/* One for text */
jack_ringbuffer_t *textbuffer = 0;

int process(jack_nframes_t nframes, void *arg)
{
    int i;
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
                    if (availableWrite >= 8)
                    {
                      int written = jack_ringbuffer_write( textbuffer, (const char*) (in_event.buffer + 6), 8 );
                      if (written != 8 ) {
                        printf("ERROR! Partial textbuffer write");
                      }
                    }
                    else {
                      printf("textbuffer full skipping");

                  }
                }
            }
            else if( (*(in_event.buffer)) == 0x90 )
            {
                /* button press LED returns */
                if ((*(in_event.buffer+1)) < 32)
                {
                    int availableWrite = jack_ringbuffer_write_space(ledbuffer);
                    if (availableWrite >= 2)
                    {
                      int written = jack_ringbuffer_write( ledbuffer, (const char*) (in_event.buffer + 1), 2 );
                      if (written != 2 ) {
                        printf("ERROR! Partial textbuffer write");
                      }
                    }
                    else {
                      printf("textbuffer full skipping");

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

int main(int narg, char **args)
{
    char on;
    int c;
    char textbit[9];
    int texoff;


    if ((client = jack_client_open ("mcpdisp", JackNullOption, NULL)) == 0)
    {
        fprintf(stderr, "jack server not running?\n");
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
        printf("Error locking LED memory!\n");
        return -1;
          }
    
    /* set up display buffer */
    textbuffer = jack_ringbuffer_create( 1024 );
    res = jack_ringbuffer_mlock(textbuffer);
    if ( res ) {
        printf("Error locking text memory!");
        return -1;
          }


    if (jack_activate (client))
    {
        fprintf(stderr, "cannot activate client");
        return 1;
    }

    /* try to end nice on anything 
    - one of these makes window close work */
    signal(SIGTERM, on_term);    
    signal(SIGINT, on_term);    
    signal(SIGPIPE, on_term);    
    signal(SIGHUP, on_term);    
    signal(SIGQUIT, on_term);    
    signal(SIGKILL, on_term);    
    signal(SIGABRT, on_term);    


    /* clear screen, park the cursor and hide it. Then flush */
    printf("%c[2J", 0x1b);
    printf("%c[3;56H%c[?25l", 0x1b, 0x1b);
    fflush(stdout);


    /* run until interrupted */
    while(1)
    {
        usleep(1000);
        /* first do text fields */
        int availableRead = jack_ringbuffer_read_space(textbuffer);
        if( availableRead >= 8 )
        {
          textbit[8] = '\x00';
          int lp = availableRead / 8;
          for(c=0; c<lp; c++)
          {
            int textres = jack_ringbuffer_read(textbuffer, (char*)textbit, 8 );
            if ( textres != 8 ) {
                printf("WARNING! didn't read full event!/n");
                return -1;
            }
            texoff = textbit[0]+1;
            /* top row */
            if(texoff < 56)
            {
              printf("%c[1;%dH%c[1;37m%s", 0x1b, texoff, 0x1b, &textbit[1] );
            }
            else
            {
              /* bottom row */
              texoff = texoff - 56;
              printf("%c[2;%dH%c[1;37m%s", 0x1b, texoff, 0x1b, &textbit[1] );
            }
          }
          /* park the cursor and hide it, then flush */
          printf("%c[3;56H%c[?25l", 0x1b, 0x1b);
          fflush(stdout);
        }
        
        /* now display "Lamps" */
        availableRead = jack_ringbuffer_read_space(ledbuffer);
        if( availableRead >= 2 )
        { 
          int lp = availableRead / 2;
          for(c=0; c<lp; c++)
          {
            int ledres = jack_ringbuffer_read(ledbuffer, (char*)textbit, 2 );
            if ( ledres != 2 ) {
                printf("WARNING! didn't read full event!/n");
                return -1;
            }
            /* Lamps 0 - 7 are Record enable */
            if( textbit[0] < 8 )
            {
              texoff = (textbit[0] * 7) +2;
              if (textbit[1] == 0) {
                  on = ' ';
              } else {
                  on = 'R';
              }
              printf ("%c[3;%dH%c[1;31m%c", 0x1b, texoff, 0x1b, on);
            } else if ( textbit[0] < 16 )
            {
              /* Lamps 8 - 15 are PFL (Solo?) buttons */
              texoff = ((textbit[0] - 8) * 7) +3;
              if (textbit[1] == 0) {
                  on = ' ';
              } else {
                  on = 'P';
              }
              printf ("%c[3;%dH%c[1;32m%c", 0x1b, texoff, 0x1b, on);
            } else if ( textbit[0] < 24 )
            {
              /* Lamps 16 - 23 are Mute buttons */
              texoff = ((textbit[0] - 16) * 7) +4;
              if (textbit[1] == 0) {
                  on = ' ';
              } else {
                  on = 'M';
              }
              printf ("%c[3;%dH%c[1;33m%c", 0x1b, texoff, 0x1b, on);
            } else if ( textbit[0] < 32 )
            {
              /* Lamps 24 - 31 are channel select indicators */
              texoff = ((textbit[0] - 24) * 7) +1;
              if (textbit[1] == 0) {
                  on = ' ';
              } else {
                  on = 'S';
              }
              printf ("%c[3;%dH%c[1;37m%c", 0x1b, texoff, 0x1b, on);
              }
            }  
            /* park cursor, hide it and flush buffer */
            printf("%c[3;56H%c[?25l", 0x1b, 0x1b);
            fflush(stdout);
        }
    }
    printf("after while\n");
    fflush(stdout);
    sleep(10);
    jack_client_close(client);
    exit (0);
}
