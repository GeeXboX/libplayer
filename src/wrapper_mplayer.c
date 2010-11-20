/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2007-2009 Mathieu Schroeter <mathieu@schroetersa.ch>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdlib.h>
#include <inttypes.h>
#include <stdio.h>
#include <fcntl.h>        /* open */
#include <string.h>       /* strstr strlen memcpy strdup */
#include <stdarg.h>       /* va_start va_end */
#include <unistd.h>       /* pipe fork close dup2 */
#include <math.h>         /* rintf */
#include <sys/wait.h>     /* waitpid */
#include <pthread.h>      /* pthread_... */
#include <semaphore.h>    /* sem_post sem_wait sem_init sem_destroy */

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "playlist.h"
#include "event.h"
#include "fs_utils.h"
#include "parse_utils.h"
#include "window.h"
#include "wrapper_mplayer.h"

#define MODULE_NAME "mplayer"

#define FIFO_BUFFER      256
#define PATH_BUFFER      512
#define MPLAYER_NAME     "mplayer"

#define SNAPSHOT_FILE    "00000001"
#define SNAPSHOT_TMP     "/tmp/libplayer.XXXXXX"

typedef enum mplayer_dvdnav {
  MPLAYER_DVDNAV_UP     = 1,
  MPLAYER_DVDNAV_DOWN   = 2,
  MPLAYER_DVDNAV_LEFT   = 3,
  MPLAYER_DVDNAV_RIGHT  = 4,
  MPLAYER_DVDNAV_MENU   = 5,
  MPLAYER_DVDNAV_SELECT = 6,
  MPLAYER_DVDNAV_PREV   = 7,
  MPLAYER_DVDNAV_MOUSE  = 8,
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

typedef enum mplayer_seek {
  MPLAYER_SEEK_RELATIVE = 0,
  MPLAYER_SEEK_PERCENT  = 1,
  MPLAYER_SEEK_ABSOLUTE = 2,
} mplayer_seek_t;

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

typedef struct mp_identify_clip_s {
  int cnt;
  int property;
} mp_identify_clip_t;

/* union for set_property */
typedef union slave_value {
  int i_val;
  float f_val;
  const char *s_val;
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
  item_state_t state_mp;  /* state of the item in MPlayer    */
  item_opt_t *opt;        /* options of the item in MPlayer  */
} item_list_t;

/* player specific structure */
typedef struct mplayer_s {
  item_list_t *slave_cmds;    /* private list of commands   */
  item_list_t *slave_props;   /* private list of properties */
  pid_t        pid;           /* forked process PID         */

  /* manage the initialization of MPlayer */
  pthread_mutex_t mutex_start;
  pthread_cond_t  cond_start;
  int             start_ok;

  /* communications between the father and the son         */
  int   pipe_in[2];   /* pipe to send commands to MPlayer  */
  int   pipe_out[2];  /* pipe to receive results           */
  FILE *fifo_in;      /* fifo on the pipe_in  (write only) */
  FILE *fifo_out;     /* fifo on the pipe_out (read only)  */
  pthread_t th_fifo;

  sem_t sem;  /* common to 'loadfile' and 'get_property' */

  /* for the MPlayer properties, see slave_result() */
  pthread_mutex_t mutex_search;
  mp_search_t     search;

  pthread_mutex_t mutex_verbosity;
  int             verbosity;

  /* manage the status of MPlayer */
  pthread_cond_t   cond_status;
  pthread_mutex_t  mutex_status;
  mplayer_status_t status;
} mplayer_t;

/*
 * Paused mode is lost without using pausing_keep. But this causes the media
 * to advance a bit.
 */
#define SLAVE_CMD_PREFIX "pausing_keep "

/* entry for an array of item_list_t */
#define ILT(s, f) { s, f, ITEM_OFF, NULL }

/*****************************************************************************/
/*                              Slave Commands                               */
/*****************************************************************************/

typedef enum slave_cmd {
  SLAVE_UNKNOWN = 0,
  SLAVE_DVDNAV,             /* dvdnav              int                       */
  SLAVE_GET_PROPERTY,       /* get_property        string                    */
  SLAVE_LOADFILE,           /* loadfile            string [int]              */
  SLAVE_OSD_SHOW_TEXT,      /* osd_show_text       string [int]   [int]      */
  SLAVE_PAUSE,              /* pause                                         */
  SLAVE_QUIT,               /* quit               [int]                      */
  SLAVE_RADIO_SET_CHANNEL,  /* radio_set_channel   string                    */
  SLAVE_RADIO_STEP_CHANNEL, /* radio_step_channel  int                       */
  SLAVE_SEEK,               /* seek                float  [int]              */
  SLAVE_SEEK_CHAPTER,       /* seek_chapter        int    [int]              */
  SLAVE_SET_MOUSE_POS,      /* set_mouse_pos       int     int               */
  SLAVE_SET_PROPERTY,       /* set_property        string  string            */
  SLAVE_STOP,               /* stop                                          */
  SLAVE_SUB_LOAD,           /* sub_load            string                    */
  SLAVE_SUB_POS,            /* sub_pos             int                       */
  SLAVE_SUB_SCALE,          /* sub_scale           int    [int]              */
  SLAVE_SWITCH_RATIO,       /* switch_ratio        float                     */
  SLAVE_SWITCH_TITLE,       /* switch_title       [int]                      */
  SLAVE_TV_SET_CHANNEL,     /* tv_set_channel      string                    */
  SLAVE_TV_SET_NORM,        /* tv_set_norm         string                    */
  SLAVE_TV_STEP_CHANNEL,    /* tv_step_channel     int                       */
  SLAVE_VOLUME,             /* volume              int    [int]              */
} slave_cmd_t;

static const item_list_t g_slave_cmds[] = {
  [SLAVE_DVDNAV]             = ILT ("dvdnav",             ITEM_ON            ),
  [SLAVE_GET_PROPERTY]       = ILT ("get_property",       ITEM_ON            ),
  [SLAVE_LOADFILE]           = ILT ("loadfile",           ITEM_ON            ),
  [SLAVE_OSD_SHOW_TEXT]      = ILT ("osd_show_text",      ITEM_ON            ),
  [SLAVE_PAUSE]              = ILT ("pause",              ITEM_ON            ),
  [SLAVE_QUIT]               = ILT ("quit",               ITEM_ON            ),
  [SLAVE_RADIO_SET_CHANNEL]  = ILT ("radio_set_channel",  ITEM_ON            ),
  [SLAVE_RADIO_STEP_CHANNEL] = ILT ("radio_step_channel", ITEM_ON            ),
  [SLAVE_SEEK]               = ILT ("seek",               ITEM_ON            ),
  [SLAVE_SEEK_CHAPTER]       = ILT ("seek_chapter",       ITEM_ON            ),
  [SLAVE_SET_MOUSE_POS]      = ILT ("set_mouse_pos",      ITEM_ON            ),
  [SLAVE_SET_PROPERTY]       = ILT ("set_property",       ITEM_ON            ),
  [SLAVE_STOP]               = ILT ("stop",               ITEM_ON | ITEM_HACK),
  [SLAVE_SUB_LOAD]           = ILT ("sub_load",           ITEM_ON            ),
  [SLAVE_SUB_POS]            = ILT ("sub_pos",            ITEM_ON            ),
  [SLAVE_SUB_SCALE]          = ILT ("sub_scale",          ITEM_ON            ),
  [SLAVE_SWITCH_RATIO]       = ILT ("switch_ratio",       ITEM_ON            ),
  [SLAVE_SWITCH_TITLE]       = ILT ("switch_title",       ITEM_ON            ),
  [SLAVE_TV_SET_CHANNEL]     = ILT ("tv_set_channel",     ITEM_ON            ),
  [SLAVE_TV_SET_NORM]        = ILT ("tv_set_norm",        ITEM_ON            ),
  [SLAVE_TV_STEP_CHANNEL]    = ILT ("tv_step_channel",    ITEM_ON            ),
  [SLAVE_VOLUME]             = ILT ("volume",             ITEM_ON            ),
  [SLAVE_UNKNOWN]            = ILT (NULL,                 ITEM_OFF           )
};

/*****************************************************************************/
/*                             Slave Properties                              */
/*****************************************************************************/

typedef enum slave_property {
  PROPERTY_UNKNOWN = 0,
  PROPERTY_ANGLE,
  PROPERTY_AUDIO_BITRATE,
  PROPERTY_AUDIO_CODEC,
  PROPERTY_AUDIO_DELAY,
  PROPERTY_CHANNELS,
  PROPERTY_FRAMEDROPPING,
  PROPERTY_HEIGHT,
  PROPERTY_LOOP,
  PROPERTY_METADATA,
  PROPERTY_METADATA_ALBUM,
  PROPERTY_METADATA_ARTIST,
  PROPERTY_METADATA_AUTHOR,
  PROPERTY_METADATA_COMMENT,
  PROPERTY_METADATA_GENRE,
  PROPERTY_METADATA_NAME,
  PROPERTY_METADATA_TITLE,
  PROPERTY_METADATA_TRACK,
  PROPERTY_METADATA_YEAR,
  PROPERTY_MUTE,
  PROPERTY_OSDLEVEL,
  PROPERTY_PERCENT_POS,
  PROPERTY_SAMPLERATE,
  PROPERTY_SPEED,
  PROPERTY_SUB,
  PROPERTY_SUB_ALIGNMENT,
  PROPERTY_SUB_DELAY,
  PROPERTY_SUB_VISIBILITY,
  PROPERTY_SWITCH_AUDIO,
  PROPERTY_TIME_POS,
  PROPERTY_VIDEO_BITRATE,
  PROPERTY_VIDEO_CODEC,
  PROPERTY_VOLUME,
  PROPERTY_WIDTH
} slave_property_t;

static const item_list_t g_slave_props[] = {
  [PROPERTY_ANGLE]            = ILT ("angle",            ITEM_ON ),
  [PROPERTY_AUDIO_BITRATE]    = ILT ("audio_bitrate",    ITEM_ON ),
  [PROPERTY_AUDIO_CODEC]      = ILT ("audio_codec",      ITEM_ON ),
  [PROPERTY_AUDIO_DELAY]      = ILT ("audio_delay",      ITEM_ON ),
  [PROPERTY_CHANNELS]         = ILT ("channels",         ITEM_ON ),
  [PROPERTY_FRAMEDROPPING]    = ILT ("framedropping",    ITEM_ON ),
  [PROPERTY_HEIGHT]           = ILT ("height",           ITEM_ON ),
  [PROPERTY_LOOP]             = ILT ("loop",             ITEM_ON ),
  [PROPERTY_METADATA]         = ILT ("metadata",         ITEM_ON ),
  [PROPERTY_METADATA_ALBUM]   = ILT ("metadata/album",   ITEM_ON ),
  [PROPERTY_METADATA_ARTIST]  = ILT ("metadata/artist",  ITEM_ON ),
  [PROPERTY_METADATA_AUTHOR]  = ILT ("metadata/author",  ITEM_ON ),
  [PROPERTY_METADATA_COMMENT] = ILT ("metadata/comment", ITEM_ON ),
  [PROPERTY_METADATA_GENRE]   = ILT ("metadata/genre",   ITEM_ON ),
  [PROPERTY_METADATA_NAME]    = ILT ("metadata/name",    ITEM_ON ),
  [PROPERTY_METADATA_TITLE]   = ILT ("metadata/title",   ITEM_ON ),
  [PROPERTY_METADATA_TRACK]   = ILT ("metadata/track",   ITEM_ON ),
  [PROPERTY_METADATA_YEAR]    = ILT ("metadata/year",    ITEM_ON ),
  [PROPERTY_MUTE]             = ILT ("mute",             ITEM_ON ),
  [PROPERTY_OSDLEVEL]         = ILT ("osdlevel",         ITEM_ON ),
  [PROPERTY_PERCENT_POS]      = ILT ("percent_pos",      ITEM_ON ),
  [PROPERTY_SAMPLERATE]       = ILT ("samplerate",       ITEM_ON ),
  [PROPERTY_SPEED]            = ILT ("speed",            ITEM_ON ),
  [PROPERTY_SUB]              = ILT ("sub",              ITEM_ON ),
  [PROPERTY_SUB_ALIGNMENT]    = ILT ("sub_alignment",    ITEM_ON ),
  [PROPERTY_SUB_DELAY]        = ILT ("sub_delay",        ITEM_ON ),
  [PROPERTY_SUB_VISIBILITY]   = ILT ("sub_visibility",   ITEM_ON ),
  [PROPERTY_SWITCH_AUDIO]     = ILT ("switch_audio",     ITEM_ON ),
  [PROPERTY_TIME_POS]         = ILT ("time_pos",         ITEM_ON ),
  [PROPERTY_VIDEO_BITRATE]    = ILT ("video_bitrate",    ITEM_ON ),
  [PROPERTY_VIDEO_CODEC]      = ILT ("video_codec",      ITEM_ON ),
  [PROPERTY_VOLUME]           = ILT ("volume",           ITEM_ON ),
  [PROPERTY_WIDTH]            = ILT ("width",            ITEM_ON ),
  [PROPERTY_UNKNOWN]          = ILT (NULL,               ITEM_OFF),
};


static const unsigned int g_slave_cmds_nb = ARRAY_NB_ELEMENTS (g_slave_cmds);
static const unsigned int g_slave_props_nb = ARRAY_NB_ELEMENTS (g_slave_props);


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

  mplayer = player->priv;
  if (!mplayer || !mplayer->slave_cmds)
    return NULL;

  if (cmd < g_slave_cmds_nb)
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

  mplayer = player->priv;
  if (!mplayer || !mplayer->slave_props)
    return NULL;

  if (property < g_slave_props_nb)
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

  mplayer = player->priv;
  if (!mplayer || !mplayer->slave_props)
    return OPT_OFF;

  if (property < g_slave_props_nb)
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
      pl_log (player, PLAYER_MSG_INFO, MODULE_NAME,
              "fix value for property '%s', %i -> %i",
              p ? p : "?", *value, new);
      *value = new;
    }
    else
      pl_log (player, PLAYER_MSG_WARNING, MODULE_NAME,
              "bad value (%i) for property '%s'", *value, p ? p : "?");

    return 0;
  }

  return 1;
}

