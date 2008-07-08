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

#define FIFO_BUFFER      256
#define MPLAYER_NAME     "mplayer"

typedef enum {
  MPLAYER_DVDNAV_UP     = 1,
  MPLAYER_DVDNAV_DOWN   = 2,
  MPLAYER_DVDNAV_LEFT   = 3,
  MPLAYER_DVDNAV_RIGHT  = 4,
  MPLAYER_DVDNAV_MENU   = 5,
  MPLAYER_DVDNAV_SELECT = 6,
} mplayer_dvdnav_t;

typedef enum mplayer_sub_alignment {
  MPLAYER_SUB_ALIGNMENT_TOP    = 0,
  MPLAYER_SUB_ALIGNMENT_CENTER = 1,
  MPLAYER_SUB_ALIGNMENT_BOTTOM = 2,
} mplayer_sub_alignment_t;

/* Status of MPlayer child */
typedef enum mplayer_status {
  MPLAYER_IS_IDLE,
  MPLAYER_IS_PLAYING,
  MPLAYER_IS_DEAD
} mplayer_status_t;

typedef enum checklist {
  CHECKLIST_COMMANDS,
#if 0
  CHECKLIST_PROPERTIES,
#endif
} checklist_t;

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
  int verbosity;
  /* specific to thread */
  pthread_t th_fifo;      /* thread for the fifo_out parser */
  pthread_mutex_t mutex_search;
  pthread_mutex_t mutex_status;
  pthread_mutex_t mutex_verbosity;
  sem_t sem;
  mp_search_t *search;    /* use when a property is searched */
} mplayer_t;

/* union for set_property */
typedef union slave_value {
  int i_val;
  float f_val;
  char *s_val;
} slave_value_t;

typedef enum item_state {
  ITEM_DISABLE  = 0,
  ITEM_ENABLE   = (1 << 0),
  ITEM_HACK     = (1 << 1),
} item_state_t;

#define ALL_ITEM_STATES (ITEM_HACK | ITEM_ENABLE)

typedef struct item_list_s {
  const char *str;
  const int state_lib;    /* states of the command in libplayer */
  item_state_t state_mp;  /* state of the command in MPlayer */
} item_list_t;

/* slave commands */
typedef enum slave_cmd {
  SLAVE_UNKNOWN = 0,
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

static item_list_t g_slave_cmds[] = {
  [SLAVE_DVDNAV]        = {"dvdnav",       ITEM_ENABLE,           ITEM_DISABLE},
  [SLAVE_GET_PROPERTY]  = {"get_property", ITEM_ENABLE,           ITEM_DISABLE},
  [SLAVE_LOADFILE]      = {"loadfile",     ITEM_ENABLE,           ITEM_DISABLE},
  [SLAVE_PAUSE]         = {"pause",        ITEM_ENABLE,           ITEM_DISABLE},
  [SLAVE_QUIT]          = {"quit",         ITEM_ENABLE,           ITEM_DISABLE},
  [SLAVE_SEEK]          = {"seek",         ITEM_ENABLE,           ITEM_DISABLE},
  [SLAVE_SET_PROPERTY]  = {"set_property", ITEM_ENABLE,           ITEM_DISABLE},
  [SLAVE_STOP]          = {"stop",         ITEM_ENABLE|ITEM_HACK, ITEM_DISABLE},
  [SLAVE_SUB_LOAD]      = {"sub_load",     ITEM_ENABLE,           ITEM_DISABLE},
  [SLAVE_UNKNOWN]       = {NULL,           ITEM_DISABLE,          ITEM_DISABLE}
};

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
  PROPERTY_METADATA_COMMENT,
  PROPERTY_METADATA_GENRE,
  PROPERTY_METADATA_NAME,
  PROPERTY_METADATA_TITLE,
  PROPERTY_METADATA_TRACK,
  PROPERTY_METADATA_YEAR,
  PROPERTY_MUTE,
  PROPERTY_SAMPLERATE,
  PROPERTY_SUB,
  PROPERTY_SUB_ALIGNMENT,
  PROPERTY_SUB_DELAY,
  PROPERTY_SUB_VISIBILITY,
  PROPERTY_TIME_POS,
  PROPERTY_VIDEO_BITRATE,
  PROPERTY_VIDEO_CODEC,
  PROPERTY_VOLUME,
  PROPERTY_WIDTH
} slave_property_t;


static const char const *g_slave_props[] = {
  [PROPERTY_AUDIO_BITRATE]    = "audio_bitrate",
  [PROPERTY_AUDIO_CODEC]      = "audio_codec",
  [PROPERTY_CHANNELS]         = "channels",
  [PROPERTY_HEIGHT]           = "height",
  [PROPERTY_LOOP]             = "loop",
  [PROPERTY_METADATA_ALBUM]   = "metadata/album",
  [PROPERTY_METADATA_ARTIST]  = "metadata/artist",
  [PROPERTY_METADATA_COMMENT] = "metadata/comment",
  [PROPERTY_METADATA_GENRE]   = "metadata/genre",
  [PROPERTY_METADATA_NAME]    = "metadata/name",
  [PROPERTY_METADATA_TITLE]   = "metadata/title",
  [PROPERTY_METADATA_TRACK]   = "metadata/track",
  [PROPERTY_METADATA_YEAR]    = "metadata/year",
  [PROPERTY_MUTE]             = "mute",
  [PROPERTY_SAMPLERATE]       = "samplerate",
  [PROPERTY_SUB]              = "sub",
  [PROPERTY_SUB_ALIGNMENT]    = "sub_alignment",
  [PROPERTY_SUB_DELAY]        = "sub_delay",
  [PROPERTY_SUB_VISIBILITY]   = "sub_visibility",
  [PROPERTY_TIME_POS]         = "time_pos",
  [PROPERTY_VIDEO_BITRATE]    = "video_bitrate",
  [PROPERTY_VIDEO_CODEC]      = "video_codec",
  [PROPERTY_VOLUME]           = "volume",
  [PROPERTY_WIDTH]            = "width",
  [PROPERTY_UNKNOWN]          = NULL
};


