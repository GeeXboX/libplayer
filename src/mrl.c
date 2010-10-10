/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006-2008 Benjamin Zores <ben@geexbox.org>
 * Copyright (C) 2007-2008 Mathieu Schroeter <mathieu@schroetersa.ch>
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
#include <string.h>
#include <inttypes.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "supervisor.h"

#define MODULE_NAME "mrl"

/*****************************************************************************/
/*                  MRL Public Multi-Threads Safe functions                  */
/*****************************************************************************/

void
mrl_free (player_t *player, mrl_t *mrl)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player || !mrl)
    return;

  pl_supervisor_send (player,
                      SV_MODE_WAIT_FOR_END, SV_FUNC_MRL_FREE, mrl, NULL);
}

uint32_t
mrl_get_property (player_t *player, mrl_t *mrl, mrl_properties_type_t p)
{
  supervisor_data_mrl_t in;
  uint32_t out = 0;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return 0;

  in.mrl   = mrl;
  in.value = p;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_MRL_GET_PROPERTY, &in, &out);

  return out;
}

char *
mrl_get_audio_codec (player_t *player, mrl_t *mrl)
{
  char *out = NULL;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return NULL;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_MRL_GET_AO_CODEC, mrl, &out);

  return out;
}

char *
mrl_get_video_codec (player_t *player, mrl_t *mrl)
{
  char *out = NULL;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return NULL;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_MRL_GET_VO_CODEC, mrl, &out);

  return out;
}

off_t
mrl_get_size (player_t *player, mrl_t *mrl)
{
  off_t out = 0;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return 0;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_MRL_GET_SIZE, mrl, &out);

  return out;
}

char *
mrl_get_metadata (player_t *player, mrl_t *mrl, mrl_metadata_type_t m)
{
  supervisor_data_mrl_t in;
  char *out = NULL;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return NULL;

  in.mrl   = mrl;
  in.value = m;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_MRL_GET_METADATA, &in, &out);

  return out;
}

char *
mrl_get_metadata_cd_track (player_t *player,
                           mrl_t *mrl, int trackid, uint32_t *length)
{
  supervisor_data_mrl_t in;
  supervisor_data_out_metadata_cd_t out;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return NULL;

  in.mrl   = mrl;
  in.value = trackid;

  memset (&out, 0, sizeof (supervisor_data_out_metadata_cd_t));
  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_MRL_GET_METADATA_CD_TRACK, &in, &out);

  if (length)
    *length = out.length;

  return out.name;
}

uint32_t
mrl_get_metadata_cd (player_t *player, mrl_t *mrl, mrl_metadata_cd_type_t m)
{
  supervisor_data_mrl_t in;
  uint32_t out = 0;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return 0;

  in.mrl   = mrl;
  in.value = m;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_MRL_GET_METADATA_CD, &in, &out);

  return out;
}

uint32_t
mrl_get_metadata_dvd_title (player_t *player,
                            mrl_t *mrl, int titleid, mrl_metadata_dvd_type_t m)
{
  supervisor_data_in_metadata_dvd_t in;
  uint32_t out = 0;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return 0;

  in.mrl  = mrl;
  in.id   = titleid;
  in.type = m;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_MRL_GET_METADATA_DVD_TITLE, &in, &out);

  return out;
}

char *
mrl_get_metadata_dvd (player_t *player, mrl_t *mrl, uint8_t *titles)
{
  supervisor_data_out_metadata_dvd_t out;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return NULL;

  memset (&out, 0, sizeof (supervisor_data_out_metadata_dvd_t));
  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_MRL_GET_METADATA_DVD, mrl, &out);

  if (titles)
    *titles = out.titles;

  return out.volumeid;
}

int
mrl_get_metadata_subtitle (player_t *player, mrl_t *mrl, int pos,
                           uint32_t *id, char **name, char **lang)
{
  supervisor_data_mrl_t in;
  supervisor_data_out_metadata_t out;

  if (!player)
    return 0;

  in.mrl   = mrl;
  in.value = pos;

  memset (&out, 0, sizeof (supervisor_data_out_metadata_t));
  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_MRL_GET_METADATA_SUBTITLE, &in, &out);

  if (id)
    *id = out.id;

  if (name)
    *name = out.name;
  else if (out.name)
    PFREE (out.name);

  if (lang)
    *lang = out.lang;
  else if (out.lang)
    PFREE (out.lang);

  return out.ret;
}

uint32_t
mrl_get_metadata_subtitle_nb (player_t *player, mrl_t *mrl)
{
  uint32_t out = 0;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return 0;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_MRL_GET_METADATA_SUBTITLE_NB, mrl, &out);

  return out;
}

int
mrl_get_metadata_audio (player_t *player, mrl_t *mrl, int pos,
                        uint32_t *id, char **name, char **lang)
{
  supervisor_data_mrl_t in;
  supervisor_data_out_metadata_t out;

  if (!player)
    return 0;

  in.mrl   = mrl;
  in.value = pos;

  memset (&out, 0, sizeof (supervisor_data_out_metadata_t));
  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_MRL_GET_METADATA_AUDIO, &in, &out);

  if (id)
    *id = out.id;

  if (name)
    *name = out.name;
  else if (out.name)
    PFREE (out.name);

  if (lang)
    *lang = out.lang;
  else if (out.lang)
    PFREE (out.lang);

  return out.ret;
}

uint32_t
mrl_get_metadata_audio_nb (player_t *player, mrl_t *mrl)
{
  uint32_t out = 0;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return 0;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_MRL_GET_METADATA_AUDIO_NB, mrl, &out);

  return out;
}

mrl_type_t
mrl_get_type (player_t *player, mrl_t *mrl)
{
  mrl_type_t out = MRL_TYPE_UNKNOWN;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return MRL_TYPE_UNKNOWN;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_MRL_GET_TYPE, mrl, &out);

  return out;
}

mrl_resource_t
mrl_get_resource (player_t *player, mrl_t *mrl)
{
  mrl_resource_t out = MRL_RESOURCE_UNKNOWN;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return MRL_RESOURCE_UNKNOWN;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_MRL_GET_RESOURCE, mrl, &out);

  return out;
}

void
mrl_add_subtitle (player_t *player, mrl_t *mrl, char *subtitle)
{
  supervisor_data_sub_t in;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  in.mrl = mrl;
  in.sub = subtitle;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_MRL_ADD_SUBTITLE, &in, NULL);
}

mrl_t *
mrl_new (player_t *player, mrl_resource_t res, void *args)
{
  supervisor_data_args_t in;
  mrl_t *out = NULL;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player || !args)
    return NULL;

  in.res  = res;
  in.args = args;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_MRL_NEW, &in, &out);

  return out;
}

void
mrl_video_snapshot (player_t *player, mrl_t *mrl,
                    int pos, mrl_snapshot_t t, const char *dst)
{
  supervisor_data_snapshot_t in;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  in.mrl  = mrl;
  in.pos  = pos;
  in.type = t;
  in.dst  = dst;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_MRL_VIDEO_SNAPSHOT, &in, NULL);
}
