/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2008 Mathieu Schroeter <mathieu@schroetersa.ch>
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

#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <inttypes.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "fifo_queue.h"
#include "supervisor.h"

typedef enum supervisor_state {
  SUPERVISOR_STATE_DEAD,
  SUPERVISOR_STATE_RUNNING,
} supervisor_state_t;

typedef struct supervisor_send_s {
  void *in;
  void *out;
  supervisor_mode_t mode;
} supervisor_send_t;

struct supervisor_s {
  pthread_t th_supervisor;
  supervisor_state_t state;
  fifo_queue_t *queue;
  pthread_mutex_t mutex_sv;
  sem_t sem_ctl;

  int cb_run;
  pthread_t cb_tid;
  pthread_mutex_t mutex_cb;

  /* to synchronize with an event handler (for example) */
  int use_sync;
  int sync_run;
  pthread_t sync_job;
  pthread_cond_t sync_cond;
  pthread_mutex_t sync_mutex;
};

#define MODULE_NAME "supervisor"


/*****************************************************************************/
/*                        Supervisor private functions                       */
/*****************************************************************************/
/*
 * NOTE: All functions below are only the links between the public API and
 *       the internal stuff.
 *       Only tests on pointers are authorized here, never use these functions
 *       to change the original behaviour, use player_internal.c for that.
 *       (for example, 'stop' before 'start' here, is prohibited).
 */

/*********************************** MRL *************************************/

static void
supervisor_mrl_free (pl_unused player_t *player, void *in, pl_unused void *out)
{
  if (!in)
    return;

  mrl_sv_free (in, 0);
}

static void
supervisor_mrl_get_property (player_t *player, void *in, void *out)
{
  supervisor_data_mrl_t *input = in;
  uint32_t *output = out;

  if (!player || !in || !out)
    return;

  *output = mrl_sv_get_property (player, input->mrl, input->value);
}

static void
supervisor_mrl_get_ao_codec (player_t *player, void *in, void *out)
{
  char **output = out;

  if (!player || !out)
    return;

  *output = mrl_sv_get_audio_codec (player, in);
}

static void
supervisor_mrl_get_vo_codec (player_t *player, void *in, void *out)
{
  char **output = out;

  if (!player || !out)
    return;

  *output = mrl_sv_get_video_codec (player, in);
}

static void
supervisor_mrl_get_size (player_t *player, void *in, void *out)
{
  off_t *output = out;

  if (!player || !out)
    return;

  *output = mrl_sv_get_size (player, in);
}

static void
supervisor_mrl_get_metadata (player_t *player, void *in, void *out)
{
  supervisor_data_mrl_t *input = in;
  char **output = out;

  if (!player || !in || !out)
    return;

  *output = mrl_sv_get_metadata (player, input->mrl, input->value);
}

static void
supervisor_mrl_get_metadata_cd_track (player_t *player, void *in, void *out)
{
  supervisor_data_mrl_t *input = in;
  supervisor_data_out_metadata_cd_t *output = out;

  if (!player || !in || !out)
    return;

  output->name =
    mrl_sv_get_metadata_cd_track (player,
                                  input->mrl, input->value, &output->length);
}

static void
supervisor_mrl_get_metadata_cd (player_t *player, void *in, void *out)
{
  supervisor_data_mrl_t *input = in;
  uint32_t *output = out;

  if (!player || !in || !out)
    return;

  *output = mrl_sv_get_metadata_cd (player, input->mrl, input->value);
}

static void
supervisor_mrl_get_metadata_dvd_title (player_t *player, void *in, void *out)
{
  supervisor_data_in_metadata_dvd_t *input = in;
  uint32_t *output = out;

  if (!player || !in || !out)
    return;

  *output = mrl_sv_get_metadata_dvd_title (player,
                                           input->mrl, input->id, input->type);
}

static void
supervisor_mrl_get_metadata_dvd (player_t *player, void *in, void *out)
{
  supervisor_data_out_metadata_dvd_t *output = out;

  if (!player || !out)
    return;

  output->volumeid = mrl_sv_get_metadata_dvd (player, in, &output->titles);
}

static void
supervisor_mrl_get_metadata_sub (player_t *player, void *in, void *out)
{
  supervisor_data_mrl_t *input = in;
  supervisor_data_out_metadata_t *output = out;

  if (!player || !in || !out)
    return;

  output->ret =
    mrl_sv_get_metadata_subtitle (player, input->mrl, input->value,
                                  &output->id, &output->name, &output->lang);
}

static void
supervisor_mrl_get_metadata_sub_nb (player_t *player, void *in, void *out)
{
  uint32_t *output = out;

  if (!player || !out)
    return;

  *output = mrl_sv_get_metadata_subtitle_nb (player, in);
}