static void
sig_handler (int signal)
{
  if (signal == SIGPIPE)
    fprintf (stderr, "SIGPIPE detected by the death of MPlayer");
}

static const char *
get_cmd (slave_cmd_t cmd, item_state_t *state)
{
  const int size = sizeof (g_slave_cmds) / sizeof (g_slave_cmds[0]);
  slave_cmd_t command = SLAVE_UNKNOWN;

  if (cmd < size && cmd >= 0)
    command = cmd;

  if (state)
  {
    int state_lib;
    item_state_t state_mp;

    *state = ITEM_DISABLE;
    state_lib = g_slave_cmds[command].state_lib & ALL_ITEM_STATES;
    state_mp = g_slave_cmds[command].state_mp;

    if ((state_lib == ITEM_HACK) ||
        (state_lib == ALL_ITEM_STATES && state_mp == ITEM_DISABLE))
    {
      *state = ITEM_HACK;
    }
    else if ((state_lib == ITEM_ENABLE && state_mp == ITEM_ENABLE) ||
             (state_lib == ALL_ITEM_STATES && state_mp == ITEM_ENABLE))
    {
      *state = ITEM_ENABLE;
    }
  }

  return g_slave_cmds[command].str;
}

/**
 * Get the text for a specific property in the g_slave_props. This function
 * should never return NULL. If that is the case, then a property is missed
 * in the table.
 */
static const char *
get_prop (slave_property_t property)
{
  const int size = sizeof (g_slave_props) / sizeof (g_slave_props[0]);
  slave_property_t prop = PROPERTY_UNKNOWN;

  if (property < size && property >= 0)
    prop = property;

  return g_slave_props[prop];
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
  char buffer[FIFO_BUFFER], log[FIFO_BUFFER];
  char *it;
  player_t *player;
  mplayer_t *mplayer;

  player = (player_t *) arg;

  if (!player)
    pthread_exit (0);

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer || !mplayer->fifo_out)
    pthread_exit (0);

  /* MPlayer's stdout parser */
  while (fgets (buffer, FIFO_BUFFER, mplayer->fifo_out))
  {
    pthread_mutex_lock (&mplayer->mutex_verbosity);
    if (mplayer->verbosity)
    {
      pthread_mutex_unlock (&mplayer->mutex_verbosity);

      strcpy (log, buffer);
      *(log + strlen (log) - 1) = '\0';
      plog (player, PLAYER_MSG_INFO, MODULE_NAME, "[process] %s", log);
    }
    else
      pthread_mutex_unlock (&mplayer->mutex_verbosity);

    /*
     * Here, the result of a property requested by the slave command
     * 'get_property', is searched and saved.
     */
    pthread_mutex_lock (&mplayer->mutex_search);
    if (mplayer->search && mplayer->search->property &&
        (it = strstr (buffer, mplayer->search->property)) == buffer)
    {
      it = parse_field (it, mplayer->search->property);

      if ((mplayer->search->value = malloc (strlen (it) + 1)))
      {
        memcpy (mplayer->search->value, it, strlen (it));
        *(mplayer->search->value + strlen (it)) = '\0';
      }
    }

    /*
     * HACK: This part will be used only to detect the end of the slave
     *       command 'get_property'. In this case, libplayer is locked on
     *       mplayer->sem as long as the property will be found or not.
     *
     * NOTE: This hack is no longer necessary since MPlayer r26296.
     */
    else if (strstr (buffer, "Command loadfile") == buffer)
    {
      if (mplayer->search)
      {
        free (mplayer->search->property);
        mplayer->search->property = NULL;
        sem_post (&mplayer->sem);
      }
    }
    pthread_mutex_unlock (&mplayer->mutex_search);

    /*
     * Search for "End Of File" in order to handle slave command 'stop'
     * or "end of stream".
     */
    if (strstr (buffer, "EOF code:") == buffer)
    {
      /*
       * When code is '4', then slave command 'stop' was used. But if the
       * command is not ENABLE, then this EOF code will be considered as an
       * "end of stream".
       */
      if (strchr (buffer, '4'))
      {
        item_state_t state;
        get_cmd (SLAVE_STOP, &state);

        if (state == ITEM_ENABLE)
        {
          pthread_mutex_lock (&mplayer->mutex_status);
          if (mplayer->status == MPLAYER_IS_IDLE)
          {
            pthread_mutex_unlock (&mplayer->mutex_status);
            sem_post (&mplayer->sem);
          }
          else
            pthread_mutex_unlock (&mplayer->mutex_status);

          continue;
        }
      }

      /*
       * Here we can consider an "end of stream" and sending an event.
       */
      pthread_mutex_lock (&mplayer->mutex_status);
      if (mplayer->status == MPLAYER_IS_PLAYING)
      {
        mplayer->status = MPLAYER_IS_IDLE;
        pthread_mutex_unlock (&mplayer->mutex_status);

        plog (player, PLAYER_MSG_INFO,
              MODULE_NAME, "Playback of stream has ended");

        if (player->event_cb)
          player->event_cb (PLAYER_EVENT_PLAYBACK_FINISHED, NULL);

        if (player->x11)
          x11_unmap (player);
      }
      else
        pthread_mutex_unlock (&mplayer->mutex_status);
    }

    /*
     * HACK: If the slave command 'stop' is not handled by MPlayer, then this
     *       part will find the end instead of "EOF code: 4".
     */
    else if (strstr (buffer, "File not found: ''") == buffer)
    {
      item_state_t state;
      get_cmd (SLAVE_STOP, &state);

      if (state != ITEM_HACK)
        continue;

      pthread_mutex_lock (&mplayer->mutex_status);
      if (mplayer->status == MPLAYER_IS_IDLE)
      {
        pthread_mutex_unlock (&mplayer->mutex_status);
        sem_post (&mplayer->sem);
      }
      else
        pthread_mutex_unlock (&mplayer->mutex_status);
    }

    /*
     * Detect when MPlayer playback is really started in order to change
     * the current status.
     */
    else if (strstr (buffer, "Starting playback") == buffer ||  /* for local */
             strstr (buffer, "Connecting to server") == buffer) /* for network */
    {
      pthread_mutex_lock (&mplayer->mutex_status);
      mplayer->status = MPLAYER_IS_PLAYING;
      pthread_mutex_unlock (&mplayer->mutex_status);
    }

    /*
     * HACK: loadlist is never used in libplayer to load a playlist. This
     *       will be used only to detect the end of the previous command.
     *       In this case, when the slave command 'loadfile' is used to play
     *       a stream, libplayer is locked on mplayer->sem as long as the
     *       loading is not finished.
     */
    else if (strstr (buffer, "Command loadlist") == buffer)
      sem_post (&mplayer->sem);
  }

  pthread_mutex_lock (&mplayer->mutex_status);
  mplayer->status = MPLAYER_IS_DEAD;
  pthread_mutex_unlock (&mplayer->mutex_status);

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
  const char *prop;
  const char *command;
  item_state_t state_cmd;

  if (!player)
    return;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer || !mplayer->fifo_in)
    return;

  prop = get_prop (property);
  command = get_cmd (SLAVE_GET_PROPERTY, &state_cmd);
  if (prop && command && state_cmd == ITEM_ENABLE)
    send_to_slave (mplayer, "%s %s", command, prop);
}

