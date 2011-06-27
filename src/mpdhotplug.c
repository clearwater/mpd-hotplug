#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
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
    char *config_file;
    char *mpd_host; 
    char *mpd_bin;
    char *process_name;
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

void log_mpd_print(mpdhotplug_state *state, const char *message)
{
    switch (mpd_connection_get_error(state->mpd_connection)) {
        // HMM - for now just a single case? 
        default:
            logprint("%s: %s", message, mpd_connection_get_error_message(state->mpd_connection));
    }
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

    int c;
    while (c=fgetc(src), !feof(src)) {
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
    state->mpd_bin = "/data/chumby/mpd/mpd-0.16.2/src/mpd";
    state->mpd_host = "localhost";
    state->process_name = "(mpd daemon)";
    state->config_file = "/tmp/media/sda1/.mpd/mpd.config";
    state->mpd_port = 6600;
    state->mpd_timeout = 0;  // default timeout
    state->mpd_connection = NULL;
    return state;
}

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
void connect_mpd(mpdhotplug_state *state)
{
    int retries = 5;
    int delay = 1000; // ms
    enum mpd_error status;
    if (!state->mpd_connection) {
        while (retries-- > 0) {
            state->mpd_connection = mpd_connection_new(state->mpd_host,
                                                       state->mpd_port,
                                                       state->mpd_timeout);
            status = mpd_connection_get_error(state->mpd_connection);
            if (status != MPD_ERROR_SUCCESS) {
                logprint("Connection to %s:%d failed, %d retries", state->mpd_host, state->mpd_port, retries);
            } else {
                break;
            }
            usleep(delay * 1000);  // units are microseconds
        }
        if (status != MPD_ERROR_SUCCESS) {
            log_mpd_print(state, "Error connecting");
        }
    }

}

void disconnect_mpd(mpdhotplug_state *state)
{
    if (state->mpd_connection) {
        mpd_connection_free(state->mpd_connection);
        state->mpd_connection = NULL;
    }
}
/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
void free_state(mpdhotplug_state *state)
{
    disconnect_mpd(state);
    free(state);
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
        disconnect_mpd(state);
    } else {
        logprint("Sent kill to mpd daemon.");
        disconnect_mpd(state);
    }
    // TODO - wait for daemon to die.
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
void configure_mpd(mpdhotplug_state *state)
{
    logprint("configure_mpd");

    // create config dir (if necessary)
    checkdir(state->config_path, 0777);

    // create playlist dir 
    char *playlist_path = path_join_alloc(state->config_path, "playlists");
    checkdir(playlist_path, 0777);
    free(playlist_path);

    // create configuration file
    expand(state->config_template, state->config_file, "%ROOT%", state->device_path);
}

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
void start_mpd(mpdhotplug_state *state)
{
    int retries = 5;
    int delay = 100;  // ms
    logprint("start_mpd");
    while (retries-->0) {
        pid_t pid = fork();
        if (pid == 0) {
            // child process
            char * const argv[] = {state->process_name, state->config_file, NULL};
            execv(state->mpd_bin, argv);
        } else {
            // parent process - waits for daemon to detach
            int status;
            if (wait(&status) != pid) {
                logprint("Error checking new daemon process");
            } else if (!WIFEXITED(status) || WEXITSTATUS(status)!=0) { 
                logprint("Error starting new daemon process");
            } else {
                break;  // succeeded
            }
        }
        usleep(delay * 1000);  // units are microseconds
    }
}

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
void reindex_mpd(mpdhotplug_state *state)
{
    connect_mpd(state);
    if (!mpd_run_update(state->mpd_connection, NULL)) {
        log_mpd_print(state, "Error updating music database");
    }
    if (!mpd_run_clear(state->mpd_connection)) {
        log_mpd_print(state, "Error clearing playlist");
    }
    if (!mpd_run_add(state->mpd_connection, "")) {
        log_mpd_print(state, "Error updating current playlist");
    }
    if (!mpd_run_play(state->mpd_connection)) {
        log_mpd_print(state, "Error starting playing");
    }
}

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
int main(int argc, char **argv) 
{
    mpdhotplug_state *state;
    state = alloc_state();
    stop_mpd(state);
    configure_mpd(state);
    start_mpd(state);
    reindex_mpd(state);
    free_state(state);
    return 0;
}

