/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2007-2008 Mathieu Schroeter <mathieu.schroeter@gamesover.ch>
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
#include <semaphore.h>    /* sem_post sem_wait sem_init sem_destroy */

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
  MPLAYER_IS_PLAYING,
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
  pthread_mutex_t mutex_search;
  pthread_mutex_t mutex_status;
  sem_t sem;
  mp_search_t *search;    /* use when a property is searched */
} mplayer_t;

/* union for set_property */
typedef union slave_value {
  int i_val;
  float f_val;
} slave_value_t;

/* slave commands */
typedef enum slave_cmd {
  SLAVE_DVDNAV,       /* dvdnav int */
  SLAVE_GET_PROPERTY, /* get_property string */
  SLAVE_LOADFILE,     /* loadfile string [int] */
  SLAVE_PAUSE,        /* pause */
  SLAVE_QUIT,         /* quit [int] */
  SLAVE_SEEK,         /* seek float [int] */
  SLAVE_SET_PROPERTY, /* set_property string string */
  SLAVE_STOP,
  SLAVE_SUB_LOAD      /* sub_load string */
} slave_cmd_t;

/* slave properties */
typedef enum slave_property {
  PROPERTY_UNKNOWN,
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
  PROPERTY_SUB,
  PROPERTY_SUB_DELAY,
  PROPERTY_SUB_VISIBILITY,
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
  {PROPERTY_SUB,              "sub"},
  {PROPERTY_SUB_DELAY,        "sub_delay"},
  {PROPERTY_SUB_VISIBILITY,   "sub_visibility"},
  {PROPERTY_VIDEO_BITRATE,    "video_bitrate"},
  {PROPERTY_VIDEO_CODEC,      "video_codec"},
  {PROPERTY_VOLUME,           "volume"},
  {PROPERTY_WIDTH,            "width"}
};