static mplayer_status_t
get_mplayer_status (player_t *player)
{
  mplayer_t *mplayer = player->priv;
  mplayer_status_t status;

  if (!mplayer)
    return MPLAYER_IS_DEAD;

  pthread_mutex_lock (&mplayer->mutex_status);
  status = mplayer->status;
  pthread_mutex_unlock (&mplayer->mutex_status);
  return status;
}

/*****************************************************************************/
/*                              MPlayer Parser                               */
/*****************************************************************************/

static int
parse_msf (const char *str)
{
  int res;
  int m, s, f;

  if (!str)
    return 0;

  res = sscanf (str, "%i:%i:%i", &m, &s, &f);

  if (res != 3)
    return 0;

  return m * 60 * 1000 + s * 1000 + f;
}

static char *
parse_field (char *line)
{
  char *its;

  /* value start */
  its = strchr (line, '=');
  if (!its)
    return line;
  its++;

  return pl_trim_whitespaces (its);
}

static void *
thread_fifo (void *arg)
{
  player_verbosity_level_t level = PLAYER_MSG_VERBOSE;
  unsigned int skip_msg = 0;
  int start_ok = 1, check_init = 1, verbosity = 0;
  mplayer_eof_t wait_uninit = MPLAYER_EOF_NO;
  char buffer[FIFO_BUFFER];
  char *it;
  player_t *player;
  mplayer_t *mplayer;

  player = (player_t *) arg;

  if (!player)
    pthread_exit (NULL);

  mplayer = player->priv;

  if (!mplayer || !mplayer->fifo_out)
    pthread_exit (NULL);

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
      *(buffer + strlen (buffer) - 1) = '\0';

      if (level == PLAYER_MSG_VERBOSE
          && strstr (buffer, "MPlayer interrupted by signal") == buffer)
      {
        level = PLAYER_MSG_CRITICAL;
      }

      if (skip_msg)
      {
        pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME,
                "libplayer has ignored %u msg from MPlayer", skip_msg);
        skip_msg = 0;
      }

      pl_log (player, level, MODULE_NAME, "[process] %s", buffer);

      if (strstr (buffer, "No stream found to handle url") == buffer)
        pl_log (player, PLAYER_MSG_WARNING,
                MODULE_NAME, "%s with this version of MPlayer", buffer);
    }

    /*
     * Here, the result of a property requested by the slave command
     * 'get_property', is searched and saved.
     */
    pthread_mutex_lock (&mplayer->mutex_search);
    if (mplayer->search.property &&
        (it = strstr (buffer, mplayer->search.property)) == buffer)
    {
      it = parse_field (it);
      mplayer->search.value = strdup (it);
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
      if (mplayer->search.property)
      {
        PFREE (mplayer->search.property);
        mplayer->search.property = NULL;
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
          if (get_mplayer_status (player) == MPLAYER_IS_IDLE)
            wait_uninit = MPLAYER_EOF_STOP;
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

        pl_log (player, PLAYER_MSG_INFO,
                MODULE_NAME, "Playback of stream has ended");

        player_event_send (player, PLAYER_EVENT_PLAYBACK_FINISHED);

        pl_window_unmap (player->window);
      }
      else
      {
        pthread_mutex_unlock (&mplayer->mutex_status);

        item_state_t state = ITEM_OFF;
        get_cmd (player, SLAVE_STOP, &state);

        /*
         * Oops, 'stop' is arrived just after the parsing of "EOF code" and
         * it was not handled like a stop but like an end of file.
         */
        if (state == ITEM_ON)
        {
          pl_log (player, PLAYER_MSG_WARNING,
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

      if (get_mplayer_status (player) == MPLAYER_IS_IDLE)
        wait_uninit = MPLAYER_EOF_STOP;
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
     * This test is useful when MPlayer can't execute a slave command because
     * the buffer is full. It happens for example with 'loadfile', when the
     * location of the file is very long (256 or 4096 with MPlayer >= r29403).
     */
    else if (strstr (buffer,
                     "Command buffer of file descriptor 0 is full") == buffer)
    {
      pl_log (player, PLAYER_MSG_ERROR,
              MODULE_NAME, "MPlayer slave buffer is full. "
                           "It happens when a slave command is too large.");

      pthread_mutex_lock (&mplayer->mutex_status);
      if (mplayer->status == MPLAYER_IS_LOADING)
      {
        pthread_cond_signal (&mplayer->cond_status);
        mplayer->status = MPLAYER_IS_IDLE;
      }
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
          pthread_cond_signal (&mplayer->cond_status);
        mplayer->status = MPLAYER_IS_IDLE;
        pthread_mutex_unlock (&mplayer->mutex_status);

        pl_window_unmap (player->window);
      }
    }

    /*
     * Check language used by MPlayer. Only english is supported. A signal
     * is sent to the init as fast as possible. If --language is not found,
     * then MPlayer is in english. But if --language is found, then the
     * first language must be 'en' or 'all'.
     * --language-msg is provided by MPlayer >= r29363.
     *
     * FIXME: to find a way to detect when MPlayer is compiled for an other
     *        language with the LINGUAS environment variable.
     */
    else if (check_init)
    {
      const char *it;
      if (   (it = pl_strrstr (buffer, "--language-msg="))
          || (it = pl_strrstr (buffer, "--language=")))
      {
        it = strchr (it, '=') + 1;
        if (strncmp (it, "en", 2) &&
            strncmp (it, "all", 3))
        {
          start_ok = 0;
          pl_log (player, PLAYER_MSG_ERROR,
                  MODULE_NAME, "only english version of MPlayer is supported");
        }
      }
      else if (strstr (buffer, "-slave") && strstr (buffer, "-idle"))
      {
        pthread_mutex_lock (&mplayer->mutex_start);
        mplayer->start_ok = start_ok;
        pthread_cond_signal (&mplayer->cond_start);
        pthread_mutex_unlock (&mplayer->mutex_start);
        check_init = 0;
      }
    }
  }

  pl_window_unmap (player->window);

  pthread_mutex_lock (&mplayer->mutex_status);
  mplayer->status = MPLAYER_IS_DEAD;
  pthread_mutex_unlock (&mplayer->mutex_status);

  /* Unexpected error at the initialization. */
  if (check_init)
    pthread_cond_signal (&mplayer->cond_start);

  pthread_exit (NULL);
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

  mplayer = player->priv;

  if (!mplayer)
    return;

  if (!mplayer->fifo_in)
  {
    pl_log (player, PLAYER_MSG_CRITICAL, MODULE_NAME,
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
  {
    pl_log (player, PLAYER_MSG_WARNING,
            MODULE_NAME, "property (%i) unsupported by this version of MPlayer."
                         " Please upgrade to a newest build", property);
    return;
  }

  command = get_cmd (player, SLAVE_GET_PROPERTY, &state);
  if (!command || state != ITEM_ON)
    return;

  send_to_slave (player, SLAVE_CMD_PREFIX "%s %s", command, prop);
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

  mplayer = player->priv;

  if (get_mplayer_status (player) == MPLAYER_IS_DEAD)
    return NULL;

  prop = get_prop (player, property, &state);
  if (!prop || state != ITEM_ON)
    return NULL;

  snprintf (str, sizeof (str), "ANS_%s=", prop);

  pthread_mutex_lock (&mplayer->mutex_search);
  mplayer->search.property = strdup (str);
  mplayer->search.value = NULL;
  pthread_mutex_unlock (&mplayer->mutex_search);

  slave_get_property (player, property);

  /*
   * HACK: Old MPlayer versions need this hack to detect when a property
   *       is unavailable. An error message is returned by the command
   *       'loadfile' (without argument).
   *
   * NOTE: This hack is no longer necessary since MPlayer r26296.
   */
  send_to_slave (player, "loadfile");

  /* wait that the thread will found the value */
  sem_wait (&mplayer->sem);

  /* the search is ended */
  pthread_mutex_lock (&mplayer->mutex_search);
  ret = mplayer->search.value;
  mplayer->search.value = NULL;
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
    value = (int) rintf (pl_atof (result));
    PFREE (result);
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
    value = pl_atof (result);
    PFREE (result);
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

  if (get_mplayer_status (player) == MPLAYER_IS_DEAD)
    return;

  prop = get_prop (player, property, &state);
  if (!prop || state != ITEM_ON)
  {
    pl_log (player, PLAYER_MSG_WARNING,
            MODULE_NAME, "property (%i) unsupported by this version of MPlayer."
                         " Please upgrade to a newest build", property);
    return;
  }

  command = get_cmd (player, SLAVE_SET_PROPERTY, &state);
  if (!command || state != ITEM_ON)
    return;

  snprintf (cmd, sizeof (cmd), SLAVE_CMD_PREFIX "%s %s", command, prop);

  switch (property)
  {
  case PROPERTY_ANGLE:
  case PROPERTY_FRAMEDROPPING:
  case PROPERTY_LOOP:
  case PROPERTY_MUTE:
  case PROPERTY_OSDLEVEL:
  case PROPERTY_SUB:
  case PROPERTY_SUB_ALIGNMENT:
  case PROPERTY_SUB_VISIBILITY:
  case PROPERTY_SWITCH_AUDIO:
  case PROPERTY_VOLUME:
    send_to_slave (player, "%s %i", cmd, value.i_val);
    break;

  case PROPERTY_AUDIO_DELAY:
  case PROPERTY_SPEED:
  case PROPERTY_SUB_DELAY:
    send_to_slave (player, "%s %.2f", cmd, value.f_val);
    break;

  default:
    pl_log (player, PLAYER_MSG_ERROR,
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

  mplayer = player->priv;

  if (get_mplayer_status (player) == MPLAYER_IS_DEAD)
    return;

  command = get_cmd (player, cmd, &state_cmd);
  if (!command || state_cmd == ITEM_OFF)
  {
    pl_log (player, PLAYER_MSG_WARNING,
            MODULE_NAME, "command (%i) unsupported by this version of MPlayer. "
                         "Please upgrade to a newest build", cmd);
    return;
  }

  if (state_cmd == ITEM_HACK)
    pl_log (player, PLAYER_MSG_WARNING,
            MODULE_NAME, "[hack] slave command '%s'", command);

  switch (cmd)
  {
  case SLAVE_LOADFILE:
    if (state_cmd == ITEM_ON && value && value->s_val)
    {
      pthread_mutex_lock (&mplayer->mutex_status);
      mplayer->status = MPLAYER_IS_LOADING;
      send_to_slave (player, "%s \"%s\" %i", command, value->s_val, opt);
      pthread_cond_wait (&mplayer->cond_status, &mplayer->mutex_status);
      pthread_mutex_unlock (&mplayer->mutex_status);
    }
    break;

  case SLAVE_STOP:
    if (state_cmd == ITEM_HACK)
      /*
       * With very old versions of MPlayer where "stop" command is not
       * available, a playback can be stopped by trying to load an
       * unexistent file.
       */
      send_to_slave (player, "loadfile \"\"");
    else if (state_cmd == ITEM_ON)
      send_to_slave (player, command);

    sem_wait (&mplayer->sem);
    break;

  case SLAVE_OSD_SHOW_TEXT:
    if (state_cmd == ITEM_ON && value && value->s_val)
      send_to_slave (player, SLAVE_CMD_PREFIX "%s \"%s\" %i 0",
                             command, value->s_val, opt);
    break;

  /* command */
  case SLAVE_PAUSE:
  case SLAVE_QUIT:
    if (state_cmd == ITEM_ON)
      send_to_slave (player, command);
    break;

  /* command "string" */
  case SLAVE_SUB_LOAD:
    if (state_cmd == ITEM_ON && value && value->s_val)
      send_to_slave (player, "%s \"%s\"", command, value->s_val);
    break;

  /* PREFIX command string */
  case SLAVE_RADIO_SET_CHANNEL:
  case SLAVE_TV_SET_CHANNEL:
  case SLAVE_TV_SET_NORM:
    if (state_cmd == ITEM_ON && value && value->s_val)
      send_to_slave (player, SLAVE_CMD_PREFIX "%s %s", command, value->s_val);
    break;

  /* PREFIX command int */
  case SLAVE_DVDNAV:
  case SLAVE_RADIO_STEP_CHANNEL:
  case SLAVE_SUB_POS:
  case SLAVE_SWITCH_TITLE:
  case SLAVE_TV_STEP_CHANNEL:
    if (state_cmd == ITEM_ON && value)
      send_to_slave (player, SLAVE_CMD_PREFIX "%s %i", command, value->i_val);
    break;

  /* PREFIX command int int */
  case SLAVE_SEEK_CHAPTER:
  case SLAVE_SET_MOUSE_POS:
  case SLAVE_SUB_SCALE:
  case SLAVE_VOLUME:
    if (state_cmd == ITEM_ON && value)
      send_to_slave (player,
                     SLAVE_CMD_PREFIX "%s %i %i", command, value->i_val, opt);
    break;

  /* PREFIX command float */
  case SLAVE_SWITCH_RATIO:
    if (state_cmd == ITEM_ON && value)
      send_to_slave (player, SLAVE_CMD_PREFIX "%s %.2f", command, value->f_val);
    break;

  /* PREFIX command float int */
  case SLAVE_SEEK:
    if (state_cmd == ITEM_ON && value)
      send_to_slave (player,
                     SLAVE_CMD_PREFIX "%s %.3f %i", command, value->f_val, opt);
    break;

  default:
    pl_log (player, PLAYER_MSG_ERROR,
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
slave_cmd_float (player_t *player, slave_cmd_t cmd, float value)
{
  slave_value_t param;

  param.f_val = value;
  slave_action (player, cmd, &param, 0);
}

static inline void
slave_cmd_float_opt (player_t *player, slave_cmd_t cmd, float value, int opt)
{
  slave_value_t param;

  param.f_val = value;
  slave_action (player, cmd, &param, opt);
}

static inline void
slave_cmd_str (player_t *player, slave_cmd_t cmd, const char *str)
{
  slave_value_t param;

  param.s_val = str;
  slave_action (player, cmd, &param, 0);
}

static inline void
slave_cmd_str_opt (player_t *player, slave_cmd_t cmd, const char *str, int opt)
{
  slave_value_t param;

  param.s_val = str;
  slave_action (player, cmd, &param, opt);
}

/*****************************************************************************/
/*                                MRL's args                                 */
/*****************************************************************************/

static char *
uri_args_device (const char *device, size_t *size)
{
  char *dev;
  size_t length;

  if (!device || !size)
    return NULL;

  length = 1 + strlen (device);
  *size += length;
  dev = malloc (1 + length);
  if (dev)
    snprintf (dev, 1 + length, "/%s", device);

  return dev;
}

static char *
mp_resource_get_uri_local (const char *protocol,
                           mrl_resource_local_args_t *args)
{
  if (!args || !args->location || !protocol)
    return NULL;

  if (strstr (args->location, "://"))
  {
    if (strncmp (args->location, protocol, strlen (protocol)))
      return NULL;

    return strdup (args->location + strlen (protocol));
  }

  return strdup (args->location);
}

static char *
mp_resource_get_uri_cd (const char *protocol, mrl_resource_cd_args_t *args)
{
  char *uri;
  char *device = NULL;
  char track_start[4] = "";
  char track_end[8] = "";
  char speed[8] = "";
  size_t size;

  if (!args || !protocol)
    return NULL;

  size = strlen (protocol);

  if (args->track_start)
  {
    size += pl_count_nb_dec (args->track_start);
    snprintf (track_start, sizeof (track_start), "%i", args->track_start);
  }
  if (args->track_end >= args->track_start)
  {
    size += 1 + pl_count_nb_dec (args->track_end);
    snprintf (track_end, sizeof (track_end), "-%i", args->track_end);
  }
  if (args->speed)
  {
    size += 1 + pl_count_nb_dec (args->speed);
    snprintf (speed, sizeof (speed), ":%i", args->speed);
  }
  device = uri_args_device (args->device, &size);

  size++;
  uri = malloc (size);
  if (uri)
    snprintf (uri, size, "%s%s%s%s%s",
              protocol, track_start, track_end, speed, device ? device : "");

  PFREE (device);

  return uri;
}

static char *
mp_resource_get_uri_dvd (const char *protocol,
                         mrl_resource_videodisc_args_t *args)
{
  char *uri;
  char *device = NULL;
  char title_start[4] = "";
  char title_end[8] = "";
  size_t size;

  if (!args || !protocol)
    return NULL;

  size = strlen (protocol);

  if (args->title_start)
  {
    size += pl_count_nb_dec (args->title_start);
    snprintf (title_start, sizeof (title_start), "%i", args->title_start);
  }
  if (args->title_end > args->title_start)
  {
    size += 1 + pl_count_nb_dec (args->title_end);
    snprintf (title_end, sizeof (title_end), "-%i", args->title_end);
  }
  /*
   * NOTE: for dvd://, "/device" is handled by MPlayer >= r27226, and that
   *       is just ignored with older.
   */
  device = uri_args_device (args->device, &size);

  size++;
  uri = malloc (size);
  if (uri)
    snprintf (uri, size, "%s%s%s%s",
              protocol, title_start, title_end, device ? device : "");

  PFREE (device);

  return uri;
}

static char *
mp_resource_get_uri_vcd (const char *protocol,
                         mrl_resource_videodisc_args_t *args)
{
  char *uri;
  char *device = NULL;
  char track_start[4] = "";
  size_t size;

  if (!args || !protocol)
    return NULL;

  size = strlen (protocol);

  if (args->track_start)
  {
    size += pl_count_nb_dec (args->track_start);
    snprintf (track_start, sizeof (track_start), "%u", args->track_start);
  }
  device = uri_args_device (args->device, &size);

  size++;
  uri = malloc (size);
  if (uri)
    snprintf (uri, size, "%s%s%s", protocol, track_start, device ? device : "");

  PFREE (device);

  return uri;
}

static char *
mp_resource_get_uri_radio (const char *protocol, mrl_resource_tv_args_t *args)
{
  char *uri;
  size_t size;

  if (!args || !protocol)
    return NULL;

  size = strlen (protocol);

  if (args->channel)
    size += strlen (args->channel);

  size++;
  uri = malloc (size);
  if (uri)
    snprintf (uri, size, "%s%s", protocol, args->channel ? args->channel : "");

  return uri;
}

static char *
mp_resource_get_uri_tv (const char *protocol, mrl_resource_tv_args_t *args)
{
  char *uri;
  size_t size;

  if (!args || !protocol)
    return NULL;

  size = strlen (protocol);

  if (args->channel)
    size += strlen (args->channel);

  size += pl_count_nb_dec (args->input) + 2;
  uri = malloc (size);
  if (!uri)
    return NULL;

  if (!strcmp (protocol, "dvb://"))
    snprintf (uri, size, "%s%i@%s",
              protocol, args->input ? args->input : 1,
              args->channel ? args->channel : "");
  else
    snprintf (uri, size, "%s%s/%i",
              protocol, args->channel ? args->channel : "", args->input);

  return uri;
}

static char *
mp_resource_get_uri_network (const char *protocol,
                             mrl_resource_network_args_t *args)
{
  char *uri, *host_file;
  char at[256] = "";
  size_t size;

  if (!args || !args->url || !protocol)
    return NULL;

  size = strlen (protocol);

  if (strstr (args->url, protocol) == args->url)
    host_file = strdup (args->url + size);
  else
    host_file = strdup (args->url);

  if (!host_file)
    return NULL;

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
  if (uri)
    snprintf (uri, size, "%s%s%s", protocol, at, host_file);

  PFREE (host_file);

  return uri;
}

static char *
mp_resource_get_uri (mrl_t *mrl)
{
  static const char *const protocols[] = {
    /* Local Streams */
    [MRL_RESOURCE_FILE]     = "file://",

    /* Audio CD */
    [MRL_RESOURCE_CDDA]     = "cdda://",
    [MRL_RESOURCE_CDDB]     = "cddb://",

    /* Video discs */
    [MRL_RESOURCE_DVD]      = "dvd://",
    [MRL_RESOURCE_DVDNAV]   = "dvdnav://",
    [MRL_RESOURCE_VCD]      = "vcd://",

    /* Radio/Television */
    [MRL_RESOURCE_DVB]      = "dvb://",
    [MRL_RESOURCE_RADIO]    = "radio://",
    [MRL_RESOURCE_TV]       = "tv://",

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
    return mp_resource_get_uri_local (protocols[mrl->resource], mrl->priv);

  case MRL_RESOURCE_CDDA: /* cdda://track_start-track_end:speed/device */
  case MRL_RESOURCE_CDDB: /* cddb://track_start-track_end:speed/device */
    return mp_resource_get_uri_cd (protocols[mrl->resource], mrl->priv);

  case MRL_RESOURCE_DVD:    /* dvd://title_start-title_end/device */
  case MRL_RESOURCE_DVDNAV: /* dvdnav://title_start-title_end/device */
    return mp_resource_get_uri_dvd (protocols[mrl->resource], mrl->priv);

  case MRL_RESOURCE_VCD: /* vcd://track_start/device */
    return mp_resource_get_uri_vcd (protocols[mrl->resource], mrl->priv);

  case MRL_RESOURCE_RADIO: /* radio://channel/capture */
    return mp_resource_get_uri_radio (protocols[mrl->resource], mrl->priv);

  case MRL_RESOURCE_DVB: /* dvb://input@channel */
  case MRL_RESOURCE_TV: /* tv://channel/input */
    return mp_resource_get_uri_tv (protocols[mrl->resource], mrl->priv);

  case MRL_RESOURCE_FTP:  /* ftp://username:password@url   */
  case MRL_RESOURCE_HTTP: /* http://username:password@url  */
  case MRL_RESOURCE_MMS:  /* mms://username:password@url   */
  case MRL_RESOURCE_RTP:  /* rtp://username:password@url   */
  case MRL_RESOURCE_RTSP: /* rtsp://username:password@url  */
  case MRL_RESOURCE_SMB:  /* smb://username:password@url   */
  case MRL_RESOURCE_UDP:  /* udp://username:password@url   */
  case MRL_RESOURCE_UNSV: /* unsv://username:password@url  */
    return mp_resource_get_uri_network (protocols[mrl->resource], mrl->priv);

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
  /* device, driver, width, height, fps, output_format, norm */
  case MRL_RESOURCE_TV:
  {
    mrl_resource_tv_args_t *args = mrl->priv;

    if (args->norm)
      slave_cmd_str (player, SLAVE_TV_SET_NORM, args->norm);
    break;
  }

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
mp_identify_metadata_clip (mrl_t *mrl,
                           const char *buffer, mp_identify_clip_t *clip)
{
  char *it, *value;
  char str[FIFO_BUFFER];
  mrl_metadata_t *meta = mrl->meta;

  if (!strstr (buffer, "ID_CLIP_INFO"))
    return 0;

  /* no new metadata */
  if (strstr (buffer, "ID_CLIP_INFO_N=") == buffer)
    return 0;

  snprintf (str, sizeof (str), "ID_CLIP_INFO_NAME%i=", clip->cnt);
  it = strstr (buffer, str);
  if (it == buffer)
  {
    if (!strcasecmp (parse_field (it), "title"))
      clip->property = PROPERTY_METADATA_TITLE;
    else if (!strcasecmp (parse_field (it), "name"))
      clip->property = PROPERTY_METADATA_NAME;
    else if (!strcasecmp (parse_field (it), "artist"))
      clip->property = PROPERTY_METADATA_ARTIST;
    else if (!strcasecmp (parse_field (it), "author"))
      clip->property = PROPERTY_METADATA_AUTHOR;
    else if (!strcasecmp (parse_field (it), "genre"))
      clip->property = PROPERTY_METADATA_GENRE;
    else if (!strcasecmp (parse_field (it), "album"))
      clip->property = PROPERTY_METADATA_ALBUM;
    else if (!strcasecmp (parse_field (it), "year"))
      clip->property = PROPERTY_METADATA_YEAR;
    else if (!strcasecmp (parse_field (it), "track"))
      clip->property = PROPERTY_METADATA_TRACK;
    else if (!strcasecmp (parse_field (it), "comment"))
      clip->property = PROPERTY_METADATA_COMMENT;
    else
      clip->property = PROPERTY_UNKNOWN;

    return 1;
  }

  snprintf (str, sizeof (str), "ID_CLIP_INFO_VALUE%i=", clip->cnt);
  it = strstr (buffer, str);
  if (it != buffer)
    return 0;

  value = parse_field (it);

  switch (clip->property)
  {
  case PROPERTY_METADATA_NAME:
  case PROPERTY_METADATA_TITLE:
    PFREE (meta->title);
    meta->title = *value ? strdup (value) : NULL;
    break;

  case PROPERTY_METADATA_ARTIST:
  case PROPERTY_METADATA_AUTHOR:
    PFREE (meta->artist);
    meta->artist = *value ? strdup (value) : NULL;
    break;

  case PROPERTY_METADATA_GENRE:
    PFREE (meta->genre);
    meta->genre = *value ? strdup (value) : NULL;
    break;

  case PROPERTY_METADATA_ALBUM:
    PFREE (meta->album);
    meta->album = *value ? strdup (value) : NULL;
    break;

  case PROPERTY_METADATA_YEAR:
    PFREE (meta->year);
    meta->year = *value ? strdup (value) : NULL;
    break;

  case PROPERTY_METADATA_TRACK:
    PFREE (meta->track);
    meta->track = *value ? strdup (value) : NULL;
    break;

  case PROPERTY_METADATA_COMMENT:
    PFREE (meta->comment);
    meta->comment = *value ? strdup (value) : NULL;
    break;

  default:
    break;
  }

  clip->cnt++;
  clip->property = PROPERTY_UNKNOWN;
  return 1;
}

static int
mp_identify_metadata_cd (mrl_t *mrl, const char *buffer)
{
  int cnt = 0, res;
  char *it;
  char str[FIFO_BUFFER];
  mrl_metadata_t *meta = mrl->meta;
  mrl_metadata_cd_t *cd = mrl->meta->priv;

  if (!cd || !strstr (buffer, "ID_CDD"))
    return 0;

  /* CDDA track length */

  it = strstr (buffer, "ID_CDDA_TRACKS=");
  if (it == buffer)
  {
    cd->tracks = atoi (parse_field (it));
    return 1;
  }

  res = sscanf (buffer, "ID_CDDA_TRACK_%i_%s", &cnt, str);
  if (res && res != EOF && cnt)
  {
    mrl_metadata_cd_track_t *track = mrl_metadata_cd_get_track (cd, cnt);

    if (!track)
      return 1;

    if (strstr (str, "MSF") == str)
      track->length = parse_msf (parse_field ((char *) buffer));
    return 1;
  }

  /*
   * NOTE: This part needs at least MPlayer >= r27207 to identify CDDB, with
   *       older version it is just ignored. Sometimes not all track names
   *       are retrieved, it seems to be a bug in MPlayer. The file from
   *       FreeDB is not always fully downloaded for an unknown reason.
   *       You can check the result in ~/.cddb/discid.
   */

  /* CDDB tracks */

  it = strstr (buffer, "ID_CDDB_INFO_TRACKS=");
  if (it == buffer)
    return 1;

  res = sscanf (buffer, "ID_CDDB_INFO_TRACK_%i_%s", &cnt, str);
  if (res && res != EOF && cnt)
  {
    mrl_metadata_cd_track_t *track = mrl_metadata_cd_get_track (cd, cnt);

    if (!track)
      return 1;

    if (strstr (str, "NAME") == str)
      track->name = strdup (parse_field ((char *) buffer));
    return 1;
  }

  /* CDDB global infos */

  it = strstr (buffer, "ID_CDDB_DISCID=");
  if (it == buffer)
  {
    cd->discid = (uint32_t) strtol (parse_field (it), NULL, 16);
    return 1;
  }

  it = strstr (buffer, "ID_CDDB_INFO_ARTIST=");
  if (it == buffer)
  {
    PFREE (meta->artist);
    meta->artist = strdup (parse_field (it));
    return 1;
  }

  it = strstr (buffer, "ID_CDDB_INFO_ALBUM=");
  if (it == buffer)
  {
    PFREE (meta->album);
    meta->album = strdup (parse_field (it));
    return 1;
  }

  it = strstr (buffer, "ID_CDDB_INFO_GENRE=");
  if (it == buffer)
  {
    PFREE (meta->genre);
    meta->genre = strdup (parse_field (it));
    return 1;
  }

  return 0;
}

static int
mp_identify_metadata_dvd (mrl_t *mrl, const char *buffer)
{
  int cnt = 0, res;
  char *it;
  char val[FIFO_BUFFER];
  mrl_metadata_dvd_t *dvd = mrl->meta->priv;

  if (!dvd || !strstr (buffer, "ID_DVD"))
    return 0;

  it = strstr (buffer, "ID_DVD_VOLUME_ID=");
  if (it == buffer)
  {
    PFREE (dvd->volumeid);
    dvd->volumeid = strdup (parse_field (it));
    return 1;
  }

  it = strstr (buffer, "ID_DVD_TITLES=");
  if (it == buffer)
  {
    dvd->titles = atoi (parse_field (it));
    return 1;
  }

  res = sscanf (buffer, "ID_DVD_TITLE_%i_%s", &cnt, val);
  if (res && res != EOF && cnt)
  {
    mrl_metadata_dvd_title_t *title = mrl_metadata_dvd_get_title (dvd, cnt);

    if (!title)
      return 1;

    if (strstr (val, "CHAPTERS") == val)
      title->chapters = atoi (parse_field (val));
    else if (strstr (val, "ANGLES") == val)
      title->angles = atoi (parse_field (val));
    else if (strstr (val, "LENGTH") == val)
      title->length = (uint32_t) (pl_atof (parse_field (val)) * 1000.0);
    return 1;
  }

  return 0;
}

static int
mp_identify_metadata_sub (mrl_t *mrl, const char *buffer)
{
  int cnt = 0, res;
  char *it;
  char val[FIFO_BUFFER];
  mrl_metadata_t *meta = mrl->meta;

  it = strstr (buffer, "ID_SUBTITLE_ID=");
  if (it == buffer)
  {
    int id = atoi (parse_field (it));
    mrl_metadata_sub_t *sub = mrl_metadata_sub_get (&meta->subs, id);

    if (!sub)
      return 1;

    sub->id = id;
    return 1;
  }

  res = sscanf (buffer, "ID_SID_%i_%s", &cnt, val);
  if (res && res != EOF)
  {
    mrl_metadata_sub_t *sub = mrl_metadata_sub_get (&meta->subs, cnt);

    if (!sub)
      return 1;

    if (strstr (val, "NAME") == val)
    {
      PFREE (sub->name);
      sub->name = strdup (parse_field (val));
    }
    else if (strstr (val, "LANG") == val)
    {
      PFREE (sub->lang);
      sub->lang = strdup (parse_field (val));
    }
    return 1;
  }

  return 0;
}

static int
mp_identify_metadata_audio (mrl_t *mrl, const char *buffer)
{
  int cnt = 0, res;
  char *it;
  char val[FIFO_BUFFER];
  mrl_metadata_t *meta = mrl->meta;

  it = strstr (buffer, "ID_AUDIO_ID=");
  if (it == buffer)
  {
    int id = atoi (parse_field (it));
    mrl_metadata_audio_t *audio =
      mrl_metadata_audio_get (&meta->audio_streams, id);

    if (!audio)
      return 1;

    audio->id = id;
    return 1;
  }

  res = sscanf (buffer, "ID_AID_%i_%s", &cnt, val);
  if (res && res != EOF)
  {
    mrl_metadata_audio_t *audio =
      mrl_metadata_audio_get (&meta->audio_streams, cnt);

    if (!audio)
      return 1;

    if (strstr (val, "NAME") == val)
    {
      PFREE (audio->name);
      audio->name = strdup (parse_field (val));
    }
    else if (strstr (val, "LANG") == val)
    {
      PFREE (audio->lang);
      audio->lang = strdup (parse_field (val));
    }
    return 1;
  }

  return 0;
}

static int
mp_identify_metadata (mrl_t *mrl, const char *buffer, mp_identify_clip_t *clip)
{
  int ret = 0;

  if (!mrl->meta)
    return 0;

  switch (mrl->resource)
  {
  case MRL_RESOURCE_CDDA:
  case MRL_RESOURCE_CDDB:
    ret = mp_identify_metadata_cd (mrl, buffer);
    break;

  case MRL_RESOURCE_DVD:
  case MRL_RESOURCE_DVDNAV:
    ret = mp_identify_metadata_dvd (mrl, buffer);
    break;

  default:
    ret = mp_identify_metadata_clip (mrl, buffer, clip);
    break;
  }

  if (!ret)
    ret = mp_identify_metadata_sub (mrl, buffer);
  if (!ret)
    ret = mp_identify_metadata_audio (mrl, buffer);

  return ret;
}

static int
mp_identify_audio (mrl_t *mrl, const char *buffer)
{
  char *it;
  mrl_properties_audio_t *audio;

  if (!mrl->prop || !strstr (buffer, "ID_AUDIO"))
    return 0;

  if (!mrl->prop->audio)
    mrl->prop->audio = mrl_properties_audio_new ();

  audio = mrl->prop->audio;

  it = strstr (buffer, "ID_AUDIO_CODEC=");
  if (it == buffer)
  {
    PFREE (audio->codec);
    audio->codec = strdup (parse_field (it));
    return 1;
  }

  it = strstr (buffer, "ID_AUDIO_BITRATE=");
  if (it == buffer)
  {
    audio->bitrate = atoi (parse_field (it));
    return 1;
  }

  it = strstr (buffer, "ID_AUDIO_NCH=");
  if (it == buffer)
  {
    audio->channels = atoi (parse_field (it));
    return 1;
  }

  it = strstr (buffer, "ID_AUDIO_RATE=");
  if (it == buffer)
  {
    audio->samplerate = atoi (parse_field (it));
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

  if (!mrl->prop || !strstr (buffer, "ID_VIDEO"))
    return 0;

  if (!mrl->prop->video)
    mrl->prop->video = mrl_properties_video_new ();

  video = mrl->prop->video;

  it = strstr (buffer, "ID_VIDEO_CODEC=");
  if (it == buffer)
  {
    PFREE (video->codec);
    video->codec = strdup (parse_field (it));
    return 1;
  }

  it = strstr (buffer, "ID_VIDEO_BITRATE=");
  if (it == buffer)
  {
    video->bitrate = atoi (parse_field (it));
    return 1;
  }

  it = strstr (buffer, "ID_VIDEO_WIDTH=");
  if (it == buffer)
  {
    video->width = atoi (parse_field (it));
    return 1;
  }

  it = strstr (buffer, "ID_VIDEO_HEIGHT=");
  if (it == buffer)
  {
    video->height = atoi (parse_field (it));
    return 1;
  }

  it = strstr (buffer, "ID_VIDEO_ASPECT=");
  if (it == buffer)
  {
    video->aspect = (uint32_t) (pl_atof (parse_field (it))
                                * PLAYER_VIDEO_ASPECT_RATIO_MULT);
    return 1;
  }

  it = strstr (buffer, "ID_VIDEO_FPS=");
  if (it == buffer)
  {
    val = pl_atof (parse_field (it));
    video->frameduration =
      (uint32_t) (val ? PLAYER_VIDEO_FRAMEDURATION_RATIO_DIV / val : 0);
    return 1;
  }

  return 0;
}

static int
mp_identify_properties (mrl_t *mrl, const char *buffer)
{
  char *it;

  if (!mrl->prop)
    return 0;

  /*
   * ID_LENGTH is not usable with all resources. For CDDA/CDDB the length is
   * calculated by the MSF sum of each track; for DVD/DVDNAV, by the sum of
   * each title.
   */
  switch (mrl->resource)
  {
  case MRL_RESOURCE_CDDA:
  case MRL_RESOURCE_CDDB:
    it = strstr (buffer, "ID_CDDA_TRACKS=");
    if (it == buffer)
    {
      mrl->prop->length = 0;
      return 1;
    }

    it = strstr (buffer, "ID_CDDA_TRACK_");
    if (it == buffer && strstr (buffer, "_MSF="))
    {
      mrl->prop->length += (uint32_t) parse_msf (parse_field (it));
      return 1;
    }
    break;

  case MRL_RESOURCE_DVD:
  case MRL_RESOURCE_DVDNAV:
    it = strstr (buffer, "ID_DVD_TITLES=");
    if (it == buffer)
    {
      mrl->prop->length = 0;
      return 1;
    }

    it = strstr (buffer, "ID_DVD_TITLE_");
    if (it == buffer && strstr (buffer, "_LENGTH="))
    {
      mrl->prop->length += (uint32_t) (pl_atof (parse_field (it)) * 1000.0);
      return 1;
    }
    break;

  default:
    it = strstr (buffer, "ID_LENGTH=");
    if (it == buffer)
    {
      mrl->prop->length = (uint32_t) (pl_atof (parse_field (it)) * 1000.0);
      return 1;
    }
    break;
  }

  it = strstr (buffer, "ID_SEEKABLE=");
  if (it == buffer)
  {
    mrl->prop->seekable = atoi (parse_field (it));
    return 1;
  }

  return 0;
}

static void
mp_identify (player_t *player, mrl_t *mrl, int flags)
{
  int mp_pipe[2];
  pid_t pid;
  char *uri = NULL;

  if (!player || !mrl)
    return;

  uri = mp_resource_get_uri (mrl);
  if (!uri)
    return;

  if (pipe (mp_pipe))
  {
    PFREE (uri);
    return;
  }

  pid = fork ();

  switch (pid)
  {
  /* the son (a new hope) */
  case 0:
  {
    char *params[32];
    int pp = 0;

    close (mp_pipe[0]);

    dup2 (mp_pipe[1], STDERR_FILENO);
    dup2 (mp_pipe[1], STDOUT_FILENO);
    close (mp_pipe[1]);

    params[pp++] = MPLAYER_NAME;
    params[pp++] = "-quiet";
    params[pp++] = "-vo";
    params[pp++] = "null";
    params[pp++] = "-ao";
    params[pp++] = "null";
    params[pp++] = "-nolirc";
    params[pp++] = "-nojoystick";
    params[pp++] = "-noconsolecontrols";
    params[pp++] = "-noar";
    params[pp++] = "-nomouseinput";
    params[pp++] = "-endpos";
    params[pp++] = "0";
    params[pp++] = uri;
    params[pp++] = "-msglevel";
    params[pp++] = "all=0:global=4:identify=6";
    params[pp] = NULL;

    execvp (MPLAYER_NAME, params);
    break;
  }

  case -1:
    break;

  /* I'm your father */
  default:
  {
    char buffer[FIFO_BUFFER];
    FILE *mp_fifo;
    mp_identify_clip_t clip = {
      .cnt      = 0,
      .property = PROPERTY_UNKNOWN
    };

    close (mp_pipe[1]);

    mp_fifo = fdopen (mp_pipe[0], "r");

    while (fgets (buffer, FIFO_BUFFER, mp_fifo))
    {
      *(buffer + strlen (buffer) - 1) = '\0';
      pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "[identify] %s", buffer);

      if (flags & IDENTIFY_VIDEO)
        mp_identify_video (mrl, buffer);

      if (flags & IDENTIFY_AUDIO)
        mp_identify_audio (mrl, buffer);

      if (flags & IDENTIFY_METADATA)
        mp_identify_metadata (mrl, buffer, &clip);

      if (flags & IDENTIFY_PROPERTIES)
        mp_identify_properties (mrl, buffer);
    }

    /* wait the death of MPlayer */
    waitpid (pid, NULL, 0);
    PFREE (uri);
    close (mp_pipe[0]);
    fclose (mp_fifo);
  }
  }
}

static void
mp_mrl_retrieve (player_t *player, mrl_t *mrl)
{
  int flags = 0;

  if (mrl->prop)
  {
    flags |= IDENTIFY_AUDIO | IDENTIFY_VIDEO | IDENTIFY_PROPERTIES;

    if (mrl->resource == MRL_RESOURCE_FILE)
    {
      mrl_resource_local_args_t *args = mrl->priv;
      if (args && args->location)
      {
        const char *location = args->location;

        if (strstr (location, "file://") == location)
          location += 7;

        mrl->prop->size = pl_file_size (location);
      }
    }
  }

  if (mrl->meta)
    flags |= IDENTIFY_METADATA;

  mp_identify (player, mrl, flags);
}

/*****************************************************************************/
/*                            Pre-Init functions                             */
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
    PFREE (opts);
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

  PFREE (list);
}

static item_opt_t *
mp_prop_get_option (char *it_min, char *it_max)
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

  opt = PCALLOC (item_opt_t, 1);
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
mp_check_get_vars (player_t *player, checklist_t check,
                   int *nb, item_list_t **list, const char **what)
{
  mplayer_t *mplayer;

  if (!player)
    return 0;

  mplayer = player->priv;
  if (!mplayer)
    return 0;

  switch (check)
  {
  case CHECKLIST_COMMANDS:
    if (nb)
      *nb = g_slave_cmds_nb;
    if (list)
      *list = mplayer->slave_cmds;
    if (what)
      *what = "command";
    break;

  case CHECKLIST_PROPERTIES:
    if (nb)
      *nb = g_slave_props_nb;
    if (list)
      *list = mplayer->slave_props;
    if (what)
      *what = "property";
    break;

  default:
    return 0;
  }

  return 1;
}

static void
mp_check_list (player_t *player, checklist_t check)
{
  item_list_t *list;
  int i, nb = 0;
  const char *what;

  if (!mp_check_get_vars (player, check, &nb, &list, &what))
    return;

  /* check items list */
  for (i = 1; i < nb; i++)
  {
    int state_libplayer;
    item_state_t *state_mp = &list[i].state_mp;
    const int *state_lib = &list[i].state_lib;
    const char *str = list[i].str;
    item_opt_t *opt = list[i].opt;

    if (!str || !state_mp || !state_lib)
      continue;

    state_libplayer = *state_lib & ALL_ITEM_STATES;

    if (strchr (str, '/'))
      continue;

    if (state_libplayer == ITEM_ON && *state_mp == ITEM_OFF)
    {
      pl_log (player, PLAYER_MSG_WARNING,
              MODULE_NAME, "slave %s '%s' is needed and not supported by your "
                           "version of MPlayer, all actions with this item "
                           "will be ignored", what, str);
    }
    else if (state_libplayer == ITEM_HACK && *state_mp == ITEM_OFF)
    {
      pl_log (player, PLAYER_MSG_WARNING,
              MODULE_NAME, "slave %s '%s' is needed and not supported by your "
                           "version of MPlayer and libplayer, then a hack is "
                           "used", what, str);
    }
    else if (state_libplayer == ALL_ITEM_STATES && *state_mp == ITEM_OFF)
    {
      pl_log (player, PLAYER_MSG_WARNING,
              MODULE_NAME, "slave %s '%s' is needed and not supported by your "
                           "version of MPlayer, then a hack is used",
                           what, str);
    }
    else if (state_libplayer == ITEM_HACK && *state_mp == ITEM_ON)
    {
      pl_log (player, PLAYER_MSG_WARNING,
              MODULE_NAME, "slave %s '%s' is supported by your version of "
                           "MPlayer but not by libplayer, then a hack is "
                           "used", what, str);
    }
    else if ((state_libplayer == ITEM_ON && *state_mp == ITEM_ON) ||
             (state_libplayer == ALL_ITEM_STATES && *state_mp == ITEM_ON))
    {
      pl_log (player, PLAYER_MSG_VERBOSE,
              MODULE_NAME, "slave %s '%s' is supported by your version of "
                           "MPlayer", what, str);

      if (opt)
        pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME,
                " *** conf:%i min:%i max:%i", opt->conf, opt->min, opt->max);
    }
  }
}

static int
mp_check_compatibility (player_t *player, checklist_t check)
{
  int i, nb = 0;
  int mp_pipe[2];
  pid_t pid;
  item_list_t *list = NULL;
  const char *str;
  item_state_t *state_mp;

  if (!mp_check_get_vars (player, check, &nb, &list, NULL))
    return 0;

  if (!list)
    return 0;

  if (pipe (mp_pipe))
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
    close (mp_pipe[1]);

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
    break;
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
      *(buffer + strlen (buffer) - 1) = '\0';
      pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "[check] %s", buffer);

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
            list[i].opt = mp_prop_get_option (it_min, it_max);
          break;
        }
      }
    }

    waitpid (pid, NULL, 0);
    close (mp_pipe[0]);
    fclose (mp_fifo);
  }
  }

  if (pl_log_test (player, PLAYER_MSG_WARNING))
    mp_check_list (player, check);

  return 1;
}

static int
executable_is_available (player_t *player, const char *bin)
{
  char *p, *fp, *env, *save_p;
  char prog[PATH_BUFFER];

  env = getenv ("PATH");

  if (!env)
    return 0;

  fp = strdup (env);
  p = fp;

  if (!fp)
    return 0;

  for (p = strtok_r (p, ":", &save_p); p; p = strtok_r (NULL, ":", &save_p))
  {
    snprintf (prog, sizeof (prog), "%s/%s", p, bin);
    if (!access (prog, X_OK))
    {
      PFREE (fp);
      return 1;
    }
  }

  pl_log (player, PLAYER_MSG_ERROR,
          MODULE_NAME, "%s executable not found in the PATH", bin);

  PFREE (fp);
  return 0;
}

static int
mp_preinit_vo (player_t *player, uint32_t *winid)
{
  int ret = 0;

  *winid = 0;

  /* The video out is sent in our X11 window, winid is used for -wid arg. */
  switch (player->vo)
  {
  case PLAYER_VO_NULL:
  case PLAYER_VO_FB:
  case PLAYER_VO_DIRECTFB:
    break;

  case PLAYER_VO_X11:
  case PLAYER_VO_XV:
  case PLAYER_VO_GL:
  case PLAYER_VO_VDPAU:
  case PLAYER_VO_VAAPI:
#ifndef HAVE_WIN_XCB
    pl_log (player, PLAYER_MSG_ERROR,
            MODULE_NAME, "libplayer is not compiled with X11 support");
    return -1;
#endif /* !HAVE_WIN_XCB */

  case PLAYER_VO_AUTO:
#ifdef HAVE_WIN_XCB
    ret = pl_window_init (player->window);
    if (player->vo != PLAYER_VO_AUTO && !ret)
    {
      pl_log (player, PLAYER_MSG_ERROR,
              MODULE_NAME, "initialization for X has failed");
      return -1;
    }
    *winid = pl_window_winid_get (player->window);
    break;
#else /* HAVE_WIN_XCB */
    pl_log (player, PLAYER_MSG_ERROR, MODULE_NAME,
            "auto-detection for window is not enabled without X11 support");
    return -1;
#endif /* !HAVE_WIN_XCB */

  default:
    pl_log (player, PLAYER_MSG_ERROR,
            MODULE_NAME, "unsupported video out (%i)", player->vo);
    return -1;
  }

  return ret;
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
 *   void  slave_cmd_float          (player_t, slave_cmd_t,      float)
 *   void  slave_cmd_float_opt      (player_t, slave_cmd_t,      float,  int)
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
  mplayer_t *mplayer = NULL;
  char winid[32];
  uint32_t winid_l = 0;
  int use_x11 = 0;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "init");

  if (!player)
    return PLAYER_INIT_ERROR;

  mplayer = player->priv;

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

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "check MPlayer compatibility");

  if (!mp_check_compatibility (player, CHECKLIST_COMMANDS))
    return PLAYER_INIT_ERROR;

  if (!mp_check_compatibility (player, CHECKLIST_PROPERTIES))
    return PLAYER_INIT_ERROR;

  use_x11 = mp_preinit_vo (player, &winid_l);
  if (use_x11 < 0)
    return PLAYER_INIT_ERROR;

  snprintf (winid, sizeof (winid), "%u", winid_l);

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
    close (mplayer->pipe_in[0]);

    dup2 (mplayer->pipe_out[1], STDERR_FILENO);
    dup2 (mplayer->pipe_out[1], STDOUT_FILENO);
    close (mplayer->pipe_out[1]);

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
    params[pp++] = "-mouse-movements";
    params[pp++] = "-nomouseinput";
    params[pp++] = "-noar";
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

    case PLAYER_VO_DIRECTFB:
      params[pp++] = "-vo";
      params[pp++] = "directfb:double";
      break;

#ifdef HAVE_WIN_XCB
    case PLAYER_VO_VDPAU:
    {
      int caps;
      char vc[256] = { 0 };

      params[pp++] = "-vo";
      params[pp++] = "vdpau,xv,x11";

      caps = pl_window_vdpau_caps_get (player->window);
      if (!caps)
        break;

      params[pp++] = "-vc";
      if (caps & (WIN_VDPAU_MPEG1 | WIN_VDPAU_MPEG2))
        strcat (vc, "ffmpeg12vdpau,");
      if (caps & WIN_VDPAU_H264)
        strcat (vc, "ffh264vdpau,");
      if (caps & WIN_VDPAU_VC1)
        strcat (vc, "ffvc1vdpau,ffwmv3vdpau,");
      if (caps & (WIN_VDPAU_MPEG4P2 | WIN_VDPAU_DIVX4 | WIN_VDPAU_DIVX5))
        strcat (vc, "ffodivxvdpau,");
      params[pp++] = vc;

      break;
    }
#endif /* HAVE_WIN_XCB */

    case PLAYER_VO_VAAPI:
      params[pp++] = "-vo";
      params[pp++] = "vaapi";
      params[pp++] = "-va";
      params[pp++] = "vaapi";
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
    else
      params[pp++] = "-nofixed-vo";

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

    case PLAYER_AO_PULSE:
      params[pp++] = "-ao";
      params[pp++] = "pulse";
      break;

    case PLAYER_AO_AUTO:
    default:
      break;
    }

    /* select expected video decoding quality */
    switch (player->quality)
    {
    case PLAYER_QUALITY_LOW:
      params[pp++] = "-vfm";
      params[pp++] = "ffmpeg";
      params[pp++] = "-lavdopts";
      params[pp++] = "fast:skiploopfilter=all";
      break;

    case PLAYER_QUALITY_LOWEST:
      params[pp++] = "-vfm";
      params[pp++] = "ffmpeg";
      params[pp++] = "-lavdopts";
      params[pp++] = "lowres=1:fast:skiploopfilter=all";
      break;

    default:
      break;
    }

    params[pp] = NULL;

    execvp (MPLAYER_NAME, params);
    break;
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

    pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "MPlayer child loaded");

    mplayer->status = MPLAYER_IS_IDLE;

    pthread_attr_init (&attr);
    pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_JOINABLE);

    pthread_mutex_lock (&mplayer->mutex_start);
    if (!pthread_create (&mplayer->th_fifo, &attr, thread_fifo, player))
    {
      int start_ok;

      pthread_cond_wait (&mplayer->cond_start, &mplayer->mutex_start);
      start_ok = mplayer->start_ok;
      pthread_mutex_unlock (&mplayer->mutex_start);

      pthread_attr_destroy (&attr);

      if (!start_ok)
      {
        pl_log (player, PLAYER_MSG_ERROR,
                MODULE_NAME, "error during MPlayer initialization");
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

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "uninit");

  if (!player)
    return;

  mplayer = player->priv;

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

    pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "MPlayer child terminated");
  }

  pl_window_uninit (player->window);

  item_list_free (mplayer->slave_cmds, g_slave_cmds_nb);
  item_list_free (mplayer->slave_props, g_slave_props_nb);

  pthread_cond_destroy (&mplayer->cond_start);
  pthread_cond_destroy (&mplayer->cond_status);
  pthread_mutex_destroy (&mplayer->mutex_search);
  pthread_mutex_destroy (&mplayer->mutex_status);
  pthread_mutex_destroy (&mplayer->mutex_verbosity);
  pthread_mutex_destroy (&mplayer->mutex_start);
  sem_destroy (&mplayer->sem);

  PFREE (mplayer);
}

