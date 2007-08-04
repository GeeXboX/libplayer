/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2007 Mathieu Schroeter <mathieu.schroeter@gamesover.ch>
 *
 * This file is part of libplayer.
 *
 * libplayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * libplayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libplayer; if not, write to the Free Software
 * Foundation, Inc, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdlib.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>       /* strstr strlen memcpy strdup */
#include <stdarg.h>       /* va_start va_end */
#include <unistd.h>       /* pipe fork close dup2 */
#include <math.h>         /* rintf */
#include <sys/wait.h>     /* waitpid */
#include <sys/types.h>
#include <sys/stat.h>     /* stat */
#include <signal.h>       /* sigaction */
#include <pthread.h>      /* pthread_... */
#include <semaphore.h>    /* sem_post sem_wait sem_init */

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "wrapper_mplayer.h"
#include "x11_common.h"

#define MODULE_NAME "mplayer"

#define SLAVE_CMD_BUFFER 256

#define MPLAYER_DVDNAV_UP     1
#define MPLAYER_DVDNAV_DOWN   2
#define MPLAYER_DVDNAV_LEFT   3
#define MPLAYER_DVDNAV_RIGHT  4
#define MPLAYER_DVDNAV_MENU   5
#define MPLAYER_DVDNAV_SELECT 6

/* Status of MPlayer child */
typedef enum mplayer_status {
  MPLAYER_IS_IDLE,
  MPLAYER_IS_DEAD
} mplayer_status_t;

/* property and value for a search in the fifo_out */
typedef struct mp_search_s {
  char *property;
  char *value;
} mp_search_t;

/* player specific structure */
typedef struct mplayer_s {
  mplayer_status_t status;
  pid_t pid;          /* process pid */
  int pipe_in[2];     /* pipe for send commands to MPlayer */
  int pipe_out[2];    /* pipe for receive results */
  FILE *fifo_in;      /* fifo on the pipe_in  (write only) */
  FILE *fifo_out;     /* fifo on the pipe_out (read only) */
  /* specific to thread */
  pthread_t th_fifo;      /* thread for the fifo_out parser */
  pthread_mutex_t mutex;
  sem_t sem;
  mp_search_t *search;    /* use when a property is searched */
} mplayer_t;

/* slave commands */
typedef enum slave_cmd {
  SLAVE_DVDNAV,       /* dvdnav int */
  SLAVE_GET_PROPERTY, /* get_property string */
  SLAVE_LOADFILE,     /* loadfile string [int] */
  SLAVE_PAUSE,        /* pause */
  SLAVE_QUIT,         /* quit [int] */
  SLAVE_SEEK,         /* seek float [int] */
  SLAVE_SET_PROPERTY, /* set_property string string */
  SLAVE_STOP
} slave_cmd_t;

/* slave properties */
typedef enum slave_property {
  PROPERTY_AUDIO_BITRATE,
  PROPERTY_AUDIO_CODEC,
  PROPERTY_CHANNELS,
  PROPERTY_HEIGHT,
  PROPERTY_LOOP,
  PROPERTY_METADATA_ALBUM,
  PROPERTY_METADATA_ARTIST,
  PROPERTY_METADATA_GENRE,
  PROPERTY_METADATA_TITLE,
  PROPERTY_METADATA_TRACK,
  PROPERTY_METADATA_YEAR,
  PROPERTY_MUTE,
  PROPERTY_SAMPLERATE,
  PROPERTY_VIDEO_BITRATE,
  PROPERTY_VIDEO_CODEC,
  PROPERTY_VOLUME,
  PROPERTY_WIDTH
} slave_property_t;


static const struct {
  slave_property_t property;
  char *text;
} g_slave_props[] = {
  {PROPERTY_AUDIO_BITRATE,    "audio_bitrate"},
  {PROPERTY_AUDIO_CODEC,      "audio_codec"},
  {PROPERTY_CHANNELS,         "channels"},
  {PROPERTY_HEIGHT,           "height"},
  {PROPERTY_LOOP,             "loop"},
  {PROPERTY_METADATA_ALBUM,   "metadata/album"},
  {PROPERTY_METADATA_ARTIST,  "metadata/artist"},
  {PROPERTY_METADATA_GENRE,   "metadata/genre"},
  {PROPERTY_METADATA_TITLE,   "metadata/title"},
  {PROPERTY_METADATA_TRACK,   "metadata/track"},
  {PROPERTY_METADATA_YEAR,    "metadata/year"},
  {PROPERTY_MUTE,             "mute"},
  {PROPERTY_SAMPLERATE,       "samplerate"},
  {PROPERTY_VIDEO_BITRATE,    "video_bitrate"},
  {PROPERTY_VIDEO_CODEC,      "video_codec"},
  {PROPERTY_VOLUME,           "volume"},
  {PROPERTY_WIDTH,            "width"}
};