/**
 * Get results of commands sent to MPlayer. The MPlayer's stdout and sterr
 * are connected to fifo_out. This function uses the thread_fifo for get
 * the result. That uses semaphore and mutex.
 */
static char *
slave_result (slave_property_t property, player_t *player)
{
  char str[FIFO_BUFFER];
  const char *prop;
  char *ret = NULL;
  mplayer_t *mplayer = NULL;

  if (!player)
    return NULL;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer || !mplayer->fifo_in || !mplayer->fifo_out)
    return NULL;

  prop = get_prop (property);
  if (prop)
    sprintf (str, "ANS_%s=", prop);
  else
    return NULL;

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
slave_get_property_int (player_t *player, slave_property_t property)
{
  int value = -1;
  char *result;

  result = slave_result (property, player);

  if (result) {
    value = (int) rintf (atof (result));
    free (result);
  }

  return value;
}

static float
slave_get_property_float (player_t *player, slave_property_t property)
{
  float value = -1.0;
  char *result;

  result = slave_result (property, player);

  if (result) {
    value = atof (result);
    free (result);
  }

  return value;
}

static inline char *
slave_get_property_str (player_t *player, slave_property_t property)
{
  return slave_result (property, player);
}

static void
slave_set_property (player_t *player, slave_property_t property,
                    slave_value_t value)
{
  mplayer_t *mplayer = NULL;
  const char *prop;
  const char *command;
  item_state_t state_cmd;
  char cmd[FIFO_BUFFER];

  if (!player)
    return;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer || !mplayer->fifo_in)
    return;

  prop = get_prop (property);
  command = get_cmd (SLAVE_SET_PROPERTY, &state_cmd);
  if (prop && command && state_cmd == ITEM_ENABLE)
    sprintf (cmd, "%s %s", command, prop);
  else
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

static inline void
slave_set_property_flag (player_t *player, slave_property_t property,
                         int value)
{
  slave_value_t param;

  param.i_val = value ? 1 : 0;
  slave_set_property (player, property, param);
}

static void
slave_action (player_t *player, slave_cmd_t cmd, slave_value_t *value, int opt)
{
  mplayer_t *mplayer = NULL;
  const char *command;
  item_state_t state_cmd;

  if (!player)
    return;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer || !mplayer->fifo_in)
    return;

  command = get_cmd (cmd, &state_cmd);
  if (!command || state_cmd == ITEM_DISABLE)
    return;

  switch (cmd) {
  case SLAVE_DVDNAV:
    if (state_cmd == ITEM_ENABLE && value)
      send_to_slave (mplayer, "%s %i", command, value->i_val);
    break;

  case SLAVE_LOADFILE:
    if (state_cmd == ITEM_ENABLE && value && value->s_val)
      send_to_slave (mplayer, "%s \"%s\" %i", command, value->s_val, opt);

    send_to_slave (mplayer, "loadlist");
    /* wait that the thread will confirm "loadlist" */
    sem_wait (&mplayer->sem);
    break;

  case SLAVE_PAUSE:
    if (state_cmd == ITEM_ENABLE)
      send_to_slave (mplayer, command);
    break;

  case SLAVE_QUIT:
    if (state_cmd == ITEM_ENABLE)
      send_to_slave (mplayer, command);
    break;

  case SLAVE_SEEK:
    if (state_cmd == ITEM_ENABLE && value)
      send_to_slave (mplayer, "%s %i %i", command, value->i_val, opt);
    break;

  case SLAVE_STOP:
    if (state_cmd == ITEM_HACK)
    {
      plog (player, PLAYER_MSG_WARNING,
            MODULE_NAME, "[hack] slave command '%s'", command);
      send_to_slave (mplayer, "loadfile \"\"");
    }
    else if (state_cmd == ITEM_ENABLE)
      send_to_slave (mplayer, command);

    /* wait that the thread will found the EOF */
    sem_wait (&mplayer->sem);
    break;

  case SLAVE_SUB_LOAD:
    if (state_cmd == ITEM_ENABLE && value && value->s_val)
      send_to_slave (mplayer, "%s \"%s\"", command, value->s_val);
    break;

  default:
    return;
  }
}

static inline void
slave_cmd (player_t *player, slave_cmd_t cmd)
{
  slave_action (player, cmd, NULL, 0);
}

static inline void
slave_cmd_int (player_t *player, slave_cmd_t cmd, int value)
{
  slave_value_t param;

  param.i_val = value;
  slave_action (player, cmd, &param, 0);
}

static inline void
slave_cmd_int_opt (player_t *player, slave_cmd_t cmd, int value, int opt)
{
  slave_value_t param;

  param.i_val = value;
  slave_action (player, cmd, &param, opt);
}

static inline void
slave_cmd_str (player_t *player, slave_cmd_t cmd, char *str)
{
  slave_value_t param;

  param.s_val = str;
  slave_action (player, cmd, &param, 0);
}

static inline void
slave_cmd_str_opt (player_t *player, slave_cmd_t cmd, char *str, int opt)
{
  slave_value_t param;

  param.s_val = str;
  slave_action (player, cmd, &param, opt);
}