static void
sig_handler (int signal)
{
  if (signal == SIGPIPE)
    fprintf (stderr, "SIGPIPE detected by the death of MPlayer");
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

static char *
parse_field (char *line, char *field)
{
  char *its, *ite;

  its = line;

  /* value start */
  its += strlen (field);
  ite = its;
  while (*ite != '\0' && *ite != '\n')
    ite++;

  /* value end */
  *ite = '\0';

  return its;
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
  char *it;
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
        pthread_mutex_lock (&mplayer->mutex_search);
        /* search the result for a property */
        if (mplayer->search && mplayer->search->property &&
            (it = strstr(buffer, mplayer->search->property)))
        {
          it = parse_field (it, mplayer->search->property);

          if ((mplayer->search->value = malloc (strlen (it) + 1))) {
            memcpy (mplayer->search->value, it, strlen (it));
            *(mplayer->search->value + strlen (it)) = '\0';
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
        pthread_mutex_unlock (&mplayer->mutex_search);

        if (strstr (buffer, "EOF code: 1")) {
          pthread_mutex_lock (&mplayer->mutex_status);
          /* when the stream is ended without stop action */
          if (mplayer->status == MPLAYER_IS_PLAYING) {
            plog (player, PLAYER_MSG_INFO,
                  MODULE_NAME, "Playback of stream has ended");
            mplayer->status = MPLAYER_IS_IDLE;
            pthread_mutex_unlock (&mplayer->mutex_status);

            if (player->event_cb)
              player->event_cb (PLAYER_EVENT_PLAYBACK_FINISHED, NULL);
            /* X11 */
            if (player->x11)
              x11_unmap (player);
          }
          /* when the stream is ended with stop action */
          else if (mplayer->status == MPLAYER_IS_IDLE) {
            pthread_mutex_unlock (&mplayer->mutex_status);
            /* ok, now we can continue */
            sem_post (&mplayer->sem);
          }
          else
            pthread_mutex_unlock (&mplayer->mutex_status);
        }
        /* when the stream is successfully started with action "play" */
        else if (strstr (buffer, "Starting playback")) {
            pthread_mutex_lock (&mplayer->mutex_status);
            mplayer->status = MPLAYER_IS_PLAYING;
            pthread_mutex_unlock (&mplayer->mutex_status);
        }
        /* Same as "Command loadfile", it is detected for continue
         * but here only with the action "play".
         */
        else if (strstr (buffer, "Command loadlist"))
          sem_post (&mplayer->sem);
        else if (strstr (buffer, "Exiting")) {
          pthread_mutex_lock (&mplayer->mutex_status);
          mplayer->status = MPLAYER_IS_DEAD;
          pthread_mutex_unlock (&mplayer->mutex_status);
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
  pthread_mutex_lock (&mplayer->mutex_search);
  mplayer->search = malloc (sizeof (mp_search_t));

  if (mplayer->search) {
    mplayer->search->property = strdup (str);
    mplayer->search->value = NULL;
    pthread_mutex_unlock (&mplayer->mutex_search);

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
    pthread_mutex_lock (&mplayer->mutex_search);
    free (mplayer->search);
    mplayer->search = NULL;
    pthread_mutex_unlock (&mplayer->mutex_search);
  }
  else
    pthread_mutex_unlock (&mplayer->mutex_search);

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

static void
slave_set_property (player_t *player, slave_property_t property,
                    slave_value_t value)
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
  case PROPERTY_SUB:
  case PROPERTY_SUB_VISIBILITY:
  case PROPERTY_VOLUME:
    send_to_slave (mplayer, "%s %i", cmd, value.i_val);
    break;

  case PROPERTY_SUB_DELAY:
    send_to_slave (mplayer, "%s %.2f", cmd, value.f_val);
    break;

  default:
    return;
  }
}

static inline void
slave_set_property_int (player_t *player, slave_property_t property, int value)
{
  slave_value_t param;

  param.i_val = value;
  slave_set_property (player, property, param);
}

static inline void
slave_set_property_float (player_t *player, slave_property_t property,
                          float value)
{
  slave_value_t param;

  param.f_val = value;
  slave_set_property (player, property, param);
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
slave_action (player_t *player, slave_cmd_t cmd, slave_value_t *value)
{
  mplayer_t *mplayer = NULL;

  if (!player)
    return;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer || !mplayer->fifo_in)
    return;

  switch (cmd) {
  case SLAVE_DVDNAV:
    if (value)
      send_to_slave (mplayer, "dvdnav %i", value->i_val);
    break;

  case SLAVE_LOADFILE:
    if (player->mrl->name && value)
      send_to_slave (mplayer, "loadfile \"%s\" %i",
                     player->mrl->name, value->i_val);

    send_to_slave (mplayer, "loadlist");
    /* wait that the thread will confirm "loadlist" */
    sem_wait (&mplayer->sem);
    break;

  case SLAVE_PAUSE:
    send_to_slave (mplayer, "pause");
    break;

  case SLAVE_QUIT:
    send_to_slave (mplayer, "quit");
    break;

  case SLAVE_SEEK:
    if (value)
      send_to_slave (mplayer, "seek %i 0", value->i_val);
    break;

  case SLAVE_STOP:
    slave_set_property_int (player, PROPERTY_LOOP, -1);
    send_to_slave (mplayer, "seek 100.00 1");

    /* wait that the thread will found the EOF */
    sem_wait (&mplayer->sem);
    break;

  case SLAVE_SUB_LOAD:
    if (player->mrl->subtitle) {
      slave_set_property_int (player, PROPERTY_SUB_VISIBILITY, 1);
      send_to_slave (mplayer, "sub_load \"%s\"", player->mrl->subtitle);
      slave_set_property_int (player, PROPERTY_SUB, 0);
    }
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
  slave_value_t param;

  param.i_val = value;
  slave_action (player, cmd, &param);
}

static int
mp_identify_metadata (mrl_t *mrl, const char *buffer)
{
  static int cnt;
  static slave_property_t property = PROPERTY_UNKNOWN;
  char *it;
  char str[256];
  mrl_metadata_t *meta;

  if (!mrl || !mrl->meta || !buffer || !strstr (buffer, "ID_CLIP_INFO"))
    return 0;

  /* no new metadata */
  if (strstr (buffer, "ID_CLIP_INFO_N=")) {
    cnt = 0;
    property = PROPERTY_UNKNOWN;
    return 0;
  }

  meta = mrl->meta;

  snprintf (str, sizeof (str), "NAME%i=", cnt);
  it = strstr (buffer, str);
  if (it) {
    if (!strcasecmp (parse_field (it, str), "title"))
      property = PROPERTY_METADATA_TITLE;
    else if (!strcasecmp (parse_field (it, str), "artist"))
      property = PROPERTY_METADATA_ARTIST;
    else if (!strcasecmp (parse_field (it, str), "genre"))
      property = PROPERTY_METADATA_GENRE;
    else if (!strcasecmp (parse_field (it, str), "album"))
      property = PROPERTY_METADATA_ALBUM;
    else if (!strcasecmp (parse_field (it, str), "year"))
      property = PROPERTY_METADATA_YEAR;
    else if (!strcasecmp (parse_field (it, str), "track"))
      property = PROPERTY_METADATA_TRACK;
    else
      property = PROPERTY_UNKNOWN;

    return 1;
  }

  snprintf (str, sizeof (str), "VALUE%i=", cnt);
  it = strstr (buffer, str);
  if (it) {
    switch (property) {
    case PROPERTY_METADATA_TITLE:
      if (meta->title)
        free (meta->title);
      meta->title = strdup (parse_field (it, str));
      break;

    case PROPERTY_METADATA_ARTIST:
      if (meta->artist)
        free (meta->artist);
      meta->artist = strdup (parse_field (it, str));
      break;

    case PROPERTY_METADATA_GENRE:
      if (meta->genre)
        free (meta->genre);
      meta->genre = strdup (parse_field (it, str));
      break;

    case PROPERTY_METADATA_ALBUM:
      if (meta->album)
        free (meta->album);
      meta->album = strdup (parse_field (it, str));
      break;

    case PROPERTY_METADATA_YEAR:
      if (meta->year)
        free (meta->year);
      meta->year = strdup (parse_field (it, str));
      break;

    case PROPERTY_METADATA_TRACK:
      if (meta->track)
        free (meta->track);
      meta->track = strdup (parse_field (it, str));
      break;

    default:
      break;
    }

    cnt++;
    property = PROPERTY_UNKNOWN;
    return 1;
  }

  return 0;
}

static int
mp_identify_audio (mrl_t *mrl, const char *buffer)
{
  char *it;
  mrl_properties_audio_t *audio;

  if (!mrl || !mrl->prop || !buffer || !strstr (buffer, "ID_AUDIO"))
  {
    return 0;
  }

  if (!mrl->prop->audio)
    mrl->prop->audio = mrl_properties_audio_new ();

  audio = mrl->prop->audio;

  it = strstr (buffer, "CODEC=");
  if (it) {
    if (audio->codec)
      free (audio->codec);
    audio->codec = strdup (parse_field (it, "CODEC="));
    return 1;
  }

  it = strstr (buffer, "BITRATE=");
  if (it) {
    audio->bitrate = atoi (parse_field (it, "BITRATE="));
    return 1;
  }

  it = strstr (buffer, "NCH=");
  if (it) {
    audio->channels = atoi (parse_field (it, "NCH="));
    return 1;
  }

  it = strstr (buffer, "RATE=");
  if (it) {
    audio->samplerate = atoi (parse_field (it, "RATE="));
    return 1;
  }

  return 0;
}

static int
mp_identify_video (mrl_t *mrl, const char *buffer)
{
  char *it;
  mrl_properties_video_t *video;

  if (!mrl || !mrl->prop || !buffer || !strstr (buffer, "ID_VIDEO"))
  {
    return 0;
  }

  if (!mrl->prop->video)
    mrl->prop->video = mrl_properties_video_new ();

  video = mrl->prop->video;

  it = strstr (buffer, "CODEC=");
  if (it) {
    if (video->codec)
      free (video->codec);
    video->codec = strdup (parse_field (it, "CODEC="));
    return 1;
  }

  it = strstr (buffer, "BITRATE=");
  if (it) {
    video->bitrate = atoi (parse_field (it, "BITRATE="));
    return 1;
  }

  it = strstr (buffer, "WIDTH=");
  if (it) {
    video->width = atoi (parse_field (it, "WIDTH="));
    return 1;
  }

  it = strstr (buffer, "HEIGHT=");
  if (it) {
    video->height = atoi (parse_field (it, "HEIGHT="));
    return 1;
  }

  /* Maybe this field is got more that one time,
   * but the last is the right value.
   */
  it = strstr (buffer, "ASPECT=");
  if (it) {
    video->aspect = (float) atof (parse_field (it, "ASPECT="));
    return 1;
  }

  it = strstr (buffer, "FPS=");
  if (it) {
    video->framerate = (float) atof (parse_field (it, "FPS="));
    return 1;
  }

  return 0;
}

static int
mp_identify_properties (mrl_t *mrl, const char *buffer)
{
  char *it;

  if (!mrl || !mrl->prop || !buffer)
    return 0;

  it = strstr (buffer, "ID_SEEKABLE=");
  if (it) {
    mrl->prop->seekable = atoi (parse_field (it, "ID_SEEKABLE="));
    return 1;
  }

  return 0;
}

/**
 * Identify a stream for complete player->w and player->h attributes. These
 * are necessary for that Xv can use a right aspect.
 */
static void
mp_identify (mrl_t *mrl, int flags)
{
  char *params[16];
  char buffer[SLAVE_CMD_BUFFER];
  int pp = 0;
  int mp_pipe[2];
  int found;
  FILE *mp_fifo;
  pid_t pid;

  if (!mrl || !mrl->name)
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
      params[pp++] = "-endpos";
      params[pp++] = "0";
      params[pp++] = strdup (mrl->name);
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
        found = 0;

        if (flags & IDENTIFY_VIDEO)
          found = mp_identify_video (mrl, buffer);

        if (!found && (flags & IDENTIFY_AUDIO))
          found = mp_identify_audio (mrl, buffer);

        if (!found && (flags & IDENTIFY_METADATA))
          found = mp_identify_metadata (mrl, buffer);

        if (!found && (flags & IDENTIFY_PROPERTIES))
          found = mp_identify_properties (mrl, buffer);

        /* stop fgets because MPlayer is ended */
        if (!found && strstr (buffer, "Exiting"))
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

static int
is_available (player_t *player, const char *bin)
{
  char *p, *fp, *env;
  char prog[256];

  env = getenv ("PATH");

  if (!env)
    return 0;

  fp = strdup (env);
  p = fp;

  if (!fp)
    return 0;

  for (p = strtok (p, ":"); p; p = strtok (NULL, ":")) {
    snprintf (prog, sizeof (prog), "%s/%s", p, bin);
    if (!access (prog, X_OK)) {
      free (fp);
      return 1;
    }
  }

  plog (player, PLAYER_MSG_ERROR,
        MODULE_NAME, "%s executable not found in the PATH", bin);

  free (fp);
  return 0;
}


/* Use only these commands for speak with MPlayer!
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * void  slave_cmd                (player_t, slave_cmd_t)
 * void  slave_cmd_int            (player_t, slave_cmd_t,      int)
 * int   slave_get_property_int   (player_t, slave_property_t)
 * char *slave_get_property_str   (player_t, slave_property_t)
 * void  slave_set_property_int   (player_t, slave_property_t, int)
 * void  slave_set_property_float (player_t, slave_property_t, float)
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
  pthread_attr_t attr;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "init");

  if (!player)
    return PLAYER_INIT_ERROR;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer)
    return PLAYER_INIT_ERROR;

  /* test if MPlayer is available */
  if (!is_available (player, "mplayer"))
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
        params[pp++] = "-v";                /* necessary for detect EOF */
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
          break;

        default:
          plog (player, PLAYER_MSG_WARNING,
                MODULE_NAME, "Unsupported video output type");
          break;
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
          break;

        default:
          plog (player, PLAYER_MSG_WARNING,
                MODULE_NAME, "Unsupported audio output type");
          break;
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

        plog (player, PLAYER_MSG_INFO, MODULE_NAME, "MPlayer child loaded");

        mplayer->status = MPLAYER_IS_IDLE;

        pthread_attr_init (&attr);
        pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_JOINABLE);

        /* create the thread */
        if (pthread_create (&mplayer->th_fifo, &attr,
                            thread_fifo, (void *) player) >= 0)
        {
          pthread_attr_destroy (&attr);
          return PLAYER_INIT_OK;
        }

        pthread_attr_destroy (&attr);
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

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "uninit");

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

    plog (player, PLAYER_MSG_WARNING, MODULE_NAME, "MPlayer child terminated");

    /* X11 */
    if (player->x11)
      x11_uninit (player);
  }

  pthread_mutex_destroy (&mplayer->mutex_search);
  pthread_mutex_destroy (&mplayer->mutex_status);
  sem_destroy (&mplayer->sem);

  free (mplayer);
}

static void
mplayer_mrl_get_properties (player_t *player, mrl_t *mrl)
{
  mrl_properties_video_t *video;
  mrl_properties_audio_t *audio;
  struct stat st;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "mrl_get_properties");

  if (!player || !mrl || !mrl->prop || !mrl->name)
    return;

  /* now fetch properties */
  stat (mrl->name, &st);
  mrl->prop->size = st.st_size;
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "File Size: %.2f MB",
        (float) mrl->prop->size / 1024 / 1024);

  mp_identify (mrl, IDENTIFY_AUDIO | IDENTIFY_VIDEO | IDENTIFY_PROPERTIES);

  audio = mrl->prop->audio;
  video = mrl->prop->video;

  plog (player, PLAYER_MSG_INFO,
        MODULE_NAME, "Seekable: %i", mrl->prop->seekable);

  if (video) {
    if (video->codec)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Video Codec: %s", video->codec);

    if (video->bitrate)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Video Bitrate: %i kbps", video->bitrate / 1000);

    if (video->width)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Video Width: %i", video->width);

    if (video->height)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Video Height: %i", video->height);

    if (video->aspect)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Video Aspect: %.2f", video->aspect);

    if (video->framerate)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Video Framerate: %.2f", video->framerate);
  }

  if (audio) {
    if (audio->codec)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Audio Codec: %s", audio->codec);

    if (audio->bitrate)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Audio Bitrate: %i kbps", audio->bitrate / 1000);

    if (audio->channels)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Audio Channels: %i", audio->channels);

    if (audio->samplerate)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Audio Sample Rate: %i Hz", audio->samplerate);
  }
}