static void
sig_handler (int signal)
{
  if (signal == SIGPIPE)
    plog (MODULE_NAME, "SIGPIPE detected by the death of MPlayer");
}

/**
 * Get the text for a specific property in the g_slave_props. This function
 * should never return NULL. If that is the case, then a property is missed
 * in the table.
 */
static char *
get_prop (slave_property_t property)
{
  int i;

  for (i = 0; i < sizeof (g_slave_props) / sizeof (g_slave_props[0]); i++)
    if (g_slave_props[i].property == property)
      return g_slave_props[i].text;

  return NULL;
}

/**
 * Send a formatted command to the MPlayer's slave. fifo_in is a file
 * descriptor on a pipe for the MPlayer's stdin.
 */
static void
send_to_slave (mplayer_t *mplayer, const char *format, ...)
{
  va_list va;

  if (!mplayer || !mplayer->fifo_in)
    return;

  va_start (va, format);
  vfprintf (mplayer->fifo_in, format, va);
  fprintf (mplayer->fifo_in, "\n");
  fflush (mplayer->fifo_in);
  va_end (va);
}

/**
 * Thread for parse the fifo_out of MPlayer. This thread must be used only
 * with slave_result() when a property is needed. The rest of the time,
 * this thread will just respond to some events.
 */
static void *
thread_fifo (void *arg)
{
  char buffer[SLAVE_CMD_BUFFER];
  char *its, *ite;
  player_t *player;
  mplayer_t *mplayer;

  player = (player_t *) arg;

  if (player) {
    mplayer = (mplayer_t *) player->priv;

    if (mplayer && mplayer->fifo_out) {
      /* MPlayer's stdout parser */
      while (fgets (buffer, SLAVE_CMD_BUFFER, mplayer->fifo_out))
      {
        /* lock the mutex for protect mplayer->search */
        pthread_mutex_lock (&mplayer->mutex);
        /* search the result for a property */
        if (mplayer->search && mplayer->search->property &&
            (its = strstr(buffer, mplayer->search->property)))
        {
          /* value start */
          its += strlen (mplayer->search->property);
          ite = its;
          while (*ite != '\0' && *ite != '\n')
            ite++;

          /* value end */
          *ite = '\0';

          if ((mplayer->search->value = malloc (strlen (its) + 1))) {
            memcpy (mplayer->search->value, its, strlen (its));
            *(mplayer->search->value + strlen (its)) = '\0';
          }
        }
        /* If this error (from stderr) exists, then we can go out
        * because there is no result for the real command.
        */
        else if (strstr (buffer, "Command loadfile")) {
          if (mplayer->search) {
            free (mplayer->search->property);
            mplayer->search->property = NULL;
            /* search ended */
            sem_post (&mplayer->sem);
          }
        }
        pthread_mutex_unlock (&mplayer->mutex);

        if (strstr (buffer, "Exiting")) {
          mplayer->status = MPLAYER_IS_DEAD;
          break;
        }
      }
    }
  }

  pthread_exit (0);
}

/**
 * Send a command to slave for get a property. That will return nothing
 * because the response is grabbed with slave_result().
 */
static void
slave_get_property (player_t *player, slave_property_t property)
{
  mplayer_t *mplayer = NULL;
  char *prop;

  if (!player)
    return;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer || !mplayer->fifo_in)
    return;

  /* get the right property in the global list */
  prop = get_prop (property);
  if (prop)
    send_to_slave (mplayer, "get_property %s", prop);
  else
    /* should never going here */
    return;
}

/**
 * Get results of commands sent to MPlayer. The MPlayer's stdout and sterr
 * are connected to fifo_out. This function uses the thread_fifo for get
 * the result. That uses semaphore and mutex.
 */