static void
supervisor_mrl_get_metadata_audio (player_t *player, void *in, void *out)
{
  supervisor_data_mrl_t *input = in;
  supervisor_data_out_metadata_t *output = out;

  if (!player || !in || !out)
    return;

  output->ret =
    mrl_sv_get_metadata_audio (player, input->mrl, input->value,
                               &output->id, &output->name, &output->lang);
}

static void
supervisor_mrl_get_metadata_audio_nb (player_t *player, void *in, void *out)
{
  uint32_t *output = out;

  if (!player || !out)
    return;

  *output = mrl_sv_get_metadata_audio_nb (player, in);
}

static void
supervisor_mrl_get_type (player_t *player, void *in, void *out)
{
  mrl_type_t *output = out;

  if (!player || !out)
    return;

  *output = mrl_sv_get_type (player, in);
}

static void
supervisor_mrl_get_resource (player_t *player, void *in, void *out)
{
  mrl_resource_t *output = out;

  if (!player || !out)
    return;

  *output = mrl_sv_get_resource (player, in);
}

static void
supervisor_mrl_add_subtitle (player_t *player, void *in, pl_unused void *out)
{
  supervisor_data_sub_t *input = in;

  if (!player || !in)
    return;

  mrl_sv_add_subtitle (player, input->mrl, input->sub);
}

static void
supervisor_mrl_new (player_t *player, void *in, void *out)
{
  supervisor_data_args_t *input = in;
  mrl_t **output = out;

  if (!player || !in || !out)
    return;

  *output = mrl_sv_new (player, input->res, input->args);
}

static void
supervisor_mrl_video_snapshot (player_t *player, void *in, pl_unused void *out)
{
  supervisor_data_snapshot_t *input = in;

  if (!player || !in)
    return;

  mrl_sv_video_snapshot (player, input->mrl,
                         input->pos, input->type, input->dst);
}

/************************* Player (Un)Initialization *************************/

static void
supervisor_player_init (player_t *player, pl_unused void *in, void *out)
{
  init_status_t *output = out;

  if (!player || !out)
    return;

  *output = player_sv_init (player);
}

static void
supervisor_player_uninit (player_t *player,
                          pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_uninit (player);
}

static void
supervisor_player_set_verbosity (player_t *player,
                                 void *in, pl_unused void *out)
{
  player_verbosity_level_t *input = in;

  if (!player || !in)
    return;

  player_sv_set_verbosity (player, *input);
}

/************************* Player to MRL connection **************************/

static void
supervisor_player_mrl_get_current (player_t *player,
                                   pl_unused void *in, void *out)
{
  mrl_t **output = out;

  if (!player || !out)
    return;

  *output = player_sv_mrl_get_current (player);
}

static void
supervisor_player_mrl_set (player_t *player,
                           void *in, pl_unused void *out)
{
  if (!player || !in)
    return;

  player_sv_mrl_set (player, in);
}

static void
supervisor_player_mrl_append (player_t *player,
                              void *in, pl_unused void *out)
{
  supervisor_data_mrl_t *input = in;

  if (!player || !in)
    return;

  player_sv_mrl_append (player, input->mrl, input->value);
}

static void
supervisor_player_mrl_remove (player_t *player,
                              pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_mrl_remove (player);
}

static void
supervisor_player_mrl_remove_all (player_t *player,
                                  pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_mrl_remove_all (player);
}

static void
supervisor_player_mrl_previous (player_t *player,
                                pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_mrl_previous (player);
}

static void
supervisor_player_mrl_next (player_t *player,
                            pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_mrl_next (player);
}

static void
supervisor_player_mrl_next_play (player_t *player,
                                 pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_mrl_next_play (player);
}

/************************ Player tuning & properties *************************/

static void
supervisor_player_get_time_pos (player_t *player,
                                pl_unused void *in, void *out)
{
  int *output = out;

  if (!player || !out)
    return;

  *output = player_sv_get_time_pos (player);
}

static void
supervisor_player_get_percent_pos (player_t *player,
                                   pl_unused void *in, void *out)
{
  int *output = out;

  if (!player || !out)
    return;

  *output = player_sv_get_percent_pos (player);
}

static void
supervisor_player_set_playback (player_t *player,
                                void *in, pl_unused void *out)
{
  player_pb_t *input = in;

  if (!player || !in)
    return;

  player_sv_set_playback (player, *input);
}

static void
supervisor_player_set_loop (player_t *player,
                            void *in, pl_unused void *out)
{
  supervisor_data_mode_t *input = in;

  if (!player || !in)
    return;

  player_sv_set_loop (player, input->mode, input->value);
}

static void
supervisor_player_set_shuffle (player_t *player,
                               void *in, pl_unused void *out)
{
  int *input = in;

  if (!player || !in)
    return;

  player_sv_set_shuffle (player, *input);
}