static int
count_nb_dec (int dec)
{
  int size = 1;

  while (dec /= 10)
    size++;

  return size;
}

static char *
mp_resource_get_uri (mrl_t *mrl)
{
  static const char const *protocols[] = {
    /* Local Streams */
    [MRL_RESOURCE_FILE]     = "file://",

    /* Audio CD */
    [MRL_RESOURCE_CDDA]     = "cdda://",
    [MRL_RESOURCE_CDDB]     = "cddb://",

    /* Video discs */
    [MRL_RESOURCE_DVD]      = "dvd://",
    [MRL_RESOURCE_DVDNAV]   = "dvdnav://",

    /* Network Streams */
    [MRL_RESOURCE_FTP]      = "ftp://",
    [MRL_RESOURCE_HTTP]     = "http://",
    [MRL_RESOURCE_MMS]      = "mms://",
    [MRL_RESOURCE_RTP]      = "rtp://",
    [MRL_RESOURCE_RTSP]     = "rtsp://",
    [MRL_RESOURCE_SMB]      = "smb://",
    [MRL_RESOURCE_UDP]      = "udp://",
    [MRL_RESOURCE_UNSV]     = "unsv://",

    [MRL_RESOURCE_UNKNOWN]  = NULL
  };

  if (!mrl)
    return NULL;

  switch (mrl->resource)
  {
  case MRL_RESOURCE_FILE:
  {
    mrl_resource_local_args_t *args = mrl->priv;
    if (args && args->location)
      return strdup (args->location);
    break;
  }

  case MRL_RESOURCE_CDDA: /* cdda://track_start-track_end:speed/device */
  case MRL_RESOURCE_CDDB: /* cddb://track_start-track_end:speed/device */
  {
    char *uri;
    char *device = NULL;
    const char *protocol = protocols[mrl->resource];
    char track_start[4] = "";
    char track_end[8] = "";
    char speed[8] = "";
    size_t size = strlen (protocol);
    mrl_resource_cd_args_t *args;

    args = mrl->priv;
    if (!args)
      break;

    if (args->track_start)
    {
      size += count_nb_dec (args->track_start);
      snprintf (track_start, sizeof (track_start), "%i", args->track_start);
    }
    if (args->track_end > args->track_start)
    {
      size += 1 + count_nb_dec (args->track_end);
      snprintf (track_end, sizeof (track_end), "-%i", args->track_end);
    }
    if (args->speed)
    {
      size += 1 + count_nb_dec (args->speed);
      snprintf (speed, sizeof (speed), ":%i", args->speed);
    }
    if (args->device)
    {
      size_t length = 1 + strlen (args->device);
      size += length;
      device = malloc (1 + length);
      if (device)
        snprintf (device, 1 + length, "/%s", args->device);
    }

    size++;
    uri = malloc (size);
    if (!uri)
    {
      if (device)
        free (device);
      break;
    }

    snprintf (uri, size, "%s%s%s%s%s",
              protocol, track_start, track_end, speed, device ? device : "");

    if (device)
      free (device);

    return uri;
  }

  case MRL_RESOURCE_DVD:    /* dvd://title_start-title_end/device */
  case MRL_RESOURCE_DVDNAV: /* dvdnav://title_start-title_end/device */
  {
    char *uri;
    char *device = NULL;
    const char *protocol = protocols[mrl->resource];
    char title_start[4] = "";
    char title_end[8] = "";
    size_t size = strlen (protocol);
    mrl_resource_videodisc_args_t *args;

    args = mrl->priv;
    if (!args)
      break;

    if (args->title_start)
    {
      size += count_nb_dec (args->title_start);
      snprintf (title_start, sizeof (title_start), "%i", args->title_start);
    }
    if (args->title_end > args->title_start)
    {
      size += 1 + count_nb_dec (args->title_end);
      snprintf (title_end, sizeof (title_end), "-%i", args->title_end);
    }
    /*
     * NOTE: for dvd://, "/device" is handled by MPlayer >= r27226, and that
     *       is just ignored with older.
     */
    if (args->device)
    {
      size_t length = 1 + strlen (args->device);
      size += length;
      device = malloc (1 + length);
      if (device)
        snprintf (device, 1 + length, "/%s", args->device);
    }

    size++;
    uri = malloc (size);
    if (!uri)
    {
      if (device)
        free (device);
      break;
    }

    snprintf (uri, size, "%s%s%s%s",
              protocol, title_start, title_end, device ? device : "");

    if (device)
      free (device);

    return uri;
  }

  case MRL_RESOURCE_FTP:  /* ftp://username:password@url   */
  case MRL_RESOURCE_HTTP: /* http://username:password@url  */
  case MRL_RESOURCE_MMS:  /* mms://username:password@url   */
  case MRL_RESOURCE_RTP:  /* rtp://username:password@url   */
  case MRL_RESOURCE_RTSP: /* rtsp://username:password@url  */
  case MRL_RESOURCE_SMB:  /* smb://username:password@url   */
  case MRL_RESOURCE_UDP:  /* udp://username:password@url   */
  case MRL_RESOURCE_UNSV: /* unsv://username:password@url  */
  {
    char *uri;
    const char *protocol = protocols[mrl->resource];
    char at[256] = "";
    size_t size = strlen (protocol);
    mrl_resource_network_args_t *args;

    args = mrl->priv;
    if (!args || !args->url)
      break;

    if (args->username)
    {
      size += 1 + strlen (args->username);
      if (args->password)
      {
        size += 1 + strlen (args->password);
        snprintf (at, sizeof (at), "%s:%s@", args->username, args->password);
      }
      else
        snprintf (at, sizeof (at), "%s@", args->username);
    }
    size += strlen (args->url);

    size++;
    uri = malloc (size);
    if (!uri)
      break;

    snprintf (uri, size, "%s%s%s", protocol, at, args->url);
    return uri;
  }

  default:
    break;
  }

  return NULL;
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
  if (strstr (buffer, "ID_CLIP_INFO_N=") == buffer) {
    cnt = 0;
    property = PROPERTY_UNKNOWN;
    return 0;
  }

  meta = mrl->meta;

  snprintf (str, sizeof (str), "ID_CLIP_INFO_NAME%i=", cnt);
  it = strstr (buffer, str);
  if (it == buffer) {
    if (!strcasecmp (parse_field (it, str), "title"))
      property = PROPERTY_METADATA_TITLE;
    else if (!strcasecmp (parse_field (it, str), "name"))
      property = PROPERTY_METADATA_NAME;
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
    else if (!strcasecmp (parse_field (it, str), "comment"))
      property = PROPERTY_METADATA_COMMENT;
    else
      property = PROPERTY_UNKNOWN;

    return 1;
  }

  snprintf (str, sizeof (str), "ID_CLIP_INFO_VALUE%i=", cnt);
  it = strstr (buffer, str);
  if (it == buffer) {
    switch (property) {
    case PROPERTY_METADATA_NAME:
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

    case PROPERTY_METADATA_COMMENT:
      if (meta->comment)
        free (meta->comment);
      meta->comment = strdup (parse_field (it, str));
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
    return 0;

  if (!mrl->prop->audio)
    mrl->prop->audio = mrl_properties_audio_new ();

  audio = mrl->prop->audio;

  it = strstr (buffer, "ID_AUDIO_CODEC=");
  if (it == buffer) {
    if (audio->codec)
      free (audio->codec);
    audio->codec = strdup (parse_field (it, "ID_AUDIO_CODEC="));
    return 1;
  }

  it = strstr (buffer, "ID_AUDIO_BITRATE=");
  if (it == buffer) {
    audio->bitrate = atoi (parse_field (it, "ID_AUDIO_BITRATE="));
    return 1;
  }

  it = strstr (buffer, "ID_AUDIO_NCH=");
  if (it == buffer) {
    audio->channels = atoi (parse_field (it, "ID_AUDIO_NCH="));
    return 1;
  }

  it = strstr (buffer, "ID_AUDIO_RATE=");
  if (it == buffer) {
    audio->samplerate = atoi (parse_field (it, "ID_AUDIO_RATE="));
    return 1;
  }

  return 0;
}

static int
mp_identify_video (mrl_t *mrl, const char *buffer)
{
  char *it;
  float val;
  mrl_properties_video_t *video;

  if (!mrl || !mrl->prop || !buffer || !strstr (buffer, "ID_VIDEO"))
    return 0;

  if (!mrl->prop->video)
    mrl->prop->video = mrl_properties_video_new ();

  video = mrl->prop->video;

  it = strstr (buffer, "ID_VIDEO_CODEC=");
  if (it == buffer) {
    if (video->codec)
      free (video->codec);
    video->codec = strdup (parse_field (it, "ID_VIDEO_CODEC="));
    return 1;
  }

  it = strstr (buffer, "ID_VIDEO_BITRATE=");
  if (it == buffer) {
    video->bitrate = atoi (parse_field (it, "ID_VIDEO_BITRATE="));
    return 1;
  }

  it = strstr (buffer, "ID_VIDEO_WIDTH=");
  if (it == buffer) {
    video->width = atoi (parse_field (it, "ID_VIDEO_WIDTH="));
    return 1;
  }

  it = strstr (buffer, "ID_VIDEO_HEIGHT=");
  if (it == buffer) {
    video->height = atoi (parse_field (it, "ID_VIDEO_HEIGHT="));
    return 1;
  }

  /* Maybe this field is got more that one time,
   * but the last is the right value.
   */
  it = strstr (buffer, "ID_VIDEO_ASPECT=");
  if (it == buffer) {
    video->aspect =
      (uint32_t) (atof (parse_field (it, "ID_VIDEO_ASPECT=")) * 10000.0);
    return 1;
  }

  it = strstr (buffer, "ID_VIDEO_FPS=");
  if (it == buffer) {
    val = atof (parse_field (it, "ID_VIDEO_FPS="));
    video->frameduration = (uint32_t) (val ? 90000.0 / val : 0);
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

  it = strstr (buffer, "ID_LENGTH=");
  if (it == buffer) {
    mrl->prop->length =
      (uint32_t) (atof (parse_field (it, "ID_LENGTH=")) * 1000.0);
    return 1;
  }

  it = strstr (buffer, "ID_SEEKABLE=");
  if (it == buffer) {
    mrl->prop->seekable = atoi (parse_field (it, "ID_SEEKABLE="));
    return 1;
  }

  return 0;
}

static void
mp_identify (mrl_t *mrl, int flags)
{
  char buffer[FIFO_BUFFER];
  int mp_pipe[2];
  int found;
  FILE *mp_fifo;
  pid_t pid;
  char *uri = NULL;

  if (!mrl)
    return;

  uri = mp_resource_get_uri (mrl);
  if (!uri)
    return;

  if (pipe (mp_pipe)) {
    free (uri);
    return;
  }

  pid = fork ();

  switch (pid) {
  /* the son (a new hope) */
  case 0:
  {
    char *params[16];
    int pp = 0;

    close (mp_pipe[0]);

    dup2 (mp_pipe[1], STDERR_FILENO);
    dup2 (mp_pipe[1], STDOUT_FILENO);

    params[pp++] = MPLAYER_NAME;
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
    params[pp++] = uri;
    params[pp++] = "-identify";
    params[pp] = NULL;

    execvp (MPLAYER_NAME, params);
  }

  case -1:
    break;

  /* I'm your father */
  default:
    free (uri);
    close (mp_pipe[1]);

    mp_fifo = fdopen (mp_pipe[0], "r");

    while (fgets (buffer, FIFO_BUFFER, mp_fifo)) {
      found = 0;

      if (flags & IDENTIFY_VIDEO)
        found = mp_identify_video (mrl, buffer);

      if (!found && (flags & IDENTIFY_AUDIO))
        found = mp_identify_audio (mrl, buffer);

      if (!found && (flags & IDENTIFY_METADATA))
        found = mp_identify_metadata (mrl, buffer);

      if (!found && (flags & IDENTIFY_PROPERTIES))
        found = mp_identify_properties (mrl, buffer);
    }

    /* wait the death of MPlayer */
    waitpid (pid, NULL, 0);

    close (mp_pipe[0]);
    fclose (mp_fifo);
  }
}

static int
mp_check_compatibility (player_t *player, checklist_t check)
{
  int i, nb = 0, res = 1;
  int mp_pipe[2];
  pid_t pid;
  item_list_t *list;
  const char *str, *what;
  const int *state_lib;
  item_state_t *state_mp;

  if (!player)
    return 0;

  if (pipe (mp_pipe))
    return 0;

  switch (check)
  {
  case CHECKLIST_COMMANDS:
    nb = sizeof (g_slave_cmds) / sizeof (g_slave_cmds[0]);
    list = g_slave_cmds;
    what = "slave command";
    break;
#if 0
  case CHECKLIST_PROPERTIES:
    nb = sizeof (g_slave_props) / sizeof (g_slave_props[0]);
    list = g_slave_props;
    what = "slave property";
    break;
#endif
  default:
    break;
  }

  pid = fork ();

  switch (pid)
  {
  /* the son (a new hope) */
  case 0:
  {
    char *params[8];
    int pp = 0;

    close (mp_pipe[0]);
    dup2 (mp_pipe[1], STDOUT_FILENO);

    params[pp++] = MPLAYER_NAME;
    switch (check)
    {
    case CHECKLIST_COMMANDS:
      params[pp++] = "-input";
      params[pp++] = "cmdlist";
      break;
#if 0
    case CHECKLIST_PROPERTIES:
      params[pp++] = "-list-properties";
      break;
#endif
    default:
      break;
    }
    params[pp] = NULL;

    execvp (MPLAYER_NAME, params);
  }

  case -1:
    break;

  /* I'm your father */
  default:
  {
    FILE *mp_fifo;
    char buffer[FIFO_BUFFER];
    char *buf;

    close (mp_pipe[1]);
    mp_fifo = fdopen (mp_pipe[0], "r");

    while (fgets (buffer, FIFO_BUFFER, mp_fifo))
    {
      for (i = 1; i < nb; i++)
      {
        state_mp = &list[i].state_mp;
        str = list[i].str;

        if (!str || !state_mp || *state_mp != ITEM_DISABLE)
          continue;

        buf = strstr (buffer, str);
        if (!buf)
          continue;

        /* search the command|property */
        if ((buf == buffer || (buf > buffer && *(buf - 1) == ' '))
            && (*(buf + strlen (str)) == ' ' || *(buf + strlen (str)) == '\n'))
        {
          *state_mp = ITEM_ENABLE;
          break;
        }
      }
    }

    waitpid (pid, NULL, 0);
    close (mp_pipe[0]);
    fclose (mp_fifo);
  }
  }

  /* check items list */
  for (i = 1; i < nb; i++)
  {
    int state_libplayer;

    state_mp = &list[i].state_mp;
    state_lib = &list[i].state_lib;
    str = list[i].str;
    state_libplayer = *state_lib & ALL_ITEM_STATES;

    if (state_libplayer == ITEM_ENABLE && *state_mp == ITEM_DISABLE)
    {
      plog (player, PLAYER_MSG_ERROR, MODULE_NAME,
            "%s '%s' is needed and not supported by your version of MPlayer",
            what, str);
      res = 0;
    }
    else if (state_libplayer == ITEM_HACK && *state_mp == ITEM_DISABLE)
    {
      plog (player, PLAYER_MSG_WARNING, MODULE_NAME,
            "%s '%s' is needed and not supported by your version of MPlayer "
            "and libplayer, then a hack is used", what, str);
    }
    else if (state_libplayer == ALL_ITEM_STATES && *state_mp == ITEM_DISABLE)
    {
      plog (player, PLAYER_MSG_WARNING, MODULE_NAME,
            "%s '%s' is needed and not supported by your version of MPlayer, "
            "then a hack is used", what, str);
    }
    else if (state_libplayer == ITEM_HACK && *state_mp == ITEM_ENABLE)
    {
      plog (player, PLAYER_MSG_WARNING, MODULE_NAME,
            "%s '%s' is supported by your version of MPlayer but not by "
            "libplayer, then a hack is used", what, str);
    }
    else if ((state_libplayer == ITEM_ENABLE && *state_mp == ITEM_ENABLE) ||
             (state_libplayer == ALL_ITEM_STATES && *state_mp == ITEM_ENABLE))
    {
      plog (player, PLAYER_MSG_INFO, MODULE_NAME,
            "%s '%s' is supported by your version of MPlayer", what, str);
    }
  }

  return res;
}

static int
executable_is_available (player_t *player, const char *bin)
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
 * void  slave_cmd_int_opt        (player_t, slave_cmd_t,      int,   int)
 * void  slave_cmd_str            (player_t, slave_cmd_t,      char*)
 * void  slave_cmd_str_opt        (player_t, slave_cmd_t,      char*, int)
 * int   slave_get_property_int   (player_t, slave_property_t)
 * float slave_get_property_float (player_t, slave_property_t)
 * char *slave_get_property_str   (player_t, slave_property_t)
 * void  slave_set_property_int   (player_t, slave_property_t, int)
 * void  slave_set_property_float (player_t, slave_property_t, float)
 * void  slave_set_property_flag  (player_t, slave_property_t, int)
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
  char winid[32];
  pthread_attr_t attr;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "init");

  if (!player)
    return PLAYER_INIT_ERROR;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer)
    return PLAYER_INIT_ERROR;

  /* test if MPlayer is available */
  if (!executable_is_available (player, MPLAYER_NAME))
    return PLAYER_INIT_ERROR;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "check MPlayer compatibility");

  if (!mp_check_compatibility (player, CHECKLIST_COMMANDS))
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
  case PLAYER_VO_XV:
    if (!x11_init (player))
      return PLAYER_INIT_ERROR;
    sprintf (winid, "%lu", (unsigned long) x11_get_window (player->x11));
  default:
    break;
  }

  if (pipe (mplayer->pipe_in))
    return PLAYER_INIT_ERROR;

  if (pipe (mplayer->pipe_out)) {
    close (mplayer->pipe_in[0]);
    close (mplayer->pipe_in[1]);
    return PLAYER_INIT_ERROR;
  }

  mplayer->pid = fork ();

  switch (mplayer->pid) {
  /* the son (a new hope) */
  case 0:
  {
    char *params[32];
    int pp = 0;

    close (mplayer->pipe_in[1]);
    close (mplayer->pipe_out[0]);

    /* connect the pipe to MPlayer's stdin */
    dup2 (mplayer->pipe_in[0], STDIN_FILENO);
    /* connect stdout and stderr to the second pipe */
    dup2 (mplayer->pipe_out[1], STDERR_FILENO);
    dup2 (mplayer->pipe_out[1], STDOUT_FILENO);

    /* default MPlayer arguments */
    params[pp++] = MPLAYER_NAME;
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
      params[pp++] = winid;
      break;

    /* with xv and wid, zoom, fs and aspect have no effect.
     * The image is always scaled on all the window.
     */
    case PLAYER_VO_XV:
      params[pp++] = "xv";
      /* window ID */
      params[pp++] = "-wid";
      params[pp++] = winid;
      break;

    case PLAYER_VO_FB:
      params[pp++] = "fbdev";
      break;

    default:
      params[pp++] = "null";
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
      params[pp++] = "null";
      break;
    }

    params[pp] = NULL;

    execvp (MPLAYER_NAME, params);
  }

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

    if (pthread_create (&mplayer->th_fifo, &attr,
                        thread_fifo, (void *) player) >= 0)
    {
      pthread_attr_destroy (&attr);
      return PLAYER_INIT_OK;
    }

    pthread_attr_destroy (&attr);
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

    close (mplayer->pipe_in[1]);
    close (mplayer->pipe_out[0]);

    fclose (mplayer->fifo_in);
    fclose (mplayer->fifo_out);

    plog (player, PLAYER_MSG_INFO, MODULE_NAME, "MPlayer child terminated");

    if (player->x11)
      x11_uninit (player);
  }

  pthread_mutex_destroy (&mplayer->mutex_search);
  pthread_mutex_destroy (&mplayer->mutex_status);
  pthread_mutex_destroy (&mplayer->mutex_verbosity);
  sem_destroy (&mplayer->sem);

  free (mplayer);
}

