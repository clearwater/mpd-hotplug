#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "libmpdclient.h"

/*----------------------------------------------------------------------
 * References:
 * Automake: http://www.gnu.org/s/hello/manual/automake/Creating-amhello.html#Creating-amhello
 * MPD command protocol: http://www.musicpd.org/doc/protocol/
 ----------------------------------------------------------------------*/

typedef enum {
  RESULT_SUCCESS = 0,
  RESULT_FAILURE = 1
} result_t;

typedef enum {
  MODE_NONE,
  MODE_ADD,
  MODE_REMOVE
} mode_t;


typedef struct mpdhotplug_state
{
  char *config_dir;
  char *config_file;
  char *pid_file;
  char *mpd_host; 
  char *mpd_bin;
  unsigned mpd_port;
  unsigned mpd_timeout;
  mpd_Connection *mpd_connection;
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

result_t mpd_log_error(mpdhotplug_state *state)
{
  if (state->mpd_connection->error) {
    logprint("mpd error: %s", state->mpd_connection->errorStr);
    mpd_clearError(state->mpd_connection);
    return RESULT_FAILURE;
  } else {
    return RESULT_SUCCESS;
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

void usage(const char *argv0)
{
  error("Usage: %s [add|remove] <udevpath>", argv0);
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

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
int mpd_connect(mpdhotplug_state *state)
{
  int attempts = 5;
  int delay = 1000; // ms
  if (!state->mpd_connection) {
    while (attempts-- > 0) {
      state->mpd_connection = mpd_newConnection(state->mpd_host,
						state->mpd_port,
						state->mpd_timeout);
      if (state->mpd_connection->error) {
	logprint("Connection to %s:%d failed (%s), %d attempts", 
		 state->mpd_host,
		 state->mpd_port,
		 state->mpd_connection->errorStr,
		 attempts);
      } else {
	return RESULT_SUCCESS;
      }
      usleep(delay * 1000);  // units are microseconds
    }
    return RESULT_FAILURE;
  }
  return RESULT_SUCCESS; // already connected
}

void mpd_disconnect(mpdhotplug_state *state)
{
    if (state->mpd_connection) {
        mpd_closeConnection(state->mpd_connection);
        state->mpd_connection = NULL;
    }
}

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
#define ALLOC(t) (t*)malloc(sizeof(t))

mpdhotplug_state *state_alloc()
{
    mpdhotplug_state *state = ALLOC(mpdhotplug_state);
    state->config_dir = "/media/ram/mpd";
    state->pid_file = path_join_alloc(state->config_dir, "mpd.pid");  // LEAKED
    state->config_file = path_join_alloc(state->config_dir, "mpd.conf"); // LEAKED
    state->mpd_host = "localhost"; 
    state->mpd_bin = "/usr/bin/mpd";
    state->mpd_port = 6600;
    state->mpd_timeout = 0;  // default timeout
    state->mpd_connection = NULL;
    return state;
}

void state_free(mpdhotplug_state *state)
{
    mpd_disconnect(state);
    free(state);
}

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/

void make_dir(const char *path, mode_t mode)
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

void ms_sleep(int time_ms)
{
  usleep(time_ms * 1000);
}

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
/*
 * Create or update the .mpd directory on the target device.
 * 
 */
void mpd_write_conf(mpdhotplug_state *state, const char *music_directory)
{
    logprint("configure_mpd");

    // create config dir (if necessary)
    make_dir(state->config_dir, 0777);

    // 
    FILE *config_file = fopen(state->config_file, "w");
    if (config_file) {
      fprintf(config_file, 
	      "port                    \"6600\"\n"
	      "music_directory         \"%s\"\n"
	      "db_file                 \"%s/mpd.db\"\n"
	      "log_file                \"%s/mpd.log\"\n"
	      "pid_file                \"%s/mpd.pid\"\n"
	      "state_file              \"%s/mpd.state\"\n"
	      "\n"
	      "audio_output {\n"
	      "        type        \"alsa\"\n"
	      "        name        \"Default Audio\"\n"
	      "        mixer_type  \"software\"\n"
	      "}\n",
	      music_directory,
	      state->config_dir,
	      state->config_dir,
	      state->config_dir,
	      state->config_dir);
      fclose(config_file);
    } else {
      error("Could not create %s", state->config_file);
    }
}

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
result_t mpd_start(mpdhotplug_state *state)
{
  int attempts = 5;
  int delay = 200;  // ms

  logprint("start_mpd");
  while (attempts-->0) {
    pid_t pid = fork();
    if (pid == 0) {
      // child process
      char * const argv[] = {state->mpd_bin, state->config_file, NULL};
      logprint("launch %s %s",state->mpd_bin, state->config_file);
      execv(state->mpd_bin, argv);
      error("Failed to start daemon: error %d", errno);
    } else {
      // parent process - waits for daemon to detach
      int status;
      if (wait(&status) != pid) {
	logprint("Error checking new daemon process");
      } else if (!WIFEXITED(status) || WEXITSTATUS(status)!=0) { 
	logprint("Error starting new daemon process");
      } else {
	return RESULT_SUCCESS;  // succeeded
      }
    }
    ms_sleep(delay);  // units are microseconds
  }
  return RESULT_FAILURE;
}

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
result_t mpd_play(mpdhotplug_state *state)
{
  int attempts = 10;
  int delay = 250;
  while (attempts-- > 0) {

    ms_sleep(delay);
    if (mpd_connect(state)) {
      logprint("Connected");
      mpd_sendCommandListBegin(state->mpd_connection);
      mpd_sendUpdateCommand(state->mpd_connection, NULL);
      mpd_sendClearCommand(state->mpd_connection);
      mpd_sendAddCommand(state->mpd_connection, "");
      mpd_sendNextCommand(state->mpd_connection);
      mpd_sendCommandListEnd(state->mpd_connection);
      mpd_log_error(state);
      mpd_finishCommand(state->mpd_connection);
      result_t result = mpd_log_error(state);
      mpd_disconnect(state);
      if (result == RESULT_SUCCESS) {
	return result;
      }
    }
  }
  return RESULT_FAILURE;
}

/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
pid_t pid_read(char *filename)
{
  int buflen = 256;
  pid_t pid = 0;
  char buf[buflen];
  FILE *pidfile = fopen(filename, "r");
  if (pidfile) {
    int len = fread(buf, 1, buflen, pidfile);
    if (len>0) {
      pid = atoi(buf);
    }
    fclose(pidfile);
  }
  logprint("pid from %s is %d",filename, pid);
  return pid;
}

result_t pid_kill(pid_t pid, int signal)
{
  int attempts = 4;
  int delay = 250; // ms
  while (attempts-- > 0) {
    logprint("Sending signal %d to process %d",signal,pid);
    if (-1 == kill(pid, signal) && errno == ESRCH) {
      logprint("Process %d has exited",signal,pid);
      return RESULT_SUCCESS;  // gone
    }
    if (errno != 0) {
      logprint("Error %d killing daemon",errno);
    }
    ms_sleep(delay);
  }
  logprint("Process %d refused to die",signal,pid);
  return RESULT_FAILURE;
}


// argv will contain a string like this:
//   add /devices/platform/stmp3xxx-usb/fsl-ehci/usb1/1-1/1-1.3/1-1.3:1.0/host4/target4:0:0/4:0:0:0/block/sda/sda1
// and we return the last component formatted like this:
//   /media/sda1
char *mount_name(const char *devname)
{
  const char *p = devname + strlen(devname);
  while (p > devname && p[-1]!='/') p--;
  int plen = strlen(p);
  char *mount = (char*)malloc(plen + 8);  // '/media/+<p>+\0
  sprintf(mount, "/media/%s", p);
  return mount;
}

result_t mount_wait(const char *mount)
{
  int attempts = 10;
  int timeout = 500; // ms
  int mountlen = strlen(mount);

  while (attempts-- > 0) {
    // looking for this line: /dev/sda1 /media/sda1 vfat rw,relatime,fmask=0022,dmask=0022,codepage=cp437,iocharset=iso8859-1 0 0
    FILE *file = fopen("/proc/mounts","r");
    int bufsize = 8096;
    char buf[bufsize];
    if (file) {
      while (fgets(buf, bufsize, file)) {
	// find the 2nd word which is the mount point
	char *p0 = buf;  // p0 is start of 2nd word
	while (*p0 && *p0!=' ') p0++;
	if (*p0) p0++;
	char *p1 = p0+1; // p1 is char after 2nd word
	while (*p1 && *p1!=' ') p1++;
	// printf("%s:%d\n", p0,p1-p0);
	if (p1-p0==mountlen && !strncmp(mount, p0, mountlen)) {
	  return RESULT_SUCCESS;
	}
      }
      fclose(file);
    } else {
      error("Could not open /proc/mounts");
    }
    ms_sleep(timeout);
  }
  return RESULT_FAILURE;
}
/*----------------------------------------------------------------------
  
  ----------------------------------------------------------------------*/
int main(int argc, char **argv) 
{
  mode_t mode = MODE_NONE;

  if (argc != 3) {
    usage(argv[0]);
  }

  if (!strcmp(argv[1],"add")) {
    mode = MODE_ADD;
  } else if (!strcmp(argv[1],"remove")) {
    mode = MODE_REMOVE;
  } else {
    usage(argv[0]);
  }

  mpdhotplug_state *state;
  state = state_alloc();

  // create working directory if necessary
  make_dir(state->config_dir, 0777);

  // if process is running, signal it and wait for it to exit
  logprint("Killing old daemon if there is one...");
  int pid = pid_read(state->pid_file);
  if (pid>0) {
    pid_kill(pid, SIGINT);
  }

  if (mode == MODE_ADD) {
    // determine which file system was added
    // argv = binname action device
    char *mount = mount_name(argv[2]);
    mount = "/dev/shm";  // HACK REMOVE ME
    logprint("Waiting for %s to be mounted", mount);
    if (RESULT_FAILURE == mount_wait(mount)) {
      error("Timeout waiting for %s to be mounted", mount);
    }

    logprint("Generating config file");
    // generate mpd config file
    mpd_write_conf(state, mount);
    // restart the daemon
    if (RESULT_FAILURE == mpd_start(state)) {
      error("Failure starting daemon process");
    }
    // connect, rescan and reload
    logprint("Bumping player to start playing");
    mpd_play(state);
  }
  
  // cleanup
  state_free(state);

  return 0;
}