static void
mplayer_set_verbosity (player_t *player, player_verbosity_level_t level)
{
  mplayer_t *mplayer;
  int verbosity = -1;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "set_verbosity");

  if (!player)
    return;

  mplayer = player->priv;

  if (!mplayer)
    return;

  switch (level)
  {
  case PLAYER_MSG_VERBOSE:
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

/*
 * NOTE: mplayer -identify returns always all informations (properties and
 *       metadata).
 *
 * mplayer_mrl_retrieve_properties() and mplayer_mrl_retrieve_metadata() will
 * retrieve all informations in order to avoid executing twice MPlayer with
 * an identical output.
 */

static void
mplayer_mrl_retrieve_properties (player_t *player, mrl_t *mrl)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "mrl_retrieve_properties");

  if (!player || !mrl || !mrl->prop)
    return;

  if (!mrl->meta)
    mrl->meta = mrl_metadata_new (mrl->resource);

  mp_mrl_retrieve (player, mrl);
}

static void
mplayer_mrl_retrieve_metadata (player_t *player, mrl_t *mrl)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "mrl_retrieve_metadata");

  if (!player || !mrl || !mrl->meta)
    return;

  if (!mrl->prop)
    mrl->prop = mrl_properties_new ();

  mp_mrl_retrieve (player, mrl);
}