static void
mplayer_set_verbosity (player_t *player, player_verbosity_level_t level)
{
  mplayer_t *mplayer;
  int verbosity = -1;

  if (!player)
    return;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer)
    return;

  switch (level) {
  case PLAYER_MSG_NONE:
  case PLAYER_MSG_INFO:
  case PLAYER_MSG_WARNING:
    verbosity = 1;
    break;
  case PLAYER_MSG_ERROR:
  case PLAYER_MSG_CRITICAL:
    verbosity = 0;
    break;
  default:
    break;
  }

  if (verbosity != -1) {
    pthread_mutex_lock (&mplayer->mutex_verbosity);
    mplayer->verbosity = 1;
    pthread_mutex_unlock (&mplayer->mutex_verbosity);
  }
}

static int
mplayer_mrl_supported_res (player_t *player, mrl_resource_t res)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "mrl_supported_res");

  if (!player)
    return 0;

  switch (res)
  {
  case MRL_RESOURCE_FILE:
  case MRL_RESOURCE_CDDA:
  case MRL_RESOURCE_CDDB:
  case MRL_RESOURCE_DVD:
  case MRL_RESOURCE_DVDNAV:
  case MRL_RESOURCE_FTP:
  case MRL_RESOURCE_HTTP:
  case MRL_RESOURCE_MMS:
  case MRL_RESOURCE_RTP:
  case MRL_RESOURCE_RTSP:
  case MRL_RESOURCE_SMB:
  case MRL_RESOURCE_UDP:
  case MRL_RESOURCE_UNSV:
    return 1;

  default:
    return 0;
  }
}