static void
mplayer_mrl_get_metadata (player_t *player, mrl_t *mrl)
{
  mrl_metadata_t *meta;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "mrl_get_metadata");

  if (!player || !mrl || !mrl->meta)
    return;

  mp_identify (mrl, IDENTIFY_METADATA);

  meta = mrl->meta;

  if (meta->title)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Title: %s", meta->title);

  if (meta->artist)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Artist: %s", meta->artist);

  if (meta->genre)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Genre: %s", meta->genre);

  if (meta->album)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Album: %s", meta->album);

  if (meta->year)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Year: %s", meta->year);

  if (meta->track)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Track: %s", meta->track);
}

static playback_status_t
mplayer_playback_start (player_t *player)
{
  mplayer_t *mplayer = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_start");

  if (!player)
    return PLAYER_PB_FATAL;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer)
    return PLAYER_PB_FATAL;

  /* 0: new play, 1: append to the current playlist */
  slave_cmd_int (player, SLAVE_LOADFILE, 0);

  pthread_mutex_lock (&mplayer->mutex_status);
  if (mplayer->status != MPLAYER_IS_PLAYING) {
    pthread_mutex_unlock (&mplayer->mutex_status);
    return PLAYER_PB_ERROR;
  }
  pthread_mutex_unlock (&mplayer->mutex_status);

  /* load subtitle if exists */
  if (player->mrl->subtitle)
    slave_cmd (player, SLAVE_SUB_LOAD);

  /* X11 */
  if (player->x11 && !mrl_uses_vo (player->mrl))
    x11_map (player);

  return PLAYER_PB_OK;
}