static void
supervisor_player_set_framedrop (player_t *player,
                                 void *in, pl_unused void *out)
{
  player_framedrop_t *input = in;

  if (!player || !in)
    return;

  player_sv_set_framedrop (player, *input);
}

static void
supervisor_player_set_mouse_pos (player_t *player,
                                 void *in, pl_unused void *out)
{
  supervisor_data_coord_t *input = in;

  if (!player || !in)
    return;

  player_sv_set_mouse_position (player, input->x, input->y);
}

static void
supervisor_player_x_window_set_props (player_t *player,
                                      void *in, pl_unused void *out)
{
  supervisor_data_window_t *input = in;

  if (!player || !in)
    return;

  player_sv_x_window_set_properties (player, input->x, input->y,
                                             input->w, input->h, input->flags);
}

static void
supervisor_player_osd_show_text (player_t *player,
                                 void *in, pl_unused void *out)
{
  supervisor_data_osd_t *input = in;

  if (!player || !in)
    return;

  player_sv_osd_show_text (player,
                           input->text, input->x, input->y, input->duration);
}

static void
supervisor_player_osd_state (player_t *player, void *in, pl_unused void *out)
{
  int *input = in;

  if (!player || !in)
    return;

  player_sv_osd_state (player, *input);
}

/************************ Playback related controls **************************/

static void
supervisor_player_pb_get_state (player_t *player,
                                pl_unused void *in, void *out)
{
  player_pb_state_t *output = out;

  if (!player || !out)
    return;

  *output = player_sv_playback_get_state (player);
}

static void
supervisor_player_pb_start (player_t *player,
                            pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_playback_start (player);
}

static void
supervisor_player_pb_stop (player_t *player,
                           pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_playback_stop (player);
}

static void
supervisor_player_pb_pause (player_t *player,
                            pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_playback_pause (player);
}

static void
supervisor_player_pb_seek (player_t *player,
                           void *in, pl_unused void *out)
{
  supervisor_data_mode_t *input = in;

  if (!player || !in)
    return;

  player_sv_playback_seek (player, input->value, input->mode);
}

static void
supervisor_player_pb_seek_chapter (player_t *player,
                                   void *in, pl_unused void *out)
{
  supervisor_data_mode_t *input = in;

  if (!player || !in)
    return;

  player_sv_playback_seek_chapter (player, input->value, input->mode);
}

static void
supervisor_player_pb_speed (player_t *player,
                            void *in, pl_unused void *out)
{
  float *input = in;

  if (!player || !in)
    return;

  player_sv_playback_speed (player, *input);
}

/************************** Audio related controls ***************************/

static void
supervisor_player_ao_volume_get (player_t *player,
                                 pl_unused void *in, void *out)
{
  int *output = out;

  if (!player || !out)
    return;

  *output = player_sv_audio_volume_get (player);
}

static void
supervisor_player_ao_volume_set (player_t *player,
                                 void *in, pl_unused void *out)
{
  int *input = in;

  if (!player || !in)
    return;

  player_sv_audio_volume_set (player, *input);
}

static void
supervisor_player_ao_mute_get (player_t *player,
                               pl_unused void *in, void *out)
{
  player_mute_t *output = out;

  if (!player || !out)
    return;

  *output = player_sv_audio_mute_get (player);
}

static void
supervisor_player_ao_mute_set (player_t *player,
                               void *in, pl_unused void *out)
{
  player_mute_t *input = in;

  if (!player || !in)
    return;

  player_sv_audio_mute_set (player, *input);
}

static void
supervisor_player_ao_set_delay (player_t *player,
                                void *in, pl_unused void *out)
{
  supervisor_data_mode_t *input = in;

  if (!player || !in)
    return;

  player_sv_audio_set_delay (player, input->value, input->mode);
}

static void
supervisor_player_ao_select (player_t *player,
                             void *in, pl_unused void *out)
{
  int *input = in;

  if (!player || !in)
    return;

  player_sv_audio_select (player, *input);
}

static void
supervisor_player_ao_prev (player_t *player,
                           pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_audio_prev (player);
}

static void
supervisor_player_ao_next (player_t *player,
                           pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_audio_next (player);
}

/************************** Video related controls ***************************/

static void
supervisor_player_vo_set_aspect (player_t *player,
                                 void *in, pl_unused void *out)
{
  supervisor_data_vo_t *input = in;

  if (!player || !in)
    return;

  player_sv_video_set_aspect (player, input->list, input->value, input->mode);
}

static void
supervisor_player_vo_set_panscan (player_t *player,
                                  void *in, pl_unused void *out)
{
  supervisor_data_vo_t *input = in;

  if (!player || !in)
    return;

  player_sv_video_set_panscan (player, input->value, input->mode);
}