static void
mplayer_mrl_video_snapshot (player_t *player, mrl_t *mrl,
                            int pos, mrl_snapshot_t t, const char *dst)
{
  pid_t pid;
  char *uri = NULL;
  char name[32] = SNAPSHOT_FILE;
  char tmp[] = SNAPSHOT_TMP;
  char vo[PATH_BUFFER], file[PATH_BUFFER];

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "mrl_video_snapshot");

  if (!player || !mrl)
    return;

  uri = mp_resource_get_uri (mrl);
  if (!uri)
    return;

  switch (t)
  {
  case MRL_SNAPSHOT_JPG:
    snprintf (vo, sizeof (vo), "jpeg:outdir=");
    strcat (name, ".jpg");
    break;

  case MRL_SNAPSHOT_PNG: /* outdir only supported with MPlayer >= r27650 */
    snprintf (vo, sizeof (vo), "png:z=2:outdir=");
    strcat (name, ".png");
    break;

  case MRL_SNAPSHOT_PPM:
    snprintf (vo, sizeof (vo), "pnm:ppm:outdir=");
    strcat (name, ".ppm");
    break;

  default:
    pl_log (player, PLAYER_MSG_WARNING,
            MODULE_NAME, "unsupported snapshot type (%i)", t);
    PFREE (uri);
    return;
  }

  if (!mkdtemp (tmp))
  {
    pl_log (player, PLAYER_MSG_ERROR,
            MODULE_NAME, "unable to create temporary directory (%s)", tmp);
    PFREE (uri);
    return;
  }

  strcat (vo, tmp);
  snprintf (file, sizeof (file), "%s/%s", tmp, name);

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "temporary directory for snapshot: %s", tmp);

  pid = fork ();

  switch (pid)
  {
  /* the son (a new hope) */
  case 0:
  {
    char *params[32];
    char ss[32];
    int pp = 0;
    int fd;

    fd = open ("/dev/null", O_WRONLY);
    dup2 (fd, STDOUT_FILENO);
    dup2 (fd, STDERR_FILENO);
    close (fd);

    params[pp++] = MPLAYER_NAME;
    params[pp++] = "-nocache";
    params[pp++] = "-quiet";
    params[pp++] = "-msglevel";
    params[pp++] = "all=0";
    params[pp++] = "-nolirc";
    params[pp++] = "-nojoystick";
    params[pp++] = "-noconsolecontrols";
    params[pp++] = "-noar";
    params[pp++] = "-nomouseinput";
    params[pp++] = "-nosound";
    params[pp++] = "-noautosub";
    params[pp++] = "-osdlevel";
    params[pp++] = "0";

    params[pp++] = "-vo";
    params[pp++] = vo;

    params[pp++] = "-ao";
    params[pp++] = "null";

    snprintf (ss, sizeof (ss), "%i", pos);
    params[pp++] = "-ss";
    params[pp++] = ss;

    params[pp++] = "-frames";
    params[pp++] = "1";
    params[pp++] = uri;
    params[pp] = NULL;

    execvp (MPLAYER_NAME, params);
    break;
  }

  case -1:
    break;

  /* I'm your father */
  default:
  {
    /* wait the death of MPlayer */
    waitpid (pid, NULL, 0);
    PFREE (uri);

    if (pl_file_exists (file))
    {
      /* use the current directory? */
      if (!dst)
        dst = name;

      if (dst)
      {
        int res = pl_copy_file (file, dst);
        if (!res)
          pl_log (player, PLAYER_MSG_INFO,
                  MODULE_NAME, "move %s to %s", file, dst);
        else
          pl_log (player, PLAYER_MSG_ERROR,
                  MODULE_NAME, "unable to move %s to %s", file, dst);
      }

      unlink (file);
    }
    else
      pl_log (player, PLAYER_MSG_WARNING, MODULE_NAME,
              "image file (%s) is unavailable, maybe MPlayer can't seek in "
              "this video", file);

    rmdir (tmp);
  }
  }
}