static void
mplayer_mrl_retrieve_properties (player_t *player, mrl_t *mrl)
{
  mrl_properties_video_t *video;
  mrl_properties_audio_t *audio;
  struct stat st;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "mrl_retrieve_properties");

  if (!player || !mrl || !mrl->prop)
    return;

  /* now fetch properties */
  if (mrl->resource == MRL_RESOURCE_FILE)
  {
    mrl_resource_local_args_t *args = mrl->priv;
    if (args && args->location)
    {
      stat (args->location, &st);
      mrl->prop->size = st.st_size;
      plog (player, PLAYER_MSG_INFO, MODULE_NAME, "File Size: %.2f MB",
            (float) mrl->prop->size / 1024 / 1024);
    }
  }

  mp_identify (mrl, IDENTIFY_AUDIO | IDENTIFY_VIDEO | IDENTIFY_PROPERTIES);

  audio = mrl->prop->audio;
  video = mrl->prop->video;

  plog (player, PLAYER_MSG_INFO,
        MODULE_NAME, "Seekable: %i", mrl->prop->seekable);

  plog (player, PLAYER_MSG_INFO,
        MODULE_NAME, "Length: %i ms", mrl->prop->length);

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
            MODULE_NAME, "Video Aspect: %i", video->aspect);

    if (video->frameduration)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Video Framerate: %i", video->frameduration);
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
mplayer_mrl_retrieve_metadata (player_t *player, mrl_t *mrl)
{
  mrl_metadata_t *meta;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "mrl_retrieve_metadata");

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

  if (meta->comment)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Comment: %s", meta->comment);
}