static void
mplayer_playback_stop (player_t *player)
{
  mplayer_t *mplayer = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_stop");

  if (!player)
    return;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer)
    return;

  pthread_mutex_lock (&mplayer->mutex_status);
  if (mplayer->status != MPLAYER_IS_PLAYING) {
    pthread_mutex_unlock (&mplayer->mutex_status);
    return;
  }

  mplayer->status = MPLAYER_IS_IDLE;
  pthread_mutex_unlock (&mplayer->mutex_status);

  /* X11 */
  if (player->x11 && !mrl_uses_vo (player->mrl))
    x11_unmap (player);

  slave_cmd (player, SLAVE_STOP);
}

static playback_status_t
mplayer_playback_pause (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_pause");

  if (!player)
    return PLAYER_PB_FATAL;

  slave_cmd (player, SLAVE_PAUSE);

  return PLAYER_PB_OK;
}

static void
mplayer_playback_seek (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_seek: %d", value);

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

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_dvdnav: %s", log);

  if (!player)
    return;

  if (player->mrl->type == PLAYER_MRL_TYPE_DVD_NAV)
    slave_cmd_int (player, SLAVE_DVDNAV, action);
}

static int
mplayer_get_volume (player_t *player)
{
  int volume = 0;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "get_volume");

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
  player_mute_t mute = PLAYER_MUTE_UNKNOWN;
  char *buffer;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "get_mute");

  if (!player)
    return mute;

  buffer = slave_get_property_str (player, PROPERTY_MUTE);

  if (buffer) {
    if (!strcmp (buffer, "yes"))
      mute = PLAYER_MUTE_ON;
    else
      mute = PLAYER_MUTE_OFF;

    free (buffer);
  }

  return mute;
}