static playback_status_t
mplayer_playback_start (player_t *player)
{
  mplayer_t *mplayer = NULL;
  char *uri = NULL;
  mrl_t *mrl;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "playback_start");

  if (!player)
    return PLAYER_PB_FATAL;

  mplayer = player->priv;

  if (!mplayer)
    return PLAYER_PB_FATAL;

  mrl = pl_playlist_get_mrl (player->playlist);
  if (!mrl)
    return PLAYER_PB_ERROR;

  uri = mp_resource_get_uri (mrl);
  if (!uri)
    return PLAYER_PB_ERROR;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "uri: %s", uri);

  /* 0: new play, 1: append to the current playlist */
  slave_cmd_str_opt (player, SLAVE_LOADFILE, uri, 0);

  PFREE (uri);

  if (get_mplayer_status (player) != MPLAYER_IS_PLAYING)
    return PLAYER_PB_ERROR;

  /*
   * Not all parameters can be set by the MRL, this function try to set/load
   * the others attributes of the 'args' structure.
   */
  mp_resource_load_args (player, mrl);

  /* load subtitle if exists */
  if (mrl->subs)
  {
    char **sub = mrl->subs;
    slave_set_property_flag (player, PROPERTY_SUB_VISIBILITY, 1);
    while (*sub)
    {
      slave_cmd_str (player, SLAVE_SUB_LOAD, *sub);
      sub++;
    }
    slave_set_property_int (player, PROPERTY_SUB, 0);
  }

  if (MRL_USES_VO (mrl))
    pl_window_map (player->window);

  return PLAYER_PB_OK;
}

