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

typedef enum mplayer_framedropping {
  MPLAYER_FRAMEDROPPING_DISABLE = 0,
  MPLAYER_FRAMEDROPPING_SOFT    = 1,
  MPLAYER_FRAMEDROPPING_HARD    = 2,
} mplayer_framedropping_t;

/* Status of MPlayer child */
typedef enum mplayer_status {
  MPLAYER_IS_IDLE,
  MPLAYER_IS_LOADING,
  MPLAYER_IS_PLAYING,
  MPLAYER_IS_DEAD
} mplayer_status_t;

typedef enum mplayer_eof {
  MPLAYER_EOF_NO,
  MPLAYER_EOF_END,
  MPLAYER_EOF_STOP,
} mplayer_eof_t;

typedef enum checklist {
  CHECKLIST_COMMANDS,
  CHECKLIST_PROPERTIES,
} checklist_t;

/* property and value for a search in the fifo_out */
typedef struct mp_search_s {
  char *property;
  char *value;
} mp_search_t;

/* union for set_property */
typedef union slave_value {
  int i_val;
  float f_val;
  char *s_val;
} slave_value_t;

typedef enum item_state {
  ITEM_OFF  = 0,
  ITEM_ON   = (1 << 0),
  ITEM_HACK = (1 << 1),
} item_state_t;

#define ALL_ITEM_STATES (ITEM_HACK | ITEM_ON)

typedef enum opt_conf {
  OPT_OFF = 0,
  OPT_MIN,
  OPT_MAX,
  OPT_RANGE,
} opt_conf_t;

typedef struct item_opt_s {
  opt_conf_t conf;
  int min;
  int max;
  struct item_opt_s *next;  /* unused ATM */
} item_opt_t;

typedef struct item_list_s {
  const char *str;
  const int state_lib;    /* states of the item in libplayer */
  item_state_t state_mp;  /* state of the item in MPlayer */
  item_opt_t *opt;        /* options of the item in MPlayer */
} item_list_t;

/* player specific structure */
typedef struct mplayer_s {
  item_list_t *slave_cmds;
  item_list_t *slave_props;
  mplayer_status_t status;
  pid_t pid;          /* process pid */
  int pipe_in[2];     /* pipe for send commands to MPlayer */
  int pipe_out[2];    /* pipe for receive results */
  FILE *fifo_in;      /* fifo on the pipe_in  (write only) */
  FILE *fifo_out;     /* fifo on the pipe_out (read only) */
  int verbosity;
  int start_ok;
  /* specific to thread */
  pthread_t th_fifo;      /* thread for the fifo_out parser */
  pthread_mutex_t mutex_search;
  pthread_mutex_t mutex_status;
  pthread_mutex_t mutex_verbosity;
  pthread_mutex_t mutex_start;
  pthread_cond_t cond_start;
  pthread_cond_t cond_status;
  sem_t sem;
  mp_search_t *search;    /* use when a property is searched */
} mplayer_t;

/*****************************************************************************/
/*                              Slave Commands                               */
/*****************************************************************************/

typedef enum slave_cmd {
  SLAVE_UNKNOWN = 0,
  SLAVE_DVDNAV,       /* dvdnav int */
  SLAVE_GET_PROPERTY, /* get_property string */
  SLAVE_LOADFILE,     /* loadfile string [int] */
  SLAVE_PAUSE,        /* pause */
  SLAVE_QUIT,         /* quit [int] */
  SLAVE_SEEK,         /* seek float [int] */
  SLAVE_SET_PROPERTY, /* set_property string string */
  SLAVE_STOP,         /* stop */
  SLAVE_SUB_LOAD,     /* sub_load string */
  SLAVE_SWITCH_TITLE, /* switch_title [int] */
} slave_cmd_t;

static const item_list_t g_slave_cmds[] = {
  [SLAVE_DVDNAV]       = {"dvdnav",       ITEM_ON,             ITEM_OFF, NULL},
  [SLAVE_GET_PROPERTY] = {"get_property", ITEM_ON,             ITEM_OFF, NULL},
  [SLAVE_LOADFILE]     = {"loadfile",     ITEM_ON,             ITEM_OFF, NULL},
  [SLAVE_PAUSE]        = {"pause",        ITEM_ON,             ITEM_OFF, NULL},
  [SLAVE_QUIT]         = {"quit",         ITEM_ON,             ITEM_OFF, NULL},
  [SLAVE_SEEK]         = {"seek",         ITEM_ON,             ITEM_OFF, NULL},
  [SLAVE_SET_PROPERTY] = {"set_property", ITEM_ON,             ITEM_OFF, NULL},
  [SLAVE_STOP]         = {"stop",         ITEM_ON | ITEM_HACK, ITEM_OFF, NULL},
  [SLAVE_SUB_LOAD]     = {"sub_load",     ITEM_ON,             ITEM_OFF, NULL},
  [SLAVE_SWITCH_TITLE] = {"switch_title", ITEM_ON,             ITEM_OFF, NULL},
  [SLAVE_UNKNOWN]      = {NULL,           ITEM_OFF,            ITEM_OFF, NULL}
};
/*                              ^                   ^             ^       ^
 * slave command (const) -------'                   |             |       |
 * state in libplayer (const flags) ----------------'             |       |
 * state in MPlayer (set at the init) ----------------------------'       |
 * options with the command (unused) -------------------------------------'
 */

/*****************************************************************************/
/*                             Slave Properties                              */
/*****************************************************************************/