static char *
slave_result (slave_property_t property, player_t *player)
{
  char str[SLAVE_CMD_BUFFER];
  char *prop;
  char *ret = NULL;
  mplayer_t *mplayer = NULL;

  if (!player)
    return NULL;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer || !mplayer->fifo_in || !mplayer->fifo_out)
    return NULL;

  /* get the right property in the global list */
  prop = get_prop (property);
  if (prop)
    sprintf (str, "ANS_%s=", prop);
  else
    /* should never going here */
    return NULL;

  /* lock the mutex for protect mplayer->search */
  pthread_mutex_lock (&mplayer->mutex);
  mplayer->search = malloc (sizeof (mp_search_t));

  if (mplayer->search) {
    mplayer->search->property = strdup (str);
    mplayer->search->value = NULL;
    pthread_mutex_unlock (&mplayer->mutex);

    /* send the slave command for get a response from MPlayer */
    slave_get_property (player, property);

    /* NOTE: /!\ That is ugly but it works pretty well :o)
    * Because MPlayer doesn't confirm commands, that is necessary for
    * have a text in the fifo_out for know if there is no new message
    * because MPlayer will not response all the time to get_property.
    *
    * An error is created by the command 'loadfile' without argument. This
    * error is got with fgets() and the search is ended if there is no
    * result for the real command.
    */
    send_to_slave (mplayer, "loadfile");

    /* wait that the thread will found the value */
    sem_wait (&mplayer->sem);

    /* we take the result */
    ret = mplayer->search->value;

    /* the search is ended */
    pthread_mutex_lock (&mplayer->mutex);
    free (mplayer->search);
    mplayer->search = NULL;
    pthread_mutex_unlock (&mplayer->mutex);
  }
  else
    pthread_mutex_unlock (&mplayer->mutex);

  return ret;
}

static int
slave_result_int (player_t *player, slave_property_t property)
{
  int value = -1;
  char *result;

  result = slave_result (property, player);

  if (result) {
    /* string to float and round to int */
    value = (int) rintf (atof (result));
    free (result);
  }

  return value;
}

static inline char *
slave_result_str (player_t *player, slave_property_t property)
{
  return slave_result (property, player);
}

/**
 * Set a value for a property. Currently, only integer can be used as value.
 * If a string must be set a day, then this function must be split like
 * slave_get_property() and use two inlined functions for set int or str.
 */
static void
slave_set_property_int (player_t *player, slave_property_t property, int value)
{
  mplayer_t *mplayer = NULL;
  char *prop;
  char cmd[SLAVE_CMD_BUFFER];

  if (!player)
    return;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer || !mplayer->fifo_in)
    return;

  /* get the right property in the global list */
  prop = get_prop (property);
  if (prop)
    sprintf (cmd, "set_property %s", prop);
  else
    /* should never going here */
    return;

  switch (property) {
  case PROPERTY_LOOP:
  case PROPERTY_MUTE:
    send_to_slave (mplayer, "%s %i", cmd, value);
    break;

  case PROPERTY_VOLUME:
    send_to_slave (mplayer, "%s %.2f", cmd, (float) value);
    break;

  default:
    return;
  }
}

static inline int
slave_get_property_int (player_t *player, slave_property_t property)
{
  int res;

  res = slave_result_int (player, property);
  return res;
}

static inline char *
slave_get_property_str (player_t *player, slave_property_t property)
{
  char *res;

  res = slave_result_str (player, property);
  return res;
}

static void
slave_action (player_t *player, slave_cmd_t cmd, void *value)
{
  mplayer_t *mplayer = NULL;

  if (!player)
    return;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer || !mplayer->fifo_in)
    return;

  switch (cmd) {
  case SLAVE_DVDNAV:
    send_to_slave (mplayer, "dvdnav %i", *((int *) value));
    break;

  case SLAVE_LOADFILE:
    if (player->mrl->name)
      send_to_slave (mplayer, "loadfile \"%s\" %i",
                     player->mrl->name, *((int *) value));
    break;

  case SLAVE_PAUSE:
    send_to_slave (mplayer, "pause");
    break;

  case SLAVE_QUIT:
    send_to_slave (mplayer, "quit");
    break;

  case SLAVE_SEEK:
    send_to_slave (mplayer, "seek %.2f 0", *((float *) value));
    break;

  case SLAVE_STOP:
    slave_set_property_int (player, PROPERTY_LOOP, -1);
    send_to_slave (mplayer, "seek 100.00 1");
    break;

  default:
    return;
  }
}