static void
supervisor_player_vo_set_ar (player_t *player,
                             void *in, pl_unused void *out)
{
  float *input = in;

  if (!player || !in)
    return;

  player_sv_video_set_aspect_ratio (player, *input);
}

/************************ Subtitles related controls *************************/

static void
supervisor_player_sub_set_delay (player_t *player,
                                 void *in, pl_unused void *out)
{
  int *input = in;

  if (!player || !in)
    return;

  player_sv_subtitle_set_delay (player, *input);
}

static void
supervisor_player_sub_set_align (player_t *player,
                                 void *in, pl_unused void *out)
{
  player_sub_alignment_t *input = in;

  if (!player || !in)
    return;

  player_sv_subtitle_set_alignment (player, *input);
}

static void
supervisor_player_sub_set_pos (player_t *player,
                               void *in, pl_unused void *out)
{
  int *input = in;

  if (!player || !in)
    return;

  player_sv_subtitle_set_position (player, *input);
}

static void
supervisor_player_sub_set_vis (player_t *player,
                               void *in, pl_unused void *out)
{
  int *input = in;

  if (!player || !in)
    return;

  player_sv_subtitle_set_visibility (player, *input);
}

static void
supervisor_player_sub_scale (player_t *player,
                             void *in, pl_unused void *out)
{
  supervisor_data_mode_t *input = in;

  if (!player || !in)
    return;

  player_sv_subtitle_scale (player, input->value, input->mode);
}

static void
supervisor_player_sub_select (player_t *player,
                              void *in, pl_unused void *out)
{
  int *input = in;

  if (!player || !in)
    return;

  player_sv_subtitle_select (player, *input);
}

static void
supervisor_player_sub_prev (player_t *player,
                            pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_subtitle_prev (player);
}

static void
supervisor_player_sub_next (player_t *player,
                            pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_subtitle_next (player);
}

/************************** DVD specific controls ****************************/

static void
supervisor_player_dvd_nav (player_t *player,
                           void *in, pl_unused void *out)
{
  player_dvdnav_t *input = in;

  if (!player || !in)
    return;

  player_sv_dvd_nav (player, *input);
}

static void
supervisor_player_dvd_angle_select (player_t *player,
                                    void *in, pl_unused void *out)
{
  int *input = in;

  if (!player || !in)
    return;

  player_sv_dvd_angle_select (player, *input);
}

static void
supervisor_player_dvd_angle_prev (player_t *player,
                                  pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_dvd_angle_prev (player);
}

static void
supervisor_player_dvd_angle_next (player_t *player,
                                  pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_dvd_angle_next (player);
}

static void
supervisor_player_dvd_title_select (player_t *player,
                                    void *in, pl_unused void *out)
{
  int *input = in;

  if (!player || !in)
    return;

  player_sv_dvd_title_select (player, *input);
}

static void
supervisor_player_dvd_title_prev (player_t *player,
                                  pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_dvd_title_prev (player);
}

static void
supervisor_player_dvd_title_next (player_t *player,
                                  pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_dvd_title_next (player);
}

/************************* TV/DVB specific controls **************************/

static void
supervisor_player_tv_chan_select (player_t *player,
                                  void *in, pl_unused void *out)
{
  if (!player || !in)
    return;

  player_sv_tv_channel_select (player, in);
}

static void
supervisor_player_tv_chan_prev (player_t *player,
                                pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_tv_channel_prev (player);
}

static void
supervisor_player_tv_chan_next (player_t *player,
                                pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_tv_channel_next (player);
}

/************************* Radio specific controls ***************************/

static void
supervisor_player_radio_chan_select (player_t *player,
                                     void *in, pl_unused void *out)
{
  if (!player || !in)
    return;

  player_sv_radio_channel_select (player, in);
}

static void
supervisor_player_radio_chan_prev (player_t *player,
                                   pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_radio_channel_prev (player);
}

static void
supervisor_player_radio_chan_next (player_t *player,
                                   pl_unused void *in, pl_unused void *out)
{
  if (!player)
    return;

  player_sv_radio_channel_next (player);
}

/************************** VDR specific controls ****************************/

static void
supervisor_player_vdr (player_t *player, void *in, pl_unused void *out)
{
  player_vdr_t *input = in;

  if (!player || !in)
    return;

  player_sv_vdr (player, *input);
}