typedef enum slave_property {
  PROPERTY_UNKNOWN = 0,
  PROPERTY_ANGLE,
  PROPERTY_AUDIO_BITRATE,
  PROPERTY_AUDIO_CODEC,
  PROPERTY_CHANNELS,
  PROPERTY_FRAMEDROPPING,
  PROPERTY_HEIGHT,
  PROPERTY_LOOP,
  PROPERTY_METADATA,
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
  PROPERTY_SPEED,
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

static const item_list_t g_slave_props[] = {
  [PROPERTY_ANGLE]            = {"angle",            ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_AUDIO_BITRATE]    = {"audio_bitrate",    ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_AUDIO_CODEC]      = {"audio_codec",      ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_CHANNELS]         = {"channels",         ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_FRAMEDROPPING]    = {"framedropping",    ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_HEIGHT]           = {"height",           ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_LOOP]             = {"loop",             ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_METADATA]         = {"metadata",         ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_METADATA_ALBUM]   = {"metadata/album",   ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_METADATA_ARTIST]  = {"metadata/artist",  ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_METADATA_COMMENT] = {"metadata/comment", ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_METADATA_GENRE]   = {"metadata/genre",   ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_METADATA_NAME]    = {"metadata/name",    ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_METADATA_TITLE]   = {"metadata/title",   ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_METADATA_TRACK]   = {"metadata/track",   ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_METADATA_YEAR]    = {"metadata/year",    ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_MUTE]             = {"mute",             ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_SAMPLERATE]       = {"samplerate",       ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_SPEED]            = {"speed",            ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_SUB]              = {"sub",              ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_SUB_ALIGNMENT]    = {"sub_alignment",    ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_SUB_DELAY]        = {"sub_delay",        ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_SUB_VISIBILITY]   = {"sub_visibility",   ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_TIME_POS]         = {"time_pos",         ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_VIDEO_BITRATE]    = {"video_bitrate",    ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_VIDEO_CODEC]      = {"video_codec",      ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_VOLUME]           = {"volume",           ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_WIDTH]            = {"width",            ITEM_ON,  ITEM_OFF, NULL},
  [PROPERTY_UNKNOWN]          = {NULL,               ITEM_OFF, ITEM_OFF, NULL}
};
/*                                       ^              ^         ^       ^
 * slave property (const) ---------------'              |         |       |
 * state in libplayer (const flags) --------------------'         |       |
 * state in MPlayer (set at the init) ----------------------------'       |
 * options with the property (set at the init) ---------------------------'
 */


static const int g_slave_cmds_nb =
  sizeof (g_slave_cmds) / sizeof (g_slave_cmds[0]);

static const int g_slave_props_nb =
  sizeof (g_slave_props) / sizeof (g_slave_props[0]);


static void
sig_handler (int signal)
{
  if (signal == SIGPIPE)
    fprintf (stderr, "SIGPIPE detected by the death of MPlayer\n");
}

/*****************************************************************************/
/*                      Properties and Commands Utils                        */
/*****************************************************************************/

static item_state_t
get_state (int lib, item_state_t mp)
{
  int state_lib = lib & ALL_ITEM_STATES;

  if ((state_lib == ITEM_HACK) ||
      (state_lib == ALL_ITEM_STATES && mp == ITEM_OFF))
  {
    return ITEM_HACK;
  }
  else if ((state_lib == ITEM_ON && mp == ITEM_ON) ||
           (state_lib == ALL_ITEM_STATES && mp == ITEM_ON))
  {
    return ITEM_ON;
  }

  return ITEM_OFF;
}

static const char *
get_cmd (player_t *player, slave_cmd_t cmd, item_state_t *state)
{
  mplayer_t *mplayer;
  slave_cmd_t command = SLAVE_UNKNOWN;

  if (!player)
    return NULL;

  mplayer = (mplayer_t *) player->priv;
  if (!mplayer || !mplayer->slave_cmds)
    return NULL;

  if (cmd < g_slave_cmds_nb && cmd >= 0)
    command = cmd;

  if (state)
    *state = get_state (mplayer->slave_cmds[command].state_lib,
                        mplayer->slave_cmds[command].state_mp);

  return mplayer->slave_cmds[command].str;
}

static const char *
get_prop (player_t *player, slave_property_t property, item_state_t *state)
{
  mplayer_t *mplayer;
  slave_property_t prop = PROPERTY_UNKNOWN;

  if (!player)
    return NULL;

  mplayer = (mplayer_t *) player->priv;
  if (!mplayer || !mplayer->slave_props)
    return NULL;

  if (property < g_slave_props_nb && property >= 0)
    prop = property;

  if (state)
    *state = get_state (mplayer->slave_props[prop].state_lib,
                        mplayer->slave_props[prop].state_mp);

  return mplayer->slave_props[prop].str;
}

static opt_conf_t
get_prop_range (player_t *player, slave_property_t property, int *min, int *max)
{
  mplayer_t *mplayer;
  slave_property_t prop = PROPERTY_UNKNOWN;
  item_opt_t *opt;

  if (!player)
    return OPT_OFF;

  mplayer = (mplayer_t *) player->priv;
  if (!mplayer || !mplayer->slave_props)
    return OPT_OFF;

  if (property < g_slave_props_nb && property >= 0)
    prop = property;

  opt = mplayer->slave_props[prop].opt;
  if (!opt)
    return OPT_OFF;

  if (min && (opt->conf == OPT_RANGE || opt->conf == OPT_MIN))
    *min = opt->min;

  if (max && (opt->conf == OPT_RANGE || opt->conf == OPT_MAX))
    *max = opt->max;

  return opt->conf;
}

static int
check_range (player_t *player,
             slave_property_t property, int *value, int update)
{
  int new;
  int min = 0, max = 0;
  opt_conf_t conf;

  if (!value)
    return 0;

  new = *value;

  conf = get_prop_range (player, property, &min, &max);
  switch (conf)
  {
  case OPT_RANGE:
  case OPT_MIN:
    if (*value < min)
      new = min;
    if (conf == OPT_MIN)
      break;

  case OPT_MAX:
    if (*value > max)
      new = max;
    break;

  case OPT_OFF:
  default:
    break;
  }

  if (new != *value)
  {
    const char *p = get_prop (player, property, NULL);

    if (update)
    {
      plog (player, PLAYER_MSG_INFO, MODULE_NAME,
            "fix value for property '%s', %i -> %i", p ? p : "?", *value, new);
      *value = new;
    }
    else
      plog (player, PLAYER_MSG_WARNING, MODULE_NAME,
            "bad value (%i) for property '%s'", *value, p ? p : "?");

    return 0;
  }

  return 1;
}

/*****************************************************************************/
/*                          MPlayer messages Parser                          */
/*****************************************************************************/

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

static void *
thread_fifo (void *arg)
{
  unsigned int skip_msg = 0;
  int start_ok = 1, check_lang = 1, verbosity = 0;
  mplayer_eof_t wait_uninit = MPLAYER_EOF_NO;
  char buffer[FIFO_BUFFER];
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
    verbosity = mplayer->verbosity;
    pthread_mutex_unlock (&mplayer->mutex_verbosity);

    /*
     * NOTE: In order to detect EOF code, that is necessary to set the
     *       msglevel of 'global' to 6. And in this case, the verbosity of
     *       vd_ffmpeg is increased with _a lot of_ useless messages related
     *       to libpostproc for example.
     *
     * This test will search and skip all strings matching this pattern:
     * \[.*@.*\].*
     */
    if (buffer[0] == '['
        && (it = strchr (buffer, '@')) > buffer
        && strchr (buffer, ']') > it)
    {
      if (verbosity)
        skip_msg++;
      continue;
    }

    if (verbosity)
    {
      static player_verbosity_level_t level = PLAYER_MSG_INFO;

      *(buffer + strlen (buffer) - 1) = '\0';

      if (level == PLAYER_MSG_INFO
          && strstr (buffer, "MPlayer interrupted by signal") == buffer)
      {
        level = PLAYER_MSG_CRITICAL;
      }

      if (skip_msg)
      {
        plog (player, PLAYER_MSG_INFO, MODULE_NAME,
              "libplayer has ignored %u msg from MPlayer", skip_msg);
        skip_msg = 0;
      }

      plog (player, level, MODULE_NAME, "[process] %s", buffer);
    }

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
        item_state_t state = ITEM_OFF;
        get_cmd (player, SLAVE_STOP, &state);

        if (state == ITEM_ON)
        {
          pthread_mutex_lock (&mplayer->mutex_status);
          if (mplayer->status == MPLAYER_IS_IDLE)
          {
            pthread_mutex_unlock (&mplayer->mutex_status);
            wait_uninit = MPLAYER_EOF_STOP;
          }
          else
            pthread_mutex_unlock (&mplayer->mutex_status);

          continue;
        }
      }

      wait_uninit = MPLAYER_EOF_END;

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
      {
        pthread_mutex_unlock (&mplayer->mutex_status);

        item_state_t state = ITEM_OFF;
        get_cmd (player, SLAVE_STOP, &state);

        /*
         * Oops, 'stop' is arrived just before "EOF code != 1" and was not
         * handled like a stop.
         */
        if (state == ITEM_ON)
        {
          plog (player, PLAYER_MSG_WARNING,
                MODULE_NAME, "'stop' unexpected detected");
          wait_uninit = MPLAYER_EOF_STOP;
        }
      }
    }

    /*
     * HACK: If the slave command 'stop' is not handled by MPlayer, then this
     *       part will find the end instead of "EOF code: 4".
     */
    else if (strstr (buffer, "File not found: ''") == buffer)
    {
      item_state_t state = ITEM_OFF;
      get_cmd (player, SLAVE_STOP, &state);

      if (state != ITEM_HACK)
        continue;

      pthread_mutex_lock (&mplayer->mutex_status);
      if (mplayer->status == MPLAYER_IS_IDLE)
      {
        pthread_mutex_unlock (&mplayer->mutex_status);
        wait_uninit = MPLAYER_EOF_STOP;
      }
      else
        pthread_mutex_unlock (&mplayer->mutex_status);
    }

    /*
     * Detect when MPlayer playback is really started in order to change
     * the current status.
     */
    else if (strstr (buffer, "Starting playback") == buffer)
    {
      pthread_mutex_lock (&mplayer->mutex_status);
      if (mplayer->status == MPLAYER_IS_LOADING)
      {
        mplayer->status = MPLAYER_IS_PLAYING;
        pthread_cond_signal (&mplayer->cond_status);
      }
      else
        mplayer->status = MPLAYER_IS_PLAYING;
      pthread_mutex_unlock (&mplayer->mutex_status);
    }

    /*
     * If this 'uninit' is detected, we can be sure that nothing is playing.
     * The status of MPlayer will be changed and a signal will be sent if
     * MPlayer was loading a stream.
     * But if EOF is detected before 'uninit', this is considered as a
     * 'stop' or an 'end of stream'.
     */
    else if (strstr (buffer, "*** uninit") == buffer)
    {
      switch (wait_uninit)
      {
      case MPLAYER_EOF_STOP:
        wait_uninit = MPLAYER_EOF_NO;
        sem_post (&mplayer->sem);
        continue;

      case MPLAYER_EOF_END:
        wait_uninit = MPLAYER_EOF_NO;
        continue;

      default:
        pthread_mutex_lock (&mplayer->mutex_status);
        if (mplayer->status == MPLAYER_IS_LOADING)
        {
          mplayer->status = MPLAYER_IS_IDLE;
          pthread_cond_signal (&mplayer->cond_status);
        }
        pthread_mutex_unlock (&mplayer->mutex_status);
      }
    }

    /*
     * Check language used by MPlayer. Only english is supported. A signal
     * is sent to the init as fast as possible. If --language is not found,
     * then MPlayer is in english. But if --language is found, then the
     * first language must be 'en' or 'all'.
     */
    else if (check_lang)
    {
      const char *it;

      if ((it = strstr (buffer, "--language=")))
      {
        if (strncmp (it + 11, "en", 2) &&
            strncmp (it + 11, "all", 3))
        {
          start_ok = 0;
        }
      }
      else if (strstr (buffer, "-slave") && strstr (buffer, "-idle"))
      {
        pthread_mutex_lock (&mplayer->mutex_start);
        mplayer->start_ok = start_ok;
        pthread_cond_signal (&mplayer->cond_start);
        pthread_mutex_unlock (&mplayer->mutex_start);
        check_lang = 0;
      }
    }
  }

  pthread_mutex_lock (&mplayer->mutex_status);
  mplayer->status = MPLAYER_IS_DEAD;
  pthread_mutex_unlock (&mplayer->mutex_status);

  pthread_exit (0);
}