static inline void
slave_cmd (player_t *player, slave_cmd_t cmd)
{
  slave_action (player, cmd, NULL);
}

static inline void
slave_cmd_int (player_t *player, slave_cmd_t cmd, int value)
{
  slave_action (player, cmd, &value);
}

/**
 * Identify a stream for complete player->w and player->h attributes. These
 * are necessary for that Xv can use a right aspect.
 */
static void
mp_identify (player_t *player)
{
  char *params[16];
  char buffer[SLAVE_CMD_BUFFER];
  char *its, *ite;
  int pp = 0;
  int mp_pipe[2];
  FILE *mp_fifo;
  pid_t pid;

  if (!player || !player->mrl || !player->mrl->name)
    return;

  /* create pipe */
  if (pipe (mp_pipe) != -1) {
    /* create the fork */
    pid = fork ();

    switch (pid) {
    /* the son (a new hope) */
    case 0:
      close (mp_pipe[0]);

      /* connect stdout and stderr to the pipe */
      dup2 (mp_pipe[1], STDERR_FILENO);
      dup2 (mp_pipe[1], STDOUT_FILENO);

      params[pp++] = "mplayer";
      params[pp++] = "-quiet";
      params[pp++] = "-vo";
      params[pp++] = "null";
      params[pp++] = "-ao";
      params[pp++] = "null";
      params[pp++] = "-nolirc";
      params[pp++] = "-nojoystick";
      params[pp++] = "-noconsolecontrols";
      params[pp++] = "-frames";
      params[pp++] = "0";
      params[pp++] = strdup (player->mrl->name);
      params[pp++] = "-identify";
      params[pp] = NULL;

      /* launch MPlayer, if execvp returns there is an error */
      execvp ("mplayer", params);

    case -1:
      break;

    /* I'm your father */
    default:
      close (mp_pipe[1]);

      mp_fifo = fdopen (mp_pipe[0], "r");

      while (fgets (buffer, SLAVE_CMD_BUFFER, mp_fifo)) {
        if ((its = strstr (buffer, "ID_VIDEO_WIDTH="))) {
          /* value start */
          its += 15;
          ite = its;
          while (*ite != '\0' && *ite != '\n')
            ite++;
          /* value end */
          *ite = '\0';

          player->w = atoi (its);
        }
        else if ((its = strstr (buffer, "ID_VIDEO_HEIGHT="))) {
          /* value start */
          its += 16;
          ite = its;
          while (*ite != '\0' && *ite != '\n')
            ite++;
          /* value end */
          *ite = '\0';

          player->h = atoi (its);
        }
        /* stop fgets because MPlayer is ended */
        else if (strstr (buffer, "Exiting"))
          break;
      }

      /* wait the death of MPlayer */
      waitpid (pid, NULL, 0);

      /* close pipe and fifo */
      close (mp_pipe[0]);
      fclose (mp_fifo);
    }
  }
}


/* Use only these commands for speak with MPlayer!
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * void  slave_cmd              (player_t, slave_cmd_t)
 * void  slave_cmd_int          (player_t, slave_cmd_t,      int)
 * int   slave_get_property_int (player_t, slave_property_t)
 * char *slave_get_property_str (player_t, slave_property_t)
 * void  slave_set_property_int (player_t, slave_property_t, int)
 */

/**
 * Init MPlayer as a forked process (son) and speak with libplayer (father)
 * throught two pipes. One for send command to slave mode and a second
 * for get results. MPlayer must not dead, else the pipes are broken and a
 * SIGPIPE is sent to libplayer. In this case, a new init is necessary.
 */