static void
mplayer_playback_stop (player_t *player)
{
  mplayer_t *mplayer = NULL;
  mrl_t *mrl;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "playback_stop");

  if (!player)
    return;

  mplayer = player->priv;

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

  mrl = pl_playlist_get_mrl (player->playlist);
  if (MRL_USES_VO (mrl))
    pl_window_unmap (player->window);

  slave_cmd (player, SLAVE_STOP);
}

static playback_status_t
mplayer_playback_pause (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "playback_pause");

  if (!player)
    return PLAYER_PB_FATAL;

  slave_cmd (player, SLAVE_PAUSE);

  return PLAYER_PB_OK;
}

static void
mplayer_playback_seek (player_t *player, int value, player_pb_seek_t seek)
{
  mplayer_seek_t opt;
  float pos;

  pl_log (player, PLAYER_MSG_VERBOSE,
        MODULE_NAME, "playback_seek: %d %d", value, seek);

  if (!player)
    return;

  switch (seek)
  {
  case PLAYER_PB_SEEK_RELATIVE:
    opt = MPLAYER_SEEK_RELATIVE;
    pos = value / 1000.0;
    break;

  case PLAYER_PB_SEEK_PERCENT:
    opt = MPLAYER_SEEK_PERCENT;
    pos = value;
    break;

  case PLAYER_PB_SEEK_ABSOLUTE:
    opt = MPLAYER_SEEK_ABSOLUTE;
    pos = value / 1000.0;
    break;

  default:
    return;
  }

  slave_cmd_float_opt (player, SLAVE_SEEK, pos, opt);
}