/*****************************************************************************/
/*                              Slave functions                              */
/*****************************************************************************/

static void
send_to_slave (player_t *player, const char *format, ...)
{
  mplayer_t *mplayer;
  va_list va;

  if (!player)
    return;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer)
    return;

  if (!mplayer->fifo_in)
  {
    plog (player, PLAYER_MSG_ERROR, MODULE_NAME,
          "the command can not be sent to slave, stdin unavailable");
    return;
  }

  va_start (va, format);
  vfprintf (mplayer->fifo_in, format, va);
  fprintf (mplayer->fifo_in, "\n");
  fflush (mplayer->fifo_in);
  va_end (va);
}

static void
slave_get_property (player_t *player, slave_property_t property)
{
  const char *prop;
  const char *command;
  item_state_t state;

  if (!player)
    return;

  prop = get_prop (player, property, &state);
  if (!prop || state != ITEM_ON)
    return;

  command = get_cmd (player, SLAVE_GET_PROPERTY, &state);
  if (!command || state != ITEM_ON)
    return;

  send_to_slave (player, "%s %s", command, prop);
}

static char *
slave_result (slave_property_t property, player_t *player)
{
  char str[FIFO_BUFFER];
  const char *prop;
  char *ret = NULL;
  mplayer_t *mplayer = NULL;
  item_state_t state;

  if (!player)
    return NULL;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer || !mplayer->fifo_in || !mplayer->fifo_out)
    return NULL;

  prop = get_prop (player, property, &state);
  if (!prop || state != ITEM_ON)
    return NULL;

  snprintf (str, sizeof (str), "ANS_%s=", prop);

  pthread_mutex_lock (&mplayer->mutex_search);
  mplayer->search = malloc (sizeof (mp_search_t));

  if (mplayer->search)
  {
    mplayer->search->property = strdup (str);
    mplayer->search->value = NULL;
    pthread_mutex_unlock (&mplayer->mutex_search);

    slave_get_property (player, property);

    /* HACK: Old MPlayer versions needs this hack to detect when a property
     *       is unavailable. An error message is returned by the command
     *       'loadfile' (without argument).
     *
     * NOTE: This hack is no longer necessary since MPlayer r26296.
     */
    send_to_slave (player, "loadfile");

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

  if (result)
  {
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

  if (result)
  {
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
  const char *prop;
  const char *command;
  item_state_t state;
  char cmd[FIFO_BUFFER];

  if (!player)
    return;

  prop = get_prop (player, property, &state);
  if (!prop || state != ITEM_ON)
    return;

  command = get_cmd (player, SLAVE_SET_PROPERTY, &state);
  if (!command || state != ITEM_ON)
    return;

  snprintf (cmd, sizeof (cmd), "%s %s", command, prop);

  switch (property)
  {
  case PROPERTY_ANGLE:
  case PROPERTY_FRAMEDROPPING:
  case PROPERTY_LOOP:
  case PROPERTY_MUTE:
  case PROPERTY_SUB:
  case PROPERTY_SUB_ALIGNMENT:
  case PROPERTY_SUB_VISIBILITY:
  case PROPERTY_VOLUME:
    send_to_slave (player, "%s %i", cmd, value.i_val);
    break;

  case PROPERTY_SPEED:
  case PROPERTY_SUB_DELAY:
    send_to_slave (player, "%s %.2f", cmd, value.f_val);
    break;

  default:
    plog (player, PLAYER_MSG_ERROR,
          MODULE_NAME, "the property %i can not be set", property);
    break;
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

  if (!mplayer)
    return;

  command = get_cmd (player, cmd, &state_cmd);
  if (!command || state_cmd == ITEM_OFF)
    return;

  if (state_cmd == ITEM_HACK)
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "[hack] slave command '%s'", command);

  switch (cmd)
  {
  case SLAVE_DVDNAV:
    if (state_cmd == ITEM_ON && value)
      send_to_slave (player, "%s %i", command, value->i_val);
    break;

  case SLAVE_LOADFILE:
    if (state_cmd == ITEM_ON && value && value->s_val)
    {
      pthread_mutex_lock (&mplayer->mutex_status);
      mplayer->status = MPLAYER_IS_LOADING;
      pthread_mutex_unlock (&mplayer->mutex_status);

      send_to_slave (player, "%s \"%s\" %i", command, value->s_val, opt);

      pthread_mutex_lock (&mplayer->mutex_status);
      pthread_cond_wait (&mplayer->cond_status, &mplayer->mutex_status);
      pthread_mutex_unlock (&mplayer->mutex_status);
    }
    break;

  case SLAVE_PAUSE:
    if (state_cmd == ITEM_ON)
      send_to_slave (player, command);
    break;

  case SLAVE_QUIT:
    if (state_cmd == ITEM_ON)
      send_to_slave (player, command);
    break;

  case SLAVE_SEEK:
    if (state_cmd == ITEM_ON && value)
      send_to_slave (player, "%s %i %i", command, value->i_val, opt);
    break;

  case SLAVE_STOP:
    if (state_cmd == ITEM_HACK)
      send_to_slave (player, "loadfile \"\"");
    else if (state_cmd == ITEM_ON)
      send_to_slave (player, command);

    sem_wait (&mplayer->sem);
    break;

  case SLAVE_SUB_LOAD:
    if (state_cmd == ITEM_ON && value && value->s_val)
      send_to_slave (player, "%s \"%s\"", command, value->s_val);
    break;

  case SLAVE_SWITCH_TITLE:
    if (state_cmd == ITEM_ON && value)
      send_to_slave (player, "%s %i", command, value->i_val);
    break;

  default:
    plog (player, PLAYER_MSG_ERROR,
          MODULE_NAME, "what to do with the slave command '%s'?", command);
    break;
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

/*****************************************************************************/
/*                                MRL's args                                 */
/*****************************************************************************/

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
  case MRL_RESOURCE_FILE: /* file://location */
  {
    const char *protocol = protocols[mrl->resource];
    mrl_resource_local_args_t *args = mrl->priv;

    if (!args || !args->location)
      return NULL;

    if (strstr (args->location, "://")
        && strncmp (args->location, protocol, strlen (protocol)))
    {
      return NULL;
    }

    return strdup (args->location);
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
    char *uri, *host_file;
    const char *protocol = protocols[mrl->resource];
    char at[256] = "";
    size_t size = strlen (protocol);
    mrl_resource_network_args_t *args;

    args = mrl->priv;
    if (!args || !args->url)
      break;

    if (strstr (args->url, protocol) == args->url)
      host_file = strdup (args->url + size);
    else
      host_file = strdup (args->url);

    if (!host_file)
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
    size += strlen (host_file);

    size++;
    uri = malloc (size);
    if (!uri)
    {
      free (host_file);
      break;
    }

    snprintf (uri, size, "%s%s%s", protocol, at, host_file);

    free (host_file);

    return uri;
  }

  default:
    break;
  }

  return NULL;
}

static void
mp_resource_load_args (player_t *player, mrl_t *mrl)
{
  if (!player || !mrl || !mrl->priv)
    return;

  switch (mrl->resource)
  {
  /* speed, angle, audio_lang, sub_lang, sub_cc */
  case MRL_RESOURCE_DVD:
  case MRL_RESOURCE_DVDNAV:
  {
    mrl_resource_videodisc_args_t *args = mrl->priv;

    if (args->angle)
    {
      int angle = args->angle;
      if (check_range (player, PROPERTY_ANGLE, &angle, 0))
        slave_set_property_int (player, PROPERTY_ANGLE, args->angle);
    }
    break;
  }

  default:
    break;
  }
}

/*****************************************************************************/
/*                            MPlayer -identify                              */
/*****************************************************************************/

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
  if (strstr (buffer, "ID_CLIP_INFO_N=") == buffer)
  {
    cnt = 0;
    property = PROPERTY_UNKNOWN;
    return 0;
  }

  meta = mrl->meta;

  snprintf (str, sizeof (str), "ID_CLIP_INFO_NAME%i=", cnt);
  it = strstr (buffer, str);
  if (it == buffer)
  {
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
  if (it != buffer)
    return 0;

  switch (property)
  {
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
  if (it == buffer)
  {
    if (audio->codec)
      free (audio->codec);
    audio->codec = strdup (parse_field (it, "ID_AUDIO_CODEC="));
    return 1;
  }

  it = strstr (buffer, "ID_AUDIO_BITRATE=");
  if (it == buffer)
  {
    audio->bitrate = atoi (parse_field (it, "ID_AUDIO_BITRATE="));
    return 1;
  }

  it = strstr (buffer, "ID_AUDIO_NCH=");
  if (it == buffer)
  {
    audio->channels = atoi (parse_field (it, "ID_AUDIO_NCH="));
    return 1;
  }

  it = strstr (buffer, "ID_AUDIO_RATE=");
  if (it == buffer)
  {
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
  if (it == buffer)
  {
    if (video->codec)
      free (video->codec);
    video->codec = strdup (parse_field (it, "ID_VIDEO_CODEC="));
    return 1;
  }

  it = strstr (buffer, "ID_VIDEO_BITRATE=");
  if (it == buffer)
  {
    video->bitrate = atoi (parse_field (it, "ID_VIDEO_BITRATE="));
    return 1;
  }

  it = strstr (buffer, "ID_VIDEO_WIDTH=");
  if (it == buffer)
  {
    video->width = atoi (parse_field (it, "ID_VIDEO_WIDTH="));
    return 1;
  }

  it = strstr (buffer, "ID_VIDEO_HEIGHT=");
  if (it == buffer)
  {
    video->height = atoi (parse_field (it, "ID_VIDEO_HEIGHT="));
    return 1;
  }

  it = strstr (buffer, "ID_VIDEO_ASPECT=");
  if (it == buffer)
  {
    video->aspect =
      (uint32_t) (atof (parse_field (it, "ID_VIDEO_ASPECT=")) * 10000.0);
    return 1;
  }

  it = strstr (buffer, "ID_VIDEO_FPS=");
  if (it == buffer)
  {
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
  if (it == buffer)
  {
    mrl->prop->length =
      (uint32_t) (atof (parse_field (it, "ID_LENGTH=")) * 1000.0);
    return 1;
  }

  it = strstr (buffer, "ID_SEEKABLE=");
  if (it == buffer)
  {
    mrl->prop->seekable = atoi (parse_field (it, "ID_SEEKABLE="));
    return 1;
  }

  return 0;
}

static void
mp_identify (mrl_t *mrl, int flags)
{
  int mp_pipe[2];
  pid_t pid;
  char *uri = NULL;

  if (!mrl)
    return;

  uri = mp_resource_get_uri (mrl);
  if (!uri)
    return;

  if (pipe (mp_pipe))
  {
    free (uri);
    return;
  }

  pid = fork ();

  switch (pid)
  {
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
    params[pp++] = "-msglevel";
    params[pp++] = "all=0:identify=6";
    params[pp] = NULL;

    execvp (MPLAYER_NAME, params);
  }

  case -1:
    break;

  /* I'm your father */
  default:
  {
    char buffer[FIFO_BUFFER];
    int found;
    FILE *mp_fifo;

    free (uri);
    close (mp_pipe[1]);

    mp_fifo = fdopen (mp_pipe[0], "r");

    while (fgets (buffer, FIFO_BUFFER, mp_fifo))
    {
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
}

/*****************************************************************************/
/*                            Pre-Init functions                             */
/*                             - check compatibility, availability           */
/*****************************************************************************/

static void
option_parse_value (char *it)
{
  while (*it != '\0' && *it != '\n' && *it != ' ' && *it != '\t')
    it++;

  *it = '\0';
}

static void
opt_free (item_opt_t *opts)
{
  item_opt_t *opt = opts;

  while (opt)
  {
    opt = opts->next;
    free (opts);
  }
}

static void
item_list_free (item_list_t *list, int nb)
{
  int i;

  if (!list)
    return;

  for (i = 0; i < nb; i++)
    if (list[i].opt)
      opt_free (list[i].opt);

  free (list);
}

static item_opt_t *
mp_prop_get_option (char *buffer, char *it_min, char *it_max)
{
  item_opt_t *opt;
  opt_conf_t opt_min = OPT_MIN;
  opt_conf_t opt_max = OPT_MAX;

  option_parse_value (it_min);
  option_parse_value (it_max);

  if (*it_min == '\0' || *it_max == '\0')
    return NULL;

  if (!strcmp (it_min, "No"))
    opt_min = OPT_OFF;
  if (!strcmp (it_max, "No"))
    opt_max = OPT_OFF;

  if (opt_min == OPT_OFF && opt_max == OPT_OFF)
    return NULL;

  opt = calloc (1, sizeof (item_opt_t));
  if (!opt)
    return NULL;

  if (opt_max == OPT_OFF)
  {
    opt->conf = OPT_MIN;
    opt->min = atoi (it_min);
  }
  else if (opt_min == OPT_OFF)
  {
    opt->conf = OPT_MAX;
    opt->max = atoi (it_max);
  }
  else
  {
    opt->conf = OPT_RANGE;
    opt->min = atoi (it_min);
    opt->max = atoi (it_max);
  }

  return opt;
}

static int
mp_check_compatibility (player_t *player, checklist_t check)
{
  mplayer_t *mplayer;
  int i, nb = 0, res = 1;
  int mp_pipe[2];
  pid_t pid;
  item_list_t *list = NULL;
  const char *str, *what = NULL;
  const int *state_lib;
  item_state_t *state_mp;

  if (!player)
    return 0;

  mplayer = (mplayer_t *) player->priv;
  if (!mplayer)
    return 0;

  if (pipe (mp_pipe))
    return 0;

  switch (check)
  {
  case CHECKLIST_COMMANDS:
    nb = g_slave_cmds_nb;
    list = mplayer->slave_cmds;
    what = "slave command";
    break;

  case CHECKLIST_PROPERTIES:
    nb = g_slave_props_nb;
    list = mplayer->slave_props;
    what = "slave property";
    break;

  default:
    break;
  }

  if (!list || !what)
    return 0;

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

    case CHECKLIST_PROPERTIES:
      params[pp++] = "-list-properties";
      break;

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
    char *it_min = NULL, *it_max = NULL;

    close (mp_pipe[1]);
    mp_fifo = fdopen (mp_pipe[0], "r");

    while (fgets (buffer, FIFO_BUFFER, mp_fifo))
    {
      if (check == CHECKLIST_PROPERTIES && !it_min && !it_max
          && strstr (buffer, "Name") && strstr (buffer, "Type"))
      {
        it_min = strstr (buffer, "Min");
        it_max = strstr (buffer, "Max");
      }

      for (i = 1; i < nb; i++)
      {
        state_mp = &list[i].state_mp;
        str = list[i].str;

        if (!str || !state_mp || *state_mp != ITEM_OFF)
          continue;

        /* all items with '/' will be ignored and automatically set to ENABLE */
        if (strchr (str, '/'))
        {
          *state_mp = ITEM_ON;
          continue;
        }

        buf = strstr (buffer, str);
        if (!buf)
          continue;

        /* search the command|property */
        if ((buf == buffer || (buf > buffer && *(buf - 1) == ' '))
            && (*(buf + strlen (str)) == ' ' || *(buf + strlen (str)) == '\n'))
        {
          *state_mp = ITEM_ON;

          /* only for properties, no range with 'cmdlist' */
          if (it_min && it_max)
            list[i].opt = mp_prop_get_option (buffer, it_min, it_max);
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
    item_opt_t *opt;

    state_mp = &list[i].state_mp;
    state_lib = &list[i].state_lib;
    str = list[i].str;
    opt = list[i].opt;

    if (!str || !state_mp || !state_lib)
      continue;

    state_libplayer = *state_lib & ALL_ITEM_STATES;

    if (strchr (str, '/'))
      continue;

    if (state_libplayer == ITEM_ON && *state_mp == ITEM_OFF)
    {
      plog (player, PLAYER_MSG_ERROR, MODULE_NAME,
            "%s '%s' is needed and not supported by your version of MPlayer",
            what, str);
      res = 0;
    }
    else if (state_libplayer == ITEM_HACK && *state_mp == ITEM_OFF)
    {
      plog (player, PLAYER_MSG_WARNING, MODULE_NAME,
            "%s '%s' is needed and not supported by your version of MPlayer "
            "and libplayer, then a hack is used", what, str);
    }
    else if (state_libplayer == ALL_ITEM_STATES && *state_mp == ITEM_OFF)
    {
      plog (player, PLAYER_MSG_WARNING, MODULE_NAME,
            "%s '%s' is needed and not supported by your version of MPlayer, "
            "then a hack is used", what, str);
    }
    else if (state_libplayer == ITEM_HACK && *state_mp == ITEM_ON)
    {
      plog (player, PLAYER_MSG_WARNING, MODULE_NAME,
            "%s '%s' is supported by your version of MPlayer but not by "
            "libplayer, then a hack is used", what, str);
    }
    else if ((state_libplayer == ITEM_ON && *state_mp == ITEM_ON) ||
             (state_libplayer == ALL_ITEM_STATES && *state_mp == ITEM_ON))
    {
      plog (player, PLAYER_MSG_INFO, MODULE_NAME,
            "%s '%s' is supported by your version of MPlayer", what, str);

      if (opt)
        plog (player, PLAYER_MSG_INFO, MODULE_NAME,
              " *** conf:%i min:%i max:%i", opt->conf, opt->min, opt->max);
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

  for (p = strtok (p, ":"); p; p = strtok (NULL, ":"))
  {
    snprintf (prog, sizeof (prog), "%s/%s", p, bin);
    if (!access (prog, X_OK))
    {
      free (fp);
      return 1;
    }
  }

  plog (player, PLAYER_MSG_ERROR,
        MODULE_NAME, "%s executable not found in the PATH", bin);

  free (fp);
  return 0;
}

/*****************************************************************************/
/*                           Private Wrapper funcs                           */
/*****************************************************************************/
/*
 *                              Slave functions
 *                     Only use these to command MPlayer.
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Commands
 *   void  slave_cmd                (player_t, slave_cmd_t)
 *   void  slave_cmd_int            (player_t, slave_cmd_t,      int)
 *   void  slave_cmd_int_opt        (player_t, slave_cmd_t,      int,    int)
 *   void  slave_cmd_str            (player_t, slave_cmd_t,      char *)
 *   void  slave_cmd_str_opt        (player_t, slave_cmd_t,      char *, int)
 *
 * Get properties
 *   int   slave_get_property_int   (player_t, slave_property_t)
 *   float slave_get_property_float (player_t, slave_property_t)
 *   char *slave_get_property_str   (player_t, slave_property_t)
 *
 * Set properties
 *   void  slave_set_property_int   (player_t, slave_property_t, int)
 *   void  slave_set_property_float (player_t, slave_property_t, float)
 *   void  slave_set_property_flag  (player_t, slave_property_t, int)
 */

static init_status_t
mplayer_init (player_t *player)
{
  struct sigaction action;
  mplayer_t *mplayer = NULL;
  char winid[32];
  int use_x11 = 0;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "init");

  if (!player)
    return PLAYER_INIT_ERROR;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer)
    return PLAYER_INIT_ERROR;

  /* test if MPlayer is available */
  if (!executable_is_available (player, MPLAYER_NAME))
    return PLAYER_INIT_ERROR;

  /* copy g_slave_cmds and g_slave_props */
  mplayer->slave_cmds = malloc (sizeof (g_slave_cmds));
  mplayer->slave_props = malloc (sizeof (g_slave_props));

  if (!mplayer->slave_cmds || !mplayer->slave_props)
    return PLAYER_INIT_ERROR;

  memcpy (mplayer->slave_cmds, g_slave_cmds, sizeof (g_slave_cmds));
  memcpy (mplayer->slave_props, g_slave_props, sizeof (g_slave_props));

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "check MPlayer compatibility");

  if (!mp_check_compatibility (player, CHECKLIST_COMMANDS))
    return PLAYER_INIT_ERROR;

  if (!mp_check_compatibility (player, CHECKLIST_PROPERTIES))
    return PLAYER_INIT_ERROR;

  /* action for SIGPIPE */
  action.sa_handler = sig_handler;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  sigaction (SIGPIPE, &action, NULL);

  /* The video out is sent in our X11 window, winid is used for -wid arg. */
  switch (player->vo)
  {
  case PLAYER_VO_X11:
  case PLAYER_VO_XV:
  case PLAYER_VO_GL:
  case PLAYER_VO_AUTO:
    use_x11 = x11_init (player);
    if (player->vo != PLAYER_VO_AUTO && !use_x11)
    {
      plog (player, PLAYER_MSG_ERROR,
            MODULE_NAME, "initialization for X has failed");
      return PLAYER_INIT_ERROR;
    }
    snprintf (winid, sizeof (winid), "%lu", (unsigned long) x11_get_window (player->x11));
  default:
    break;
  }

  if (pipe (mplayer->pipe_in))
    return PLAYER_INIT_ERROR;

  if (pipe (mplayer->pipe_out))
  {
    close (mplayer->pipe_in[0]);
    close (mplayer->pipe_in[1]);
    return PLAYER_INIT_ERROR;
  }

  mplayer->pid = fork ();

  switch (mplayer->pid)
  {
  /* the son (a new hope) */
  case 0:
  {
    char *params[32];
    int pp = 0;

    close (mplayer->pipe_in[1]);
    close (mplayer->pipe_out[0]);

    dup2 (mplayer->pipe_in[0], STDIN_FILENO);

    dup2 (mplayer->pipe_out[1], STDERR_FILENO);
    dup2 (mplayer->pipe_out[1], STDOUT_FILENO);

    /* default MPlayer arguments */
    params[pp++] = MPLAYER_NAME;
    params[pp++] = "-slave";            /* work in slave mode */
    params[pp++] = "-quiet";            /* reduce output messages */
    params[pp++] = "-msglevel";
    params[pp++] = "all=2:global=6:cplayer=7";
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
    /* TODO: possibility to add parameters for each video output */
    switch (player->vo)
    {
    case PLAYER_VO_NULL:
      params[pp++] = "-vo";
      params[pp++] = "null";
      break;

    case PLAYER_VO_X11:
      params[pp++] = "-vo";
      params[pp++] = "x11";
      break;

    case PLAYER_VO_XV:
      params[pp++] = "-vo";
      params[pp++] = "xv";
      break;

    case PLAYER_VO_GL:
      params[pp++] = "-vo";
      params[pp++] = "gl";
      break;

    case PLAYER_VO_FB:
      params[pp++] = "-vo";
      params[pp++] = "fbdev";
      break;

    case PLAYER_VO_AUTO:
    default:
      break;
    }

    if (use_x11)
    {
      params[pp++] = "-wid";
      params[pp++] = winid;
    }

    /* select the audio output */
    /* TODO: possibility to add parameters for each audio output */
    switch (player->ao)
    {
    case PLAYER_AO_NULL:
      params[pp++] = "-ao";
      params[pp++] = "null";
      break;

    case PLAYER_AO_ALSA:
      params[pp++] = "-ao";
      params[pp++] = "alsa";
      break;

    case PLAYER_AO_OSS:
      params[pp++] = "-ao";
      params[pp++] = "oss";
      break;

    case PLAYER_AO_AUTO:
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
    pthread_attr_t attr;

    close (mplayer->pipe_in[0]);
    close (mplayer->pipe_out[1]);

    mplayer->fifo_in = fdopen (mplayer->pipe_in[1], "w");
    mplayer->fifo_out = fdopen (mplayer->pipe_out[0], "r");

    plog (player, PLAYER_MSG_INFO, MODULE_NAME, "MPlayer child loaded");

    mplayer->status = MPLAYER_IS_IDLE;

    pthread_attr_init (&attr);
    pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_JOINABLE);

    pthread_mutex_lock (&mplayer->mutex_start);
    if (pthread_create (&mplayer->th_fifo, &attr,
                        thread_fifo, (void *) player) >= 0)
    {
      int start_ok;

      pthread_cond_wait (&mplayer->cond_start, &mplayer->mutex_start);
      start_ok = mplayer->start_ok;
      pthread_mutex_unlock (&mplayer->mutex_start);

      pthread_attr_destroy (&attr);

      if (!start_ok)
      {
        plog (player, PLAYER_MSG_ERROR,
              MODULE_NAME, "only english version of MPlayer is supported");
        return PLAYER_INIT_ERROR;
      }

      return PLAYER_INIT_OK;
    }
    pthread_mutex_unlock (&mplayer->mutex_start);

    pthread_attr_destroy (&attr);
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

  if (mplayer->fifo_in)
  {
    /* suicide of MPlayer */
    slave_cmd (player, SLAVE_QUIT);

    /* wait the death of the thread fifo_out */
    pthread_join (mplayer->th_fifo, &ret);

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

  item_list_free (mplayer->slave_cmds, g_slave_cmds_nb);
  item_list_free (mplayer->slave_props, g_slave_props_nb);

  pthread_cond_destroy (&mplayer->cond_start);
  pthread_cond_destroy (&mplayer->cond_status);
  pthread_mutex_destroy (&mplayer->mutex_search);
  pthread_mutex_destroy (&mplayer->mutex_status);
  pthread_mutex_destroy (&mplayer->mutex_verbosity);
  pthread_mutex_destroy (&mplayer->mutex_start);
  sem_destroy (&mplayer->sem);

  free (mplayer);
}

static void
mplayer_set_verbosity (player_t *player, player_verbosity_level_t level)
{
  mplayer_t *mplayer;
  int verbosity = -1;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "set_verbosity");

  if (!player)
    return;

  mplayer = (mplayer_t *) player->priv;

  if (!mplayer)
    return;

  switch (level)
  {
  case PLAYER_MSG_INFO:
  case PLAYER_MSG_WARNING:
  case PLAYER_MSG_ERROR:
  case PLAYER_MSG_CRITICAL:
    verbosity = 1;
    break;

  case PLAYER_MSG_NONE:
    verbosity = 0;
    break;

  default:
    break;
  }

  if (verbosity != -1)
  {
    pthread_mutex_lock (&mplayer->mutex_verbosity);
    mplayer->verbosity = verbosity;
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
      const char *location = args->location;

      if (strstr (location, "file://") == location)
        location += 7;

      stat (location, &st);
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

  if (video)
  {
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

  if (audio)
  {
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
  if (mplayer->status != MPLAYER_IS_PLAYING)
  {
    pthread_mutex_unlock (&mplayer->mutex_status);
    return PLAYER_PB_ERROR;
  }
  pthread_mutex_unlock (&mplayer->mutex_status);

  /* set parameters */
  mp_resource_load_args (player, player->mrl);

  /* load subtitle if exists */
  if (player->mrl->subs)
  {
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
  if (mplayer->status != MPLAYER_IS_PLAYING)
  {
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

  switch (seek)
  {
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
mplayer_playback_set_speed (player_t *player, float value)
{
  int speed;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_set_speed %.2f", value);

  if (!player)
    return;

  /*
   * min value for 'speed' must be 0.01 but `mplayer -list-properties`
   * returns only int and 0 for this property.
   */
  if (value < 0.01)
    return;

  speed = (int) rintf (value);
  if (!check_range (player, PROPERTY_SPEED, &speed, 0))
    return;

  slave_set_property_float (player, PROPERTY_SPEED, value);
}

static int
mplayer_get_volume (player_t *player)
{
  int volume = -1;

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

  if (buffer)
  {
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
    return -1;

  time_pos = slave_get_property_float (player, PROPERTY_TIME_POS);

  if (time_pos < 0.0)
    return -1;

  return (int) (time_pos * 1000.0);
}

static void
mplayer_set_framedrop (player_t *player, player_framedrop_t fd)
{
  mplayer_framedropping_t framedrop;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "set_framedrop: %i", fd);

  if (!player)
    return;

  switch (fd)
  {
  case PLAYER_FRAMEDROP_DISABLE:
    framedrop = MPLAYER_FRAMEDROPPING_DISABLE;
    break;

  case PLAYER_FRAMEDROP_SOFT:
    framedrop = MPLAYER_FRAMEDROPPING_SOFT;
    break;

  case PLAYER_FRAMEDROP_HARD:
    framedrop = MPLAYER_FRAMEDROPPING_HARD;
    break;

  default:
    return;
  }

  slave_set_property_int (player, PROPERTY_FRAMEDROPPING, framedrop);
}

static void
mplayer_set_volume (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "set_volume: %d", value);

  if (!player)
    return;

  if (!check_range (player, PROPERTY_VOLUME, &value, 0))
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

static void
mplayer_dvd_nav (player_t *player, player_dvdnav_t value)
{
  int action;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "dvd_nav: %i", value);

  if (!player)
    return;

  switch (value)
  {
  case PLAYER_DVDNAV_UP:
    action = MPLAYER_DVDNAV_UP;
    break;

  case PLAYER_DVDNAV_DOWN:
    action = MPLAYER_DVDNAV_DOWN;
    break;

  case PLAYER_DVDNAV_LEFT:
    action = MPLAYER_DVDNAV_LEFT;
    break;

  case PLAYER_DVDNAV_RIGHT:
    action = MPLAYER_DVDNAV_RIGHT;
    break;

  case PLAYER_DVDNAV_MENU:
    action = MPLAYER_DVDNAV_MENU;
    break;

  case PLAYER_DVDNAV_SELECT:
    action = MPLAYER_DVDNAV_SELECT;
    break;

  default:
    return;
  }

  slave_cmd_int (player, SLAVE_DVDNAV, action);
}

static void
mplayer_dvd_angle_set (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "dvd_angle_set: %i", value);

  if (!player)
    return;

  if (!check_range (player, PROPERTY_ANGLE, &value, 0))
    return;

  slave_set_property_int (player, PROPERTY_ANGLE, value);
}

static void
mplayer_dvd_angle_prev (player_t *player)
{
  int angle;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "dvd_angle_prev");

  if (!player)
    return;

  angle = slave_get_property_int (player, PROPERTY_ANGLE);
  angle--;

  check_range (player, PROPERTY_ANGLE, &angle, 1);
  slave_set_property_int (player, PROPERTY_ANGLE, angle);
}

static void
mplayer_dvd_angle_next (player_t *player)
{
  int angle;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "dvd_angle_next");

  if (!player)
    return;

  angle = slave_get_property_int (player, PROPERTY_ANGLE);
  angle++;

  check_range (player, PROPERTY_ANGLE, &angle, 1);
  slave_set_property_int (player, PROPERTY_ANGLE, angle);
}

static void
mplayer_dvd_title_set (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "dvd_title_set: %i", value);

  if (!player)
    return;

  if (value < 1 || value > 99)
    return;

  slave_cmd_int (player, SLAVE_SWITCH_TITLE, value);
}

/*****************************************************************************/
/*                           Public Wrapper API                              */
/*****************************************************************************/

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
  funcs->set_framedrop      = mplayer_set_framedrop;

  funcs->pb_start           = mplayer_playback_start;
  funcs->pb_stop            = mplayer_playback_stop;
  funcs->pb_pause           = mplayer_playback_pause;
  funcs->pb_seek            = mplayer_playback_seek;
  funcs->pb_seek_chapter    = NULL;
  funcs->pb_set_speed       = mplayer_playback_set_speed;

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
  funcs->dvd_angle_set      = mplayer_dvd_angle_set;
  funcs->dvd_angle_prev     = mplayer_dvd_angle_prev;
  funcs->dvd_angle_next     = mplayer_dvd_angle_next;
  funcs->dvd_title_set      = mplayer_dvd_title_set;
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
  pthread_cond_init (&mplayer->cond_start, NULL);
  pthread_cond_init (&mplayer->cond_status, NULL);
  pthread_mutex_init (&mplayer->mutex_search, NULL);
  pthread_mutex_init (&mplayer->mutex_status, NULL);
  pthread_mutex_init (&mplayer->mutex_verbosity, NULL);
  pthread_mutex_init (&mplayer->mutex_start, NULL);

  return mplayer;
}