static init_status_t
mplayer_init (player_t *player)
{
  struct sigaction action;
  mplayer_t *mplayer = NULL;
  char *params[32];
  char winid[32];
  int pp = 0;

  plog (MODULE_NAME, "init");

  if (!player)
    return PLAYER_INIT_ERROR;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer)
    return PLAYER_INIT_ERROR;

  /* action for SIGPIPE */
  action.sa_handler = sig_handler;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  sigaction (SIGPIPE, &action, NULL);

  /* The video out is sent in our X11 window, winid is used for -wid arg.
   * The advantage is important, because MPlayer will not create the window
   * then we take the control on X11 events and only libplayer can send
   * commands to MPlayer.
   */
  switch (player->vo) {
  case PLAYER_VO_X11:
  case PLAYER_VO_X11_SDL:
  case PLAYER_VO_XV:
    if (!x11_init (player))
      return PLAYER_INIT_ERROR;
    sprintf (winid, "%li", (long int) player->x11->window);
  default:
    break;
  }

  /* create pipes */
  if (pipe (mplayer->pipe_in) != -1) {
    if (pipe (mplayer->pipe_out) != -1) {
      /* create the fork */
      mplayer->pid = fork ();

      switch (mplayer->pid) {
      /* the son (a new hope) */
      case 0:
        close (mplayer->pipe_in[1]);
        close (mplayer->pipe_out[0]);

        /* connect the pipe to MPlayer's stdin */
        dup2 (mplayer->pipe_in[0], STDIN_FILENO);
        /* connect stdout and stderr to the second pipe */
        dup2 (mplayer->pipe_out[1], STDERR_FILENO);
        dup2 (mplayer->pipe_out[1], STDOUT_FILENO);

        /* default MPlayer arguments */
        params[pp++] = "mplayer";
        params[pp++] = "-slave";            /* work in slave mode */
        params[pp++] = "-quiet";            /* reduce output messages */
        params[pp++] = "-idle";             /* MPlayer stays always alive */
        params[pp++] = "-fs";               /* fullscreen (if possible) */
        params[pp++] = "-zoom";             /* zoom (if possible) */
        params[pp++] = "-ontop";            /* ontop (if possible) */
        params[pp++] = "-noborder";         /* no border decoration */
        params[pp++] = "-nolirc";
        params[pp++] = "-nojoystick";
        params[pp++] = "-nomouseinput";
        params[pp++] = "-nograbpointer";
        params[pp++] = "-noconsolecontrols";

        /* select the video output */
        params[pp++] = "-vo";
        /* TODO: possibility to add parameters for each video output */
        switch (player->vo) {
        case PLAYER_VO_NULL:
          params[pp++] = "null";
          break;

        case PLAYER_VO_X11:
          params[pp++] = "x11";
          /* window ID */
          params[pp++] = "-wid";
          params[pp++] = strdup (winid);
          break;

        /* with MPlayer, SDL is not specific to X11 */
        case PLAYER_VO_X11_SDL:
          params[pp++] = "sdl";
          /* window ID */
          params[pp++] = "-wid";
          params[pp++] = strdup (winid);
          break;

        /* with xv and wid, zoom, fs and aspect have no effect.
         * The image is always scaled on all the window.
         */
        case PLAYER_VO_XV:
          params[pp++] = "xv";
          /* window ID */
          params[pp++] = "-wid";
          params[pp++] = strdup (winid);
          break;

        case PLAYER_VO_FB:
          params[pp++] = "fbdev";
        }

        /* select the audio output */
        params[pp++] = "-ao";
        /* TODO: possibility to add parameters for each audio output */
        switch (player->ao) {
        /* 'null' output seems to be bugged (MPlayer crash in some cases) */
        case PLAYER_AO_NULL:
          params[pp++] = "null";
          break;

        case PLAYER_AO_ALSA:
          params[pp++] = "alsa";
          break;

        case PLAYER_AO_OSS:
          params[pp++] = "oss";
        }

        params[pp] = NULL;

        /* launch MPlayer, if execvp returns there is an error */
        execvp ("mplayer", params);

      case -1:
        break;

      /* I'm your father */
      default:
        close (mplayer->pipe_in[0]);
        close (mplayer->pipe_out[1]);

        mplayer->fifo_in = fdopen (mplayer->pipe_in[1], "w");
        mplayer->fifo_out = fdopen (mplayer->pipe_out[0], "r");

        plog (MODULE_NAME, "MPlayer child loaded");

        mplayer->status = MPLAYER_IS_IDLE;

        /* init semaphore and mutex */
        sem_init (&mplayer->sem, 0, 0);
        pthread_mutex_init (&mplayer->mutex, NULL);

        /* create the thread */
        if (pthread_create (&mplayer->th_fifo, NULL,
                            thread_fifo, (void *) player) >= 0)
          return PLAYER_INIT_OK;
      }
    }
  }

  return PLAYER_INIT_ERROR;
}

