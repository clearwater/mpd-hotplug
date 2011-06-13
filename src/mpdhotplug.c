#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include "mpd/client.h"

/*----------------------------------------------------------------------
 * Options: 
 *  --dir <path> : specify a directory for mpd control files.  
 *                 Could be a tmpfs for example.  By default the
 *                 mpd control files are stored in a directory
 *                 called .mpd in the root directory of the mounted
 *                 media.
 *
 * References:
 * Automake: http://www.gnu.org/s/hello/manual/automake/Creating-amhello.html#Creating-amhello
 * MPD command protocol: http://www.musicpd.org/doc/protocol/
 ----------------------------------------------------------------------*/

typedef struct mpdhotplug_state
{
    char *device_path;
    char *config_path;
    char *config_template;
    char *mpd_host; 
    unsigned mpd_port;
    unsigned mpd_timeout;
    struct mpd_connection *mpd_connection;
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
void error(const char *fmt, ...)
{
    va_list argp;
    fprintf(stderr, "error: ");
    va_start(argp, fmt);
    vfprintf(stderr, fmt, argp);
    va_end(argp);
    fprintf(stderr, "\n");
    exit(-1);
}

char *path_join_alloc(const char *a, const char *b)
{
    size_t alen = strlen(a);
    size_t blen = strlen(b);
    char *c = (char *)malloc(alen+1+blen+1);
    strncpy(c, a, alen);
    c[alen]='/';
    strncpy(c+alen+1, b, blen);
    c[alen+1+blen]=0;
    return c;
}

void expand(const char *src_name, const char *dst_name, const char *find, const char *replace)
{
    FILE *src = fopen(src_name, "r");
    if (!src) error("Unable to open template source %s", src_name);
    FILE *dst = fopen(dst_name, "w");
    if (!dst) error("Unable to open template destination %s", dst_name);

    size_t findlen = strlen(find);
    size_t replacelen = strlen(replace);
    const char *next = find;

    while (!feof(src)) {
        int c = fgetc(src);
        if (c==*next) {
            // matched
            next++;
            if (next-find == findlen) {
                // matched entire string
                fwrite(replace, 1, replacelen, dst);
                next = find;
            }
        } else {
            // did not match 
            if (next!=find) {
                // flush partial match
                fwrite(find, 1, next-find, dst);
                next = find;
            }
            fputc(c, dst);
        }
    }
    // flush final partial match
    if (next!=find) {
        fwrite(find, 1, next-find, dst);
    }
    
    fclose(src);
    fclose(dst);
}

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
#define ALLOC(t) (t*)malloc(sizeof(t))
mpdhotplug_state *alloc_state()
{
    mpdhotplug_state *state = ALLOC(mpdhotplug_state);
    state->device_path = "/tmp/media/sda1";  // HACK
    state->config_path = "/tmp/media/sda1/.mpd";
    state->config_template = "templates/mpd.config";
    state->mpd_host = "localhost";
    state->mpd_port = 6600;
    state->mpd_timeout = 0;  // default timeout
    state->mpd_connection = NULL;
    return state;
}

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
void free_state(mpdhotplug_state *state)
{
    if (state->mpd_connection) {
        mpd_connection_free(state->mpd_connection);
    }
    free(state);
}

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
void connect_mpd(mpdhotplug_state *state)
{
    if (!state->mpd_connection) {
        state->mpd_connection = mpd_connection_new(state->mpd_host,
                                                   state->mpd_port,
                                                   state->mpd_timeout);
        if (!state->mpd_connection) {
            error("Connection to %s:%d failed", state->mpd_host, state->mpd_port);
        }
    }
}
/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
void stop_mpd(mpdhotplug_state *state)
{
    logprint("stop_mpd");
    connect_mpd(state);
    bool ok = mpd_send_command(state->mpd_connection, "kill", NULL);
    if (!ok) {
        logprint("Error killing mpd daemon - maybe already gone?");
    } else {
        logprint("Sent kill to mpd daemon.");
    }
    // TODO - confirm dead
}


void checkdir(const char *path, mode_t mode)
{
    struct stat statbuf;
    int i = stat (path, &statbuf);
    if (i == 0) {
        // found
        if (S_ISDIR(statbuf.st_mode)) {
            // is directory
            logprint("dir %s already exists", path);
        } else {
            // not a directory - fail
            error("dir %s exists and is not a directory", path);
        }
    } else {
        // not found - create
        logprint("dir %s does not exist", path);
        if (mkdir(path, mode)) {
            error("could not create dir %s", path);
        }
    }
}

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
/*
 * Create or update the .mpd directory on the target device.
 * 
 */
void init_mpd(mpdhotplug_state *state)
{
    logprint("init_mpd");

    // create config dir (if necessary)
    checkdir(state->config_path, 0777);

    // create playlist dir 
    char *playlist_path = path_join_alloc(state->config_path, "playlists");
    checkdir(playlist_path, 0777);
    free(playlist_path);

    // create configuration file
    char *config_file = path_join_alloc(state->config_path, "mpd.config");
    expand(state->config_template, config_file, "%ROOT%", state->device_path);
    free(config_file);
}

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
void start_mpd()
{
    logprint("start_mpd");
}

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
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