static void (*g_supervisor_funcs[]) (player_t *player, void *in, void *out) = {
  /* MRL */
  [SV_FUNC_MRL_FREE]                     = supervisor_mrl_free,
  [SV_FUNC_MRL_GET_PROPERTY]             = supervisor_mrl_get_property,
  [SV_FUNC_MRL_GET_AO_CODEC]             = supervisor_mrl_get_ao_codec,
  [SV_FUNC_MRL_GET_VO_CODEC]             = supervisor_mrl_get_vo_codec,
  [SV_FUNC_MRL_GET_SIZE]                 = supervisor_mrl_get_size,
  [SV_FUNC_MRL_GET_METADATA]             = supervisor_mrl_get_metadata,
  [SV_FUNC_MRL_GET_METADATA_CD_TRACK]    = supervisor_mrl_get_metadata_cd_track,
  [SV_FUNC_MRL_GET_METADATA_CD]          = supervisor_mrl_get_metadata_cd,
  [SV_FUNC_MRL_GET_METADATA_DVD_TITLE]   = supervisor_mrl_get_metadata_dvd_title,
  [SV_FUNC_MRL_GET_METADATA_DVD]         = supervisor_mrl_get_metadata_dvd,
  [SV_FUNC_MRL_GET_METADATA_SUBTITLE]    = supervisor_mrl_get_metadata_sub,
  [SV_FUNC_MRL_GET_METADATA_SUBTITLE_NB] = supervisor_mrl_get_metadata_sub_nb,
  [SV_FUNC_MRL_GET_METADATA_AUDIO]       = supervisor_mrl_get_metadata_audio,
  [SV_FUNC_MRL_GET_METADATA_AUDIO_NB]    = supervisor_mrl_get_metadata_audio_nb,
  [SV_FUNC_MRL_GET_TYPE]                 = supervisor_mrl_get_type,
  [SV_FUNC_MRL_GET_RESOURCE]             = supervisor_mrl_get_resource,
  [SV_FUNC_MRL_ADD_SUBTITLE]             = supervisor_mrl_add_subtitle,
  [SV_FUNC_MRL_NEW]                      = supervisor_mrl_new,
  [SV_FUNC_MRL_VIDEO_SNAPSHOT]           = supervisor_mrl_video_snapshot,

  /* Player (Un)Initialization */
  [SV_FUNC_PLAYER_INIT]                  = supervisor_player_init,
  [SV_FUNC_PLAYER_UNINIT]                = supervisor_player_uninit,
  [SV_FUNC_PLAYER_SET_VERBOSITY]         = supervisor_player_set_verbosity,

  /* Player to MRL connection */
  [SV_FUNC_PLAYER_MRL_GET_CURRENT]       = supervisor_player_mrl_get_current,
  [SV_FUNC_PLAYER_MRL_SET]               = supervisor_player_mrl_set,
  [SV_FUNC_PLAYER_MRL_APPEND]            = supervisor_player_mrl_append,
  [SV_FUNC_PLAYER_MRL_REMOVE]            = supervisor_player_mrl_remove,
  [SV_FUNC_PLAYER_MRL_REMOVE_ALL]        = supervisor_player_mrl_remove_all,
  [SV_FUNC_PLAYER_MRL_PREVIOUS]          = supervisor_player_mrl_previous,
  [SV_FUNC_PLAYER_MRL_NEXT]              = supervisor_player_mrl_next,
  [SV_FUNC_PLAYER_MRL_NEXT_PLAY]         = supervisor_player_mrl_next_play,

  /* Player tuning & properties */
  [SV_FUNC_PLAYER_GET_TIME_POS]          = supervisor_player_get_time_pos,
  [SV_FUNC_PLAYER_GET_PERCENT_POS]       = supervisor_player_get_percent_pos,
  [SV_FUNC_PLAYER_SET_PLAYBACK]          = supervisor_player_set_playback,
  [SV_FUNC_PLAYER_SET_LOOP]              = supervisor_player_set_loop,
  [SV_FUNC_PLAYER_SET_SHUFFLE]           = supervisor_player_set_shuffle,
  [SV_FUNC_PLAYER_SET_FRAMEDROP]         = supervisor_player_set_framedrop,
  [SV_FUNC_PLAYER_SET_MOUSE_POS]         = supervisor_player_set_mouse_pos,
  [SV_FUNC_PLAYER_X_WINDOW_SET_PROPS]    = supervisor_player_x_window_set_props,
  [SV_FUNC_PLAYER_OSD_SHOW_TEXT]         = supervisor_player_osd_show_text,
  [SV_FUNC_PLAYER_OSD_STATE]             = supervisor_player_osd_state,

  /* Playback related controls */
  [SV_FUNC_PLAYER_PB_GET_STATE]          = supervisor_player_pb_get_state,
  [SV_FUNC_PLAYER_PB_START]              = supervisor_player_pb_start,
  [SV_FUNC_PLAYER_PB_STOP]               = supervisor_player_pb_stop,
  [SV_FUNC_PLAYER_PB_PAUSE]              = supervisor_player_pb_pause,
  [SV_FUNC_PLAYER_PB_SEEK]               = supervisor_player_pb_seek,
  [SV_FUNC_PLAYER_PB_SEEK_CHAPTER]       = supervisor_player_pb_seek_chapter,
  [SV_FUNC_PLAYER_PB_SPEED]              = supervisor_player_pb_speed,

  /* Audio related controls */
  [SV_FUNC_PLAYER_AO_VOLUME_GET]         = supervisor_player_ao_volume_get,
  [SV_FUNC_PLAYER_AO_VOLUME_SET]         = supervisor_player_ao_volume_set,
  [SV_FUNC_PLAYER_AO_MUTE_GET]           = supervisor_player_ao_mute_get,
  [SV_FUNC_PLAYER_AO_MUTE_SET]           = supervisor_player_ao_mute_set,
  [SV_FUNC_PLAYER_AO_SET_DELAY]          = supervisor_player_ao_set_delay,
  [SV_FUNC_PLAYER_AO_SELECT]             = supervisor_player_ao_select,
  [SV_FUNC_PLAYER_AO_PREV]               = supervisor_player_ao_prev,
  [SV_FUNC_PLAYER_AO_NEXT]               = supervisor_player_ao_next,

  /* Video related controls */
  [SV_FUNC_PLAYER_VO_SET_ASPECT]         = supervisor_player_vo_set_aspect,
  [SV_FUNC_PLAYER_VO_SET_PANSCAN]        = supervisor_player_vo_set_panscan,
  [SV_FUNC_PLAYER_VO_SET_AR]             = supervisor_player_vo_set_ar,

  /* Subtitles related controls */
  [SV_FUNC_PLAYER_SUB_SET_DELAY]         = supervisor_player_sub_set_delay,
  [SV_FUNC_PLAYER_SUB_SET_ALIGN]         = supervisor_player_sub_set_align,
  [SV_FUNC_PLAYER_SUB_SET_POS]           = supervisor_player_sub_set_pos,
  [SV_FUNC_PLAYER_SUB_SET_VIS]           = supervisor_player_sub_set_vis,
  [SV_FUNC_PLAYER_SUB_SCALE]             = supervisor_player_sub_scale,
  [SV_FUNC_PLAYER_SUB_SELECT]            = supervisor_player_sub_select,
  [SV_FUNC_PLAYER_SUB_PREV]              = supervisor_player_sub_prev,
  [SV_FUNC_PLAYER_SUB_NEXT]              = supervisor_player_sub_next,

  /* DVD specific controls */
  [SV_FUNC_PLAYER_DVD_NAV]               = supervisor_player_dvd_nav,
  [SV_FUNC_PLAYER_DVD_ANGLE_SELECT]      = supervisor_player_dvd_angle_select,
  [SV_FUNC_PLAYER_DVD_ANGLE_PREV]        = supervisor_player_dvd_angle_prev,
  [SV_FUNC_PLAYER_DVD_ANGLE_NEXT]        = supervisor_player_dvd_angle_next,
  [SV_FUNC_PLAYER_DVD_TITLE_SELECT]      = supervisor_player_dvd_title_select,
  [SV_FUNC_PLAYER_DVD_TITLE_PREV]        = supervisor_player_dvd_title_prev,
  [SV_FUNC_PLAYER_DVD_TITLE_NEXT]        = supervisor_player_dvd_title_next,

  /* TV/DVB specific controls */
  [SV_FUNC_PLAYER_TV_CHAN_SELECT]        = supervisor_player_tv_chan_select,
  [SV_FUNC_PLAYER_TV_CHAN_PREV]          = supervisor_player_tv_chan_prev,
  [SV_FUNC_PLAYER_TV_CHAN_NEXT]          = supervisor_player_tv_chan_next,

  /* Radio specific controls */
  [SV_FUNC_PLAYER_RADIO_CHAN_SELECT]     = supervisor_player_radio_chan_select,
  [SV_FUNC_PLAYER_RADIO_CHAN_PREV]       = supervisor_player_radio_chan_prev,
  [SV_FUNC_PLAYER_RADIO_CHAN_NEXT]       = supervisor_player_radio_chan_next,

  /* VDR specific controls */
  [SV_FUNC_PLAYER_VDR]                   = supervisor_player_vdr,

};