static playback_status_t
mplayer_playback_start (player_t *player)
{
  mplayer_t *mplayer = NULL;
  char *uri = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_start");

  if (!player)
    return PLAYER_PB_FATAL;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer)
    return PLAYER_PB_FATAL;

  if (!player->mrl)
    return PLAYER_PB_ERROR;

  uri = mp_resource_get_uri (player->mrl);
  if (!uri)
    return PLAYER_PB_ERROR;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "uri: %s", uri);

  /* 0: new play, 1: append to the current playlist */
  slave_cmd_str_opt (player, SLAVE_LOADFILE, uri, 0);

  free (uri);

  pthread_mutex_lock (&mplayer->mutex_status);
  if (mplayer->status != MPLAYER_IS_PLAYING) {
    pthread_mutex_unlock (&mplayer->mutex_status);
    return PLAYER_PB_ERROR;
  }
  pthread_mutex_unlock (&mplayer->mutex_status);

  /* load subtitle if exists */
  if (player->mrl->subs) {
    char **sub = player->mrl->subs;
    slave_set_property_flag (player, PROPERTY_SUB_VISIBILITY, 1);
    while (*sub)
    {
      slave_cmd_str (player, SLAVE_SUB_LOAD, *sub);
      (*sub)++;
    }
    slave_set_property_int (player, PROPERTY_SUB, 0);
  }

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
mplayer_playback_seek (player_t *player, int value, player_pb_seek_t seek)
{
  int opt;

  plog (player, PLAYER_MSG_INFO,
        MODULE_NAME, "playback_seek: %d %d", value, seek);

  if (!player)
    return;

  switch (seek) {
  default:
  case PLAYER_PB_SEEK_RELATIVE:
    opt = 0;
    break;
  case PLAYER_PB_SEEK_PERCENT:
    opt = 1;
    break;
  case PLAYER_PB_SEEK_ABSOLUTE:
    opt = 2;
    break;
  }

  slave_cmd_int_opt (player, SLAVE_SEEK, value, opt);
}