static void
mplayer_playback_seek_chapter (player_t *player, int value, int absolute)
{
  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "playback_seek_chapter: %i %i", value, absolute);

  if (!player)
    return;

  /*
   * NOTE: seek_chapter needs at least MPlayer >= 28226 to work correctly,
   *       else MPlayer hangs if a chapter after the last is reached.
   */
  slave_cmd_int_opt (player, SLAVE_SEEK_CHAPTER, value, absolute);
}

static void
mplayer_playback_set_speed (player_t *player, float value)
{
  int speed;

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "playback_set_speed %.2f", value);

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
mplayer_audio_get_volume (player_t *player)
{
  int volume = -1;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "audio_get_volume");

  if (!player)
    return volume;

  volume = slave_get_property_int (player, PROPERTY_VOLUME);

  if (volume < 0)
    return -1;

  return volume;
}

static player_mute_t
mplayer_audio_get_mute (player_t *player)
{
  player_mute_t mute = PLAYER_MUTE_UNKNOWN;
  char *buffer;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "audio_get_mute");

  if (!player)
    return mute;

  buffer = slave_get_property_str (player, PROPERTY_MUTE);

  if (buffer)
  {
    if (!strcmp (buffer, "yes"))
      mute = PLAYER_MUTE_ON;
    else
      mute = PLAYER_MUTE_OFF;

    PFREE (buffer);
  }

  return mute;
}

static int
mplayer_get_time_pos (player_t *player)
{
  float time_pos = 0.0;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "get_time_pos");

  if (!player)
    return -1;

  time_pos = slave_get_property_float (player, PROPERTY_TIME_POS);

  if (time_pos < 0.0)
    return -1;

  return (int) (time_pos * 1000.0);
}

static int
mplayer_get_percent_pos (player_t *player)
{
  int percent_pos = 0;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "get_percent_pos");

  if (!player)
    return -1;

  percent_pos = slave_get_property_int (player, PROPERTY_PERCENT_POS);

  if (percent_pos < 0)
    return -1;

  return percent_pos;
}

static void
mplayer_set_framedrop (player_t *player, player_framedrop_t fd)
{
  mplayer_framedropping_t framedrop;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "set_framedrop: %i", fd);

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
mplayer_set_mouse_pos (player_t *player, int x, int y)
{
  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "set_mouse_pos: %i %i", x, y);

  if (!player)
    return;

  /* absolute to relative */
  if (player->window)
  {
    int video_x, video_y;

    pl_window_video_pos_get (player->window, &video_x, &video_y);
    x -= video_x;
    y -= video_y;
  }

  slave_cmd_int_opt (player, SLAVE_SET_MOUSE_POS, x, y);
}

static void
mplayer_osd_show_text (player_t *player,
                       const char *text, int x, int y, int duration)
{
  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "osd_show_text: %s %i %i %i", text, x, y, duration);

  if (!player || !text)
    return;

  /* Prevent buggy behaviours with the slave and some characters. */
  if (strchr (text, '"') || strchr (text, '\n') || strchr (text, '\r'))
  {
    pl_log (player, PLAYER_MSG_WARNING,
            MODULE_NAME, "'\"' '\\r' and '\\n' are prohibited in the string");
    return;
  }

  slave_cmd_str_opt (player, SLAVE_OSD_SHOW_TEXT, text, duration);
}

static void
mplayer_osd_state (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "osd_state: %i", value);

  if (!player)
    return;

  /* we use only level 0 (only subtitles) and 1 (+ volume + seek) */
  slave_set_property_int (player, PROPERTY_OSDLEVEL, !!value);
}

static void
mplayer_audio_set_volume (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "audio_set_volume: %d", value);

  if (!player)
    return;

  if (!check_range (player, PROPERTY_VOLUME, &value, 0))
    return;

  slave_cmd_int_opt (player, SLAVE_VOLUME, value, 1);
}

static void
mplayer_audio_set_mute (player_t *player, player_mute_t value)
{
  int mute = 0;

  if (value == PLAYER_MUTE_UNKNOWN)
    return;

  if (value == PLAYER_MUTE_ON)
    mute = 1;

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "audio_set_mute: %s", mute ? "on" : "off");

  if (!player)
    return;

  slave_set_property_flag (player, PROPERTY_MUTE, mute);
}

static void
mplayer_audio_set_delay (player_t *player, int value, int absolute)
{
  float delay = 0.0;
  int val;

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "audio_set_delay: %i %i", value, absolute);

  if (!player)
    return;

  if (!absolute)
    delay = slave_get_property_float (player, PROPERTY_AUDIO_DELAY);

  delay += value / 1000.0;
  val = (int) rintf (delay);
  if (!check_range (player, PROPERTY_AUDIO_DELAY, &val, 0))
    return;

  slave_set_property_float (player, PROPERTY_AUDIO_DELAY, delay);
}

static void
mplayer_audio_select (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "audio_select: %i", value);

  if (!player)
    return;

  if (!check_range (player, PROPERTY_SWITCH_AUDIO, &value, 0))
    return;

  slave_set_property_int (player, PROPERTY_SWITCH_AUDIO, value);
}

static void
mplayer_audio_prev (player_t *player)
{
  int audio;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "audio_prev");

  if (!player)
    return;

  audio = slave_get_property_int (player, PROPERTY_SWITCH_AUDIO);
  audio--;

  /*
   * min value for 'switch_audio' must be 0 but `mplayer -list-properties`
   * returns -2.
   */
  if (audio < 0)
    return;

  check_range (player, PROPERTY_SWITCH_AUDIO, &audio, 1);
  slave_set_property_int (player, PROPERTY_SWITCH_AUDIO, audio);
}

static void
mplayer_audio_next (player_t *player)
{
  int audio;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "audio_next");

  if (!player)
    return;

  audio = slave_get_property_int (player, PROPERTY_SWITCH_AUDIO);
  audio++;

  check_range (player, PROPERTY_SWITCH_AUDIO, &audio, 1);
  slave_set_property_int (player, PROPERTY_SWITCH_AUDIO, audio);
}