static void
mplayer_uninit (player_t *player)
{
  mplayer_t *mplayer = NULL;
  void *ret;

  plog (MODULE_NAME, "uninit");

  if (!player)
    return;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer)
    return;

  if (mplayer && mplayer->fifo_in) {
    /* suicide of MPlayer */
    slave_cmd (player, SLAVE_QUIT);

    /* wait the death of the thread fifo_out */
    (void) pthread_join (mplayer->th_fifo, &ret);

    /* wait the death of MPlayer */
    waitpid (mplayer->pid, NULL, 0);

    mplayer->status = MPLAYER_IS_DEAD;

    /* close pipes */
    close (mplayer->pipe_in[1]);
    close (mplayer->pipe_out[0]);

    /* close fifos */
    fclose (mplayer->fifo_in);
    fclose (mplayer->fifo_out);

    plog (MODULE_NAME, "MPlayer child terminated");

    /* X11 */
    if (player->x11)
      x11_uninit (player);
  }

  free (mplayer);
}

static void
mplayer_mrl_get_audio_properties (player_t *player,
                                  mrl_properties_audio_t *audio)
{
  char *buffer_c;
  int buffer_i;
  mplayer_t *mplayer = NULL;

  if (!player || !audio)
    return;

  mplayer = (mplayer_t *) player->priv;
  /* FIXME: test for know if it's playing or not will be better */
  if (mplayer->status == MPLAYER_IS_DEAD)
    return;

  buffer_c = slave_get_property_str (player, PROPERTY_AUDIO_CODEC);
  if (buffer_c) {
    audio->codec = strdup (buffer_c);
    free (buffer_c);
  }
  if (audio->codec)
    plog (MODULE_NAME, "Audio Codec: %s", audio->codec);

  buffer_i = slave_get_property_int (player, PROPERTY_AUDIO_BITRATE);
  if (buffer_i > -1) {
    audio->bitrate = buffer_i;
    plog (MODULE_NAME, "Audio Bitrate: %i kbps", audio->bitrate / 1000);
  }

  // TODO: autio->bits

  buffer_i = slave_get_property_int (player, PROPERTY_CHANNELS);
  if (buffer_i > -1) {
    audio->channels = buffer_i;
    plog (MODULE_NAME, "Audio Channels: %i", audio->channels);
  }

  buffer_i = slave_get_property_int (player, PROPERTY_SAMPLERATE);
  if (buffer_i > -1) {
    audio->samplerate = buffer_i;
    plog (MODULE_NAME, "Audio Sample Rate: %i Hz", audio->samplerate);
  }
}

static void
mplayer_mrl_get_video_properties (player_t *player,
                                  mrl_properties_video_t *video)
{
  char *buffer_c;
  int buffer_i;
  mplayer_t *mplayer = NULL;

  if (!player || !video)
    return;

  mplayer = (mplayer_t *) player->priv;
  /* FIXME: test for know if it's playing or not will be better */
  if (mplayer->status == MPLAYER_IS_DEAD)
    return;

  buffer_c = slave_get_property_str (player, PROPERTY_VIDEO_CODEC);
  if (buffer_c) {
    video->codec = strdup (buffer_c);
    free (buffer_c);
  }
  if (video->codec)
    plog (MODULE_NAME, "Video Codec: %s", video->codec);

  buffer_i = slave_get_property_int (player, PROPERTY_VIDEO_BITRATE);
  if (buffer_i > -1) {
    video->bitrate = buffer_i;
    plog (MODULE_NAME, "Video Bitrate: %i kbps", video->bitrate / 1000);
  }

  buffer_i = slave_get_property_int (player, PROPERTY_WIDTH);
  if (buffer_i > -1) {
    video->width = buffer_i;
    plog (MODULE_NAME, "Video Width: %i", video->width);
  }

  buffer_i = slave_get_property_int (player, PROPERTY_HEIGHT);
  if (buffer_i > -1) {
    video->height = buffer_i;
    plog (MODULE_NAME, "Video Height: %i", video->height);
  }

  // TODO: audio->channels and audio->streams
}