static void
mplayer_dvd_nav (player_t *player, player_dvdnav_t value)
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

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "dvd_nav: %s", log);

  if (!player)
    return;

  if (player->mrl->resource == MRL_RESOURCE_DVDNAV)
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

static int
mplayer_get_time_pos (player_t *player)
{
  float time_pos = 0.0;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "get_time_pos");

  if (!player)
    return 0;

  time_pos = slave_get_property_float (player, PROPERTY_TIME_POS);

  if (time_pos < 0.0)
    return 0;

  return (int) (time_pos * 1000.0);
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

  slave_set_property_flag (player, PROPERTY_MUTE, mute);
}

static void
mplayer_set_sub_delay (player_t *player, float value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "set_sub_delay: %.2f", value);

  if (!player)
    return;

  slave_set_property_float (player, PROPERTY_SUB_DELAY, value);
}

static void
mplayer_set_sub_alignment (player_t *player, player_sub_alignment_t a)
{
  mplayer_sub_alignment_t alignment;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "set_sub_alignment: %i", a);

  if (!player)
    return;

  switch (a)
  {
  case PLAYER_SUB_ALIGNMENT_TOP:
    alignment = MPLAYER_SUB_ALIGNMENT_TOP;
    break;

  case PLAYER_SUB_ALIGNMENT_CENTER:
    alignment = MPLAYER_SUB_ALIGNMENT_CENTER;
    break;

  case PLAYER_SUB_ALIGNMENT_BOTTOM:
    alignment = MPLAYER_SUB_ALIGNMENT_BOTTOM;
    break;

  default:
    return;
  }

  slave_set_property_int (player, PROPERTY_SUB_ALIGNMENT, alignment);
}

static void
mplayer_set_sub_visibility (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "set_sub_visibility: %i", value);

  if (!player)
    return;

  slave_set_property_flag (player, PROPERTY_SUB_VISIBILITY, value);
}

/* public API */
player_funcs_t *
register_functions_mplayer (void)
{
  player_funcs_t *funcs = NULL;

  funcs = calloc (1, sizeof (player_funcs_t));

  funcs->init               = mplayer_init;
  funcs->uninit             = mplayer_uninit;
  funcs->set_verbosity      = mplayer_set_verbosity;

  funcs->mrl_supported_res  = mplayer_mrl_supported_res;
  funcs->mrl_retrieve_props = mplayer_mrl_retrieve_properties;
  funcs->mrl_retrieve_meta  = mplayer_mrl_retrieve_metadata;

  funcs->get_time_pos       = mplayer_get_time_pos;
  funcs->set_framedrop      = NULL;

  funcs->pb_start           = mplayer_playback_start;
  funcs->pb_stop            = mplayer_playback_stop;
  funcs->pb_pause           = mplayer_playback_pause;
  funcs->pb_seek            = mplayer_playback_seek;
  funcs->pb_seek_chapter    = NULL;
  funcs->pb_set_speed       = NULL;

  funcs->get_volume         = mplayer_get_volume;
  funcs->set_volume         = mplayer_set_volume;
  funcs->get_mute           = mplayer_get_mute;
  funcs->set_mute           = mplayer_set_mute;
  funcs->audio_set_delay    = NULL;
  funcs->audio_select       = NULL;
  funcs->audio_prev         = NULL;
  funcs->audio_next         = NULL;

  funcs->video_set_fs       = NULL;
  funcs->video_set_aspect   = NULL;
  funcs->video_set_panscan  = NULL;
  funcs->video_set_ar       = NULL;

  funcs->set_sub_delay      = mplayer_set_sub_delay;
  funcs->set_sub_alignment  = mplayer_set_sub_alignment;
  funcs->set_sub_pos        = NULL;
  funcs->set_sub_visibility = mplayer_set_sub_visibility;
  funcs->sub_scale          = NULL;
  funcs->sub_select         = NULL;
  funcs->sub_prev           = NULL;
  funcs->sub_next           = NULL;

  funcs->dvd_nav            = mplayer_dvd_nav;
  funcs->dvd_angle_set      = NULL;
  funcs->dvd_angle_prev     = NULL;
  funcs->dvd_angle_next     = NULL;
  funcs->dvd_title_set      = NULL;
  funcs->dvd_title_prev     = NULL;
  funcs->dvd_title_next     = NULL;

  funcs->tv_channel_set     = NULL;
  funcs->tv_channel_prev    = NULL;
  funcs->tv_channel_next    = NULL;

  funcs->radio_channel_set  = NULL;
  funcs->radio_channel_prev = NULL;
  funcs->radio_channel_next = NULL;

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

  sem_init (&mplayer->sem, 0, 0);
  pthread_mutex_init (&mplayer->mutex_search, NULL);
  pthread_mutex_init (&mplayer->mutex_status, NULL);
  pthread_mutex_init (&mplayer->mutex_verbosity, NULL);

  return mplayer;
}