static const int g_supervisor_funcs_nb = ARRAY_NB_ELEMENTS (g_supervisor_funcs);


/*****************************************************************************/
/*                   Supervisor synchronization and thread                   */
/*****************************************************************************/

static void
supervisor_sync_catch (supervisor_t *supervisor)
{
  if (!supervisor)
    return;

  if (!supervisor->use_sync)
    return;

  pthread_mutex_lock (&supervisor->sync_mutex);
  /* someone already running? */
  if (supervisor->sync_run &&
      !pthread_equal (supervisor->sync_job, supervisor->th_supervisor))
    pthread_cond_wait (&supervisor->sync_cond, &supervisor->sync_mutex);
  supervisor->sync_job = supervisor->th_supervisor;
  supervisor->sync_run = 1;
  pthread_mutex_unlock (&supervisor->sync_mutex);
}

static void
supervisor_sync_release (supervisor_t *supervisor)
{
  if (!supervisor)
    return;

  if (!supervisor->use_sync)
    return;

  pthread_mutex_lock (&supervisor->sync_mutex);
  supervisor->sync_run = 0;
  pthread_cond_signal (&supervisor->sync_cond); /* release for "other" */
  pthread_mutex_unlock (&supervisor->sync_mutex);
}

void
pl_supervisor_sync_recatch (player_t *player, pthread_t which)
{
  supervisor_t *supervisor;
  int useless = 0;

  if (!player)
    return;

  supervisor = player->supervisor;
  if (!supervisor)
    return;

  if (pthread_equal (supervisor->th_supervisor, which))
  {
    pl_log (player, PLAYER_MSG_ERROR, MODULE_NAME, "recatch for own identity?");
    return;
  }

  pthread_mutex_lock (&supervisor->sync_mutex);
  if (pthread_equal (supervisor->sync_job, supervisor->th_supervisor) &&
      pthread_equal (supervisor->sync_job, pthread_self ()))
  {
    supervisor->sync_job = which;
    pthread_cond_signal (&supervisor->sync_cond); /* release for "which" */
  }
  else
    useless = 1;
  pthread_mutex_unlock (&supervisor->sync_mutex);

  if (useless)
    return;

  supervisor_sync_catch (supervisor);

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "recatch");
}