static void
mplayer_set_volume (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "set_volume: %d", value);

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

  plog (player, PLAYER_MSG_INFO,
        MODULE_NAME, "set_mute: %s", mute ? "on" : "off");

  if (!player)
    return;

  slave_set_property_int (player, PROPERTY_MUTE, mute);
}

static void
mplayer_set_sub_delay (player_t *player, float value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "set_sub_delay: %.2f", value);

  if (!player)
    return;

  slave_set_property_float (player, PROPERTY_SUB_DELAY, value);
}

/* public API */
player_funcs_t *
register_functions_mplayer (void)
{
  player_funcs_t *funcs = NULL;

  funcs = calloc (1, sizeof (player_funcs_t));
  funcs->init             = mplayer_init;
  funcs->uninit           = mplayer_uninit;
  funcs->set_verbosity    = NULL;
  funcs->mrl_get_props    = mplayer_mrl_get_properties;
  funcs->mrl_get_meta     = mplayer_mrl_get_metadata;
  funcs->pb_start         = mplayer_playback_start;
  funcs->pb_stop          = mplayer_playback_stop;
  funcs->pb_pause         = mplayer_playback_pause;
  funcs->pb_seek          = mplayer_playback_seek;
  funcs->pb_dvdnav        = mplayer_playback_dvdnav;
  funcs->get_volume       = mplayer_get_volume;
  funcs->get_mute         = mplayer_get_mute;
  funcs->set_volume       = mplayer_set_volume;
  funcs->set_mute         = mplayer_set_mute;
  funcs->set_sub_delay    = mplayer_set_sub_delay;

  return funcs;
}

void *
register_private_mplayer (void)
{
  mplayer_t *mplayer = NULL;

  mplayer = calloc (1, sizeof (mplayer_t));

  mplayer->status = MPLAYER_IS_DEAD;
  mplayer->fifo_in = NULL;
  mplayer->fifo_out = NULL;
  mplayer->search = NULL;

  /* init semaphore and mutex */
  sem_init (&mplayer->sem, 0, 0);
  pthread_mutex_init (&mplayer->mutex_search, NULL);
  pthread_mutex_init (&mplayer->mutex_status, NULL);

  return mplayer;
}