static void
mplayer_mrl_get_properties (player_t *player)
{
  mplayer_t *mplayer = NULL;
  struct stat st;
  mrl_t *mrl;

  plog (MODULE_NAME, "mrl_get_properties");

  mrl = player->mrl;

  if (!player || !mrl || !mrl->prop)
    return;

  mplayer = (mplayer_t *) player->priv;
  if (mplayer->status == MPLAYER_IS_DEAD)
    return;

  /* now fetch properties */
  stat (mrl->name, &st);
  mrl->prop->size = st.st_size;
  plog (MODULE_NAME, "File Size: %.2f MB",
        (float) mrl->prop->size / 1024 / 1024);

  /* FIXME: no idea how implement */
  // mrl->prop->seekable =

  mrl->prop->audio = mrl_properties_audio_new ();
  mplayer_mrl_get_audio_properties (player, mrl->prop->audio);

  mrl->prop->video = mrl_properties_video_new ();
  mplayer_mrl_get_video_properties (player, mrl->prop->video);
}

static void
mplayer_mrl_get_metadata (player_t *player)
{
  char *buffer_c;
  mplayer_t *mplayer = NULL;
  mrl_t *mrl;

  plog (MODULE_NAME, "mrl_get_metadata");

  mrl = player->mrl;

  if (!player || !mrl || !mrl->meta)
    return;

  mplayer = (mplayer_t *) player->priv;
  if (mplayer->status == MPLAYER_IS_DEAD)
    return;

  /* now fetch metadata */
  buffer_c = slave_get_property_str (player, PROPERTY_METADATA_TITLE);
  if (buffer_c) {
    mrl->meta->title = strdup (buffer_c);
    free (buffer_c);
  }
  if (mrl->meta->title)
    plog (MODULE_NAME, "Meta Title: %s", mrl->meta->title);

  buffer_c = slave_get_property_str (player, PROPERTY_METADATA_ARTIST);
  if (buffer_c) {
    mrl->meta->artist = strdup (buffer_c);
    free (buffer_c);
  }
  if (mrl->meta->artist)
    plog (MODULE_NAME, "Meta Artist: %s", mrl->meta->artist);

  buffer_c = slave_get_property_str (player, PROPERTY_METADATA_GENRE);
  if (buffer_c) {
    mrl->meta->genre = strdup (buffer_c);
    free (buffer_c);
  }
  if (mrl->meta->genre)
    plog (MODULE_NAME, "Meta Genre: %s", mrl->meta->genre);

  buffer_c = slave_get_property_str (player, PROPERTY_METADATA_ALBUM);
  if (buffer_c) {
    mrl->meta->album = strdup (buffer_c);
    free (buffer_c);
  }
  if (mrl->meta->album)
    plog (MODULE_NAME, "Meta Album: %s", mrl->meta->album);

  buffer_c = slave_get_property_str (player, PROPERTY_METADATA_YEAR);
  if (buffer_c) {
    mrl->meta->year = strdup (buffer_c);
    free (buffer_c);
  }
  if (mrl->meta->year)
    plog (MODULE_NAME, "Meta Year: %s", mrl->meta->year);

  buffer_c = slave_get_property_str (player, PROPERTY_METADATA_TRACK);
  if (buffer_c) {
    mrl->meta->track = strdup (buffer_c);
    free (buffer_c);
  }
  if (mrl->meta->track)
    plog (MODULE_NAME, "Meta Track: %s", mrl->meta->track);
}

static playback_status_t
mplayer_playback_start (player_t *player)
{
  plog (MODULE_NAME, "playback_start");

  if (!player)
    return PLAYER_PB_FATAL;

  /* identify the current stream */
  mp_identify (player);

  // FIXME: playback error if not loaded
  /* 0: new play, 1: append to the current playlist */
  slave_cmd_int (player, SLAVE_LOADFILE, 0);

  /* X11 */
  if (player->x11 && mrl_uses_vo (player->mrl))
    x11_map (player);

  return PLAYER_PB_OK;
}

static void
mplayer_playback_stop (player_t *player)
{
  plog (MODULE_NAME, "playback_stop");

  if (!player)
    return;

  /* X11 */
  if (player->x11 && mrl_uses_vo (player->mrl))
    x11_unmap (player);

  slave_cmd (player, SLAVE_STOP);
}

static playback_status_t
mplayer_playback_pause (player_t *player)
{
  plog (MODULE_NAME, "playback_pause");

  if (!player)
    return PLAYER_PB_FATAL;

  slave_cmd (player, SLAVE_PAUSE);

  return PLAYER_PB_OK;
}

