#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/*----------------------------------------------------------------------
 * Options: 
 *  --dir <path> : specify a directory for mpd control files.  
 *                 Could be a tmpfs for example.  By default the
 *                 mpd control files are stored in a directory
 *                 called .mpd in the root directory of the mounted
 *                 media.
 *
 * References:
 * Autoconfiguration: http://www.seul.org/docs/autotut/
 * MPD command protocol: http://www.musicpd.org/doc/protocol/
 ----------------------------------------------------------------------*/

typedef struct mpdhotplug_state
{
    char *device_path;
    char *control_path;
    
} mpdhotplug_state;

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
void logprint(const char *fmt, ...)
{
    va_list argp;
    fprintf(stderr, "log: ");
    va_start(argp, fmt);
    vfprintf(stderr, fmt, argp);
    va_end(argp);
    fprintf(stderr, "\n");
}

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
#define ALLOC(t) (t*)malloc(sizeof(t))
mpdhotplug_state *alloc_state()
{
    mpdhotplug_state *state = ALLOC(mpdhotplug_state);
    state->device_path = "/tmp/media/sda1";  // HACK
    state->control_path = "/tmp/media/sda1/.mpd";
    return state;
}

void free_state(mpdhotplug_state *state)
{
    free(state);
}

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
void stop_mpd()
{
    logprint("stop_mpd");
}

/*
 * Create or update the .mpd directory on the target device.
 * 
 */
void init_mpd(mpdhotplug_state *state)
{
    logprint("init_mpd");
    struct stat statbuf;
    int i = stat (state->control_path, &statbuf);
    if (i == 0) {
        // found
        if (S_ISDIR(statbuf.st_mode)) {
            // is directory
            logprint("control dir %s already exists", state->control_path);
        } else {
            // not a directory - fail
            logprint("control dir %s exists and is not a directory", state->control_path);        
        }
    } else {
        // not found - create
        logprint("control dir %s does not exist", state->control_path);
    }
}

/*
 *
 */
void start_mpd()
{
    logprint("starte_mpd");
}

int main(int argc, char **argv) 
{
    mpdhotplug_state *state;
    state = alloc_state();
    stop_mpd(state);
    init_mpd(state);
    start_mpd(state);
    free_state(state);
    return 0;
}