static void
mplayer_video_set_ar (player_t *player, float value)
{
  mrl_t *mrl;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "video_set_ar: %.2f", value);

  if (!player)
    return;

  if (get_mplayer_status (player) != MPLAYER_IS_PLAYING)
    return;

  mrl = pl_playlist_get_mrl (player->playlist);
  if (!MRL_USES_VO (mrl))
    return;

  /* use original aspect ratio if value is 0.0 */
  if (!value && mrl && mrl->prop && mrl->prop->video)
  {
    mrl_properties_video_t *video = mrl->prop->video;
    player->aspect = video->aspect / PLAYER_VIDEO_ASPECT_RATIO_MULT;
  }
  else
    player->aspect = value;

  pl_window_resize (player->window);

  slave_cmd_float (player, SLAVE_SWITCH_RATIO, player->aspect);
}

static void
mplayer_sub_set_delay (player_t *player, int value)
{
  float delay;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "sub_set_delay: %i", value);

  if (!player)
    return;

  delay = value / 1000.0;

  slave_set_property_float (player, PROPERTY_SUB_DELAY, delay);
}

static void
mplayer_sub_set_alignment (player_t *player, player_sub_alignment_t a)
{
  mplayer_sub_alignment_t alignment;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "sub_set_alignment: %i", a);

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
mplayer_sub_set_pos (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "sub_set_pos: %i", value);

  if (!player)
    return;

  slave_cmd_int (player, SLAVE_SUB_POS, value);
}

static void
mplayer_sub_set_visibility (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "sub_set_visibility: %i", value);

  if (!player)
    return;

  slave_set_property_flag (player, PROPERTY_SUB_VISIBILITY, value);
}

static void
mplayer_sub_scale (player_t *player, int value, int absolute)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME,
          "sub_scale: %i [%d]", value, absolute);

  if (!player)
    return;

  slave_cmd_int_opt (player, SLAVE_SUB_SCALE, value, absolute);
}

static void
mplayer_sub_select (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "sub_select: %i", value);

  if (!player)
    return;

  if (!check_range (player, PROPERTY_SUB, &value, 0))
    return;

  slave_set_property_int (player, PROPERTY_SUB, value);
}

static void
mplayer_sub_prev (player_t *player)
{
  int sub;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "sub_prev");

  if (!player)
    return;

  sub = slave_get_property_int (player, PROPERTY_SUB);
  sub--;

  /*
   * min value for 'sub' must be 0 but `mplayer -list-properties`
   * returns -1.
   */
  if (sub < 0)
    return;

  check_range (player, PROPERTY_SUB, &sub, 1);
  slave_set_property_int (player, PROPERTY_SUB, sub);
}

static void
mplayer_sub_next (player_t *player)
{
  int sub;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "sub_next");

  if (!player)
    return;

  sub = slave_get_property_int (player, PROPERTY_SUB);
  sub++;

  check_range (player, PROPERTY_SUB, &sub, 1);
  slave_set_property_int (player, PROPERTY_SUB, sub);
}

static void
mplayer_dvd_nav (player_t *player, player_dvdnav_t value)
{
  int action;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "dvd_nav: %i", value);

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

  case PLAYER_DVDNAV_PREVMENU:
    action = MPLAYER_DVDNAV_PREV;
    break;

  case PLAYER_DVDNAV_MOUSECLICK:
    action = MPLAYER_DVDNAV_MOUSE;
    break;

  default:
    return;
  }

  slave_cmd_int (player, SLAVE_DVDNAV, action);
}

static void
mplayer_dvd_angle_set (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "dvd_angle_set: %i", value);

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

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "dvd_angle_prev");

  if (!player)
    return;

  angle = slave_get_property_int (player, PROPERTY_ANGLE);
  angle--;

  /*
   * min value for 'angle' must be 1 but `mplayer -list-properties`
   * returns -2.
   */
  if (angle < 1)
    return;

  check_range (player, PROPERTY_ANGLE, &angle, 1);
  slave_set_property_int (player, PROPERTY_ANGLE, angle);
}

static void
mplayer_dvd_angle_next (player_t *player)
{
  int angle;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "dvd_angle_next");

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
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "dvd_title_set: %i", value);

  if (!player)
    return;

  if (value < 1 || value > 99)
    return;

  slave_cmd_int (player, SLAVE_SWITCH_TITLE, value);
}

static void
mplayer_tv_channel_set (player_t *player, const char *channel)
{
  mrl_resource_t res;

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "tv_channel_set: %s", channel ? channel : "?");

  if (!player || !channel)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res == MRL_RESOURCE_TV)
    slave_cmd_str (player, SLAVE_TV_SET_CHANNEL, channel);
  else /* MRL_RESOURCE_DVB */
    pl_log (player, PLAYER_MSG_WARNING, MODULE_NAME, "unsupported with DVB");
}

static void
mplayer_tv_channel_prev (player_t *player)
{
  mrl_resource_t res;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "tv_channel_prev");

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res == MRL_RESOURCE_TV)
    slave_cmd_int (player, SLAVE_TV_STEP_CHANNEL, 0);
  else /* MRL_RESOURCE_DVB */
    pl_log (player, PLAYER_MSG_WARNING, MODULE_NAME, "unsupported with DVB");
}

static void
mplayer_tv_channel_next (player_t *player)
{
  mrl_resource_t res;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "tv_channel_next");

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res == MRL_RESOURCE_TV)
    slave_cmd_int (player, SLAVE_TV_STEP_CHANNEL, 1);
  else /* MRL_RESOURCE_DVB */
    pl_log (player, PLAYER_MSG_WARNING, MODULE_NAME, "unsupported with DVB");
}

static void
mplayer_radio_channel_set (player_t *player, const char *channel)
{
  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "radio_channel_set: %s", channel ? channel : "?");

  if (!player || !channel)
    return;

  slave_cmd_str (player, SLAVE_RADIO_SET_CHANNEL, channel);
}

static void
mplayer_radio_channel_prev (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "radio_channel_prev");

  if (!player)
    return;

  slave_cmd_int (player, SLAVE_RADIO_STEP_CHANNEL, -1);
}

static void
mplayer_radio_channel_next (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "radio_channel_next");

  if (!player)
    return;

  slave_cmd_int (player, SLAVE_RADIO_STEP_CHANNEL, 1);
}

/*****************************************************************************/
/*                            Public Wrapper API                             */
/*****************************************************************************/

int
pl_supported_resources_mplayer (mrl_resource_t res)
{
  switch (res)
  {
  case MRL_RESOURCE_CDDA:
  case MRL_RESOURCE_CDDB:
  case MRL_RESOURCE_DVB:
  case MRL_RESOURCE_DVD:
  case MRL_RESOURCE_DVDNAV:
  case MRL_RESOURCE_FILE:
  case MRL_RESOURCE_FTP:
  case MRL_RESOURCE_HTTP:
  case MRL_RESOURCE_MMS:
  case MRL_RESOURCE_RADIO:
  case MRL_RESOURCE_RTP:
  case MRL_RESOURCE_RTSP:
  case MRL_RESOURCE_SMB:
  case MRL_RESOURCE_TV:
  case MRL_RESOURCE_UDP:
  case MRL_RESOURCE_UNSV:
  case MRL_RESOURCE_VCD:
    return 1;

  default:
    return 0;
  }
}

player_funcs_t *
pl_register_functions_mplayer (void)
{
  player_funcs_t *funcs = NULL;

  funcs = PCALLOC (player_funcs_t, 1);
  if (!funcs)
    return NULL;

  funcs->init               = mplayer_init;
  funcs->uninit             = mplayer_uninit;
  funcs->set_verbosity      = mplayer_set_verbosity;

  funcs->mrl_retrieve_props = mplayer_mrl_retrieve_properties;
  funcs->mrl_retrieve_meta  = mplayer_mrl_retrieve_metadata;
  funcs->mrl_video_snapshot = mplayer_mrl_video_snapshot;

  funcs->get_time_pos       = mplayer_get_time_pos;
  funcs->get_percent_pos    = mplayer_get_percent_pos;
  funcs->set_framedrop      = mplayer_set_framedrop;
  funcs->set_mouse_pos      = mplayer_set_mouse_pos;
  funcs->osd_show_text      = mplayer_osd_show_text;
  funcs->osd_state          = mplayer_osd_state;

  funcs->pb_start           = mplayer_playback_start;
  funcs->pb_stop            = mplayer_playback_stop;
  funcs->pb_pause           = mplayer_playback_pause;
  funcs->pb_seek            = mplayer_playback_seek;
  funcs->pb_seek_chapter    = mplayer_playback_seek_chapter;
  funcs->pb_set_speed       = mplayer_playback_set_speed;

  funcs->audio_get_volume   = mplayer_audio_get_volume;
  funcs->audio_set_volume   = mplayer_audio_set_volume;
  funcs->audio_get_mute     = mplayer_audio_get_mute;
  funcs->audio_set_mute     = mplayer_audio_set_mute;
  funcs->audio_set_delay    = mplayer_audio_set_delay;
  funcs->audio_select       = mplayer_audio_select;
  funcs->audio_prev         = mplayer_audio_prev;
  funcs->audio_next         = mplayer_audio_next;

  funcs->video_set_aspect   = NULL;
  funcs->video_set_panscan  = NULL;
  funcs->video_set_ar       = mplayer_video_set_ar;

  funcs->sub_set_delay      = mplayer_sub_set_delay;
  funcs->sub_set_alignment  = mplayer_sub_set_alignment;
  funcs->sub_set_pos        = mplayer_sub_set_pos;
  funcs->sub_set_visibility = mplayer_sub_set_visibility;
  funcs->sub_scale          = mplayer_sub_scale;
  funcs->sub_select         = mplayer_sub_select;
  funcs->sub_prev           = mplayer_sub_prev;
  funcs->sub_next           = mplayer_sub_next;

  funcs->dvd_nav            = mplayer_dvd_nav;
  funcs->dvd_angle_set      = mplayer_dvd_angle_set;
  funcs->dvd_angle_prev     = mplayer_dvd_angle_prev;
  funcs->dvd_angle_next     = mplayer_dvd_angle_next;
  funcs->dvd_title_set      = mplayer_dvd_title_set;
  funcs->dvd_title_prev     = NULL;
  funcs->dvd_title_next     = NULL;

  funcs->tv_channel_set     = mplayer_tv_channel_set;
  funcs->tv_channel_prev    = mplayer_tv_channel_prev;
  funcs->tv_channel_next    = mplayer_tv_channel_next;

  funcs->radio_channel_set  = mplayer_radio_channel_set;
  funcs->radio_channel_prev = mplayer_radio_channel_prev;
  funcs->radio_channel_next = mplayer_radio_channel_next;

  funcs->vdr                = PL_NOT_SUPPORTED;

  return funcs;
}

void *
pl_register_private_mplayer (void)
{
  mplayer_t *mplayer = NULL;

  mplayer = PCALLOC (mplayer_t, 1);
  if (!mplayer)
    return NULL;

  mplayer->status = MPLAYER_IS_DEAD;

  sem_init (&mplayer->sem, 0, 0);
  pthread_cond_init (&mplayer->cond_start, NULL);
  pthread_cond_init (&mplayer->cond_status, NULL);
  pthread_mutex_init (&mplayer->mutex_search, NULL);
  pthread_mutex_init (&mplayer->mutex_status, NULL);
  pthread_mutex_init (&mplayer->mutex_verbosity, NULL);
  pthread_mutex_init (&mplayer->mutex_start, NULL);

  return mplayer;
}