static void *
thread_supervisor (void *arg)
{
  int res;
  player_t *player;
  supervisor_t *supervisor;

  player = arg;
  if (!player)
    pthread_exit (NULL);

  supervisor = player->supervisor;
  if (!supervisor)
    pthread_exit (NULL);

  supervisor->state = SUPERVISOR_STATE_RUNNING;

  while (supervisor->state == SUPERVISOR_STATE_RUNNING)
  {
    supervisor_ctl_t ctl = SV_FUNC_NOP;
    supervisor_mode_t mode;
    supervisor_send_t *data = NULL;
    void *in, *out;

    /* wait for job */
    res = pl_fifo_queue_pop (supervisor->queue, &ctl, (void *) &data);
    if (res)
    {
      pl_log (player, PLAYER_MSG_ERROR,
              MODULE_NAME, "error on queue? no sense :(");
      continue; /* retry */
    }

    in = data->in;
    out = data->out;
    mode = data->mode;
    PFREE (data);

    supervisor_sync_catch (supervisor);

    pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "run job: %i (%s)",
            ctl, mode == SV_MODE_WAIT_FOR_END ? "wait for end" : "no wait");

    switch (ctl)
    {
    case SV_FUNC_NOP: /* nothing to do? */
      break;

    case SV_FUNC_KILL: /* uninit, kill this thread */
      supervisor->state = SUPERVISOR_STATE_DEAD;
      break;

    default:
      if (ctl > 0 && ctl < g_supervisor_funcs_nb && g_supervisor_funcs[ctl])
      {
        g_supervisor_funcs[ctl] (player, in, out);
        pl_log (player, PLAYER_MSG_VERBOSE,
                MODULE_NAME, "job: %i (completed)", ctl);
      }
      break;
    }

    if (mode == SV_MODE_WAIT_FOR_END)
      sem_post (&supervisor->sem_ctl);

    supervisor_sync_release (supervisor);
  }

  pthread_exit (NULL);
}

/*****************************************************************************/
/*                         Supervisor main functions                         */
/*****************************************************************************/

void
pl_supervisor_callback_in (player_t *player, pthread_t which)
{
  supervisor_t *supervisor;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  supervisor = player->supervisor;
  if (!supervisor)
    return;

  pthread_mutex_lock (&supervisor->mutex_cb);
  supervisor->cb_tid = which;
  supervisor->cb_run = 1;
  pthread_mutex_unlock (&supervisor->mutex_cb);
}

void
pl_supervisor_callback_out (player_t *player)
{
  supervisor_t *supervisor;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  supervisor = player->supervisor;
  if (!supervisor)
    return;

  pthread_mutex_lock (&supervisor->mutex_cb);
  supervisor->cb_run = 0;
  pthread_mutex_unlock (&supervisor->mutex_cb);
}