static void
mplayer_playback_seek (player_t *player, int value)
{
  plog (MODULE_NAME, "playback_seek: %d", value);

  if (!player)
    return;

  slave_cmd_int (player, SLAVE_SEEK, value);
}

static void
mplayer_playback_dvdnav (player_t *player, player_dvdnav_t value)
{
  char log[8];
  int action;

  switch (value)
  {
  case PLAYER_DVDNAV_UP:
    strcpy (log, "up");
    action = MPLAYER_DVDNAV_UP;
    break;

  case PLAYER_DVDNAV_DOWN:
    strcpy (log, "down");
    action = MPLAYER_DVDNAV_DOWN;
    break;

  case PLAYER_DVDNAV_LEFT:
    strcpy (log, "left");
    action = MPLAYER_DVDNAV_LEFT;
    break;

  case PLAYER_DVDNAV_RIGHT:
    strcpy (log, "right");
    action = MPLAYER_DVDNAV_RIGHT;
    break;

  case PLAYER_DVDNAV_MENU:
    strcpy (log, "menu");
    action = MPLAYER_DVDNAV_MENU;
    break;

  case PLAYER_DVDNAV_SELECT:
    strcpy (log, "select");
    action = MPLAYER_DVDNAV_SELECT;
    break;

  default:
    return;
  }

  plog (MODULE_NAME, "playback_dvdnav: %s", log);

  if (!player)
    return;

  if (player->mrl->type == PLAYER_MRL_TYPE_DVD_NAV)
    slave_cmd_int (player, SLAVE_DVDNAV, action);
}

static int
mplayer_get_volume (player_t *player)
{
  int volume = 0;

  plog (MODULE_NAME, "get_volume");

  if (!player)
    return volume;

  volume = slave_get_property_int (player, PROPERTY_VOLUME);

  if (volume < 0)
    return -1;

  return volume;
}

static player_mute_t
mplayer_get_mute (player_t *player)
{
  plog (MODULE_NAME, "get_mute");

  if (!player)
    return PLAYER_MUTE_UNKNOWN;

  if (slave_get_property_int (player, PROPERTY_MUTE))
    return PLAYER_MUTE_ON;

  return PLAYER_MUTE_OFF;
}

static void
mplayer_set_volume (player_t *player, int value)
{
  plog (MODULE_NAME, "set_volume: %d", value);

  if (!player)
    return;

  slave_set_property_int (player, PROPERTY_VOLUME, value);
}

static void
mplayer_set_mute (player_t *player, player_mute_t value)
{
  int mute = 0;

  if (value == PLAYER_MUTE_UNKNOWN)
    return;

  if (value == PLAYER_MUTE_ON)
    mute = 1;

  plog (MODULE_NAME, "set_mute: %s", mute ? "on" : "off");

  if (!player)
    return;

  slave_set_property_int (player, PROPERTY_MUTE, mute);
}

/* public API */
player_funcs_t *
register_functions_mplayer (void)
{
  player_funcs_t *funcs = NULL;

  funcs = malloc (sizeof (player_funcs_t));
  funcs->init = mplayer_init;
  funcs->uninit = mplayer_uninit;
  funcs->mrl_get_props = mplayer_mrl_get_properties;
  funcs->mrl_get_meta = mplayer_mrl_get_metadata;
  funcs->pb_start = mplayer_playback_start;
  funcs->pb_stop = mplayer_playback_stop;
  funcs->pb_pause = mplayer_playback_pause;
  funcs->pb_seek = mplayer_playback_seek;
  funcs->pb_dvdnav = mplayer_playback_dvdnav;
  funcs->get_volume = mplayer_get_volume;
  funcs->get_mute = mplayer_get_mute;
  funcs->set_volume = mplayer_set_volume;
  funcs->set_mute = mplayer_set_mute;

  return funcs;
}

void *
register_private_mplayer (void)
{
  mplayer_t *mplayer = NULL;

  mplayer = malloc (sizeof (mplayer_t));

  mplayer->status = MPLAYER_IS_DEAD;
  mplayer->fifo_in = NULL;
  mplayer->fifo_out = NULL;
  mplayer->search = NULL;

  return mplayer;
}