void
pl_supervisor_send (player_t *player, supervisor_mode_t mode,
                    supervisor_ctl_t ctl, void *in, void *out)
{
  supervisor_send_t *data;
  int res;
  int cb_run;
  pthread_t cb_tid;
  supervisor_t *supervisor;

  if (!player)
    return;

  supervisor = player->supervisor;
  if (!supervisor)
    return;

  pthread_mutex_lock (&supervisor->mutex_cb);
  cb_run = supervisor->cb_run;
  cb_tid = supervisor->cb_tid;
  pthread_mutex_unlock (&supervisor->mutex_cb);

  if (cb_run && pthread_equal (cb_tid, pthread_self ())
      && supervisor->use_sync && mode == SV_MODE_WAIT_FOR_END)
  {
    pl_log (player, PLAYER_MSG_WARNING, MODULE_NAME,
            "change mode to (no wait) because this control (%i) comes "
            "from the public callback", ctl);
    mode = SV_MODE_NO_WAIT;
  }

  if (mode == SV_MODE_NO_WAIT && (in || out))
  {
    pl_log (player, PLAYER_MSG_ERROR, MODULE_NAME,
            "never use no_wait when the function (%i) needs input "
            "and (or) output values", ctl);
    return;
  }

  data = malloc (sizeof (supervisor_send_t));
  if (!data)
    return;

  data->in = in;
  data->out = out;
  data->mode = mode;

  /*
   * If more that one can push in the queue, there is no guarantee that the
   * order of push() is the same as the order of sem_wait().
   * This mutex will ""fix"" the problem with only one item in the queue.
   */
  if (mode == SV_MODE_WAIT_FOR_END)
    pthread_mutex_lock (&supervisor->mutex_sv);

  res = pl_fifo_queue_push (supervisor->queue, ctl, data);
  if (!res && mode == SV_MODE_WAIT_FOR_END)
    sem_wait (&supervisor->sem_ctl);
  else if (res)
  {
    PFREE (data);
    pl_log (player, PLAYER_MSG_ERROR,
            MODULE_NAME, "error on queue? no sense :(");
  }

  if (mode == SV_MODE_WAIT_FOR_END)
    pthread_mutex_unlock (&supervisor->mutex_sv);
}

supervisor_t *
pl_supervisor_new (void)
{
  supervisor_t *supervisor;

  supervisor = PCALLOC (supervisor_t, 1);
  if (!supervisor)
    return NULL;

  supervisor->queue = pl_fifo_queue_new ();
  if (!supervisor->queue)
  {
    PFREE (supervisor);
    return NULL;
  }

  pthread_mutex_init (&supervisor->mutex_sv, NULL);
  sem_init (&supervisor->sem_ctl, 0, 0);

  pthread_cond_init (&supervisor->sync_cond, NULL);
  pthread_mutex_init (&supervisor->sync_mutex, NULL);

  pthread_mutex_init (&supervisor->mutex_cb, NULL);

  return supervisor;
}

supervisor_status_t
pl_supervisor_init (player_t *player, int **run, pthread_t **job,
                    pthread_cond_t **cond, pthread_mutex_t **mutex)
{
  supervisor_t *supervisor;
  pthread_attr_t attr;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return SUPERVISOR_STATUS_ERROR;

  supervisor = player->supervisor;
  if (!supervisor)
    return SUPERVISOR_STATUS_ERROR;

  supervisor->state = SUPERVISOR_STATE_DEAD;

  /* can be used for a sync with an other thread (event handler for example) */
  if (run && job && cond && mutex)
  {
    *run = &supervisor->sync_run;
    *job = &supervisor->sync_job;
    *cond = &supervisor->sync_cond;
    *mutex = &supervisor->sync_mutex;
    supervisor->use_sync = 1;
  }

  pthread_attr_init (&attr);
  pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_JOINABLE);

  if (!pthread_create (&supervisor->th_supervisor,
                       &attr, thread_supervisor, player))
  {
    pthread_attr_destroy (&attr);
    return SUPERVISOR_STATUS_OK;
  }

  pthread_attr_destroy (&attr);
  return SUPERVISOR_STATUS_ERROR;
}

void
pl_supervisor_uninit (player_t *player)
{
  supervisor_t *supervisor;
  void *ret;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  supervisor = player->supervisor;
  if (!supervisor)
    return;

  pl_supervisor_send (player, SV_MODE_NO_WAIT,
                      SV_FUNC_KILL, NULL, NULL);
  pthread_join (supervisor->th_supervisor, &ret);

  pl_fifo_queue_free (supervisor->queue);

  pthread_mutex_destroy (&supervisor->mutex_sv);
  sem_destroy (&supervisor->sem_ctl);

  pthread_cond_destroy (&supervisor->sync_cond);
  pthread_mutex_destroy (&supervisor->sync_mutex);

  pthread_mutex_destroy (&supervisor->mutex_cb);

  PFREE (supervisor);
  player->supervisor = NULL;
}
