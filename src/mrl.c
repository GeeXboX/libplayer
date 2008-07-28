/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006-2008 Benjamin Zores <ben@geexbox.org>
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
#include <string.h>
#include <inttypes.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "supervisor.h"

#define MODULE_NAME "mrl"

/*****************************************************************************/
/*                      MRL Public Reentrant functions                       */
/*****************************************************************************/

void
mrl_free (mrl_t *mrl, int recursive)
{
  if (!mrl)
    return;

  if (mrl->subs)
  {
    char **sub = mrl->subs;
    while (*sub)
    {
      free (*sub);
      (*sub)++;
    }
    free (mrl->subs);
  }

  if (mrl->prop)
    mrl_properties_free (mrl->prop);
  if (mrl->meta)
    mrl_metadata_free (mrl->meta, mrl->resource);

  if (mrl->priv)
  {
    switch (mrl->resource)
    {
    case MRL_RESOURCE_FIFO:
    case MRL_RESOURCE_FILE:
    case MRL_RESOURCE_STDIN:
      mrl_resource_local_free (mrl->priv);
      break;

    case MRL_RESOURCE_CDDA:
    case MRL_RESOURCE_CDDB:
      mrl_resource_cd_free (mrl->priv);
      break;

    case MRL_RESOURCE_DVD:
    case MRL_RESOURCE_DVDNAV:
    case MRL_RESOURCE_VCD:
      mrl_resource_videodisc_free (mrl->priv);
      break;

    case MRL_RESOURCE_DVB:
    case MRL_RESOURCE_PVR:
    case MRL_RESOURCE_RADIO:
    case MRL_RESOURCE_TV:
      mrl_resource_tv_free (mrl->priv);
      break;

    case MRL_RESOURCE_FTP: 
    case MRL_RESOURCE_HTTP:
    case MRL_RESOURCE_MMS:
    case MRL_RESOURCE_RTP:
    case MRL_RESOURCE_RTSP:
    case MRL_RESOURCE_SMB:
    case MRL_RESOURCE_TCP:
    case MRL_RESOURCE_UDP:
    case MRL_RESOURCE_UNSV:
      mrl_resource_network_free (mrl->priv);
      break;

    default:
      break;
    }
    free (mrl->priv);
  }

  if (recursive && mrl->next)
    mrl_free (mrl->next, 1);

  free (mrl);
}

void
mrl_list_free (mrl_t *mrl)
{
  if (!mrl)
    return;

  /* go to the very begining of the playlist in case of recursive free() */
  while (mrl->prev)
    mrl = mrl->prev;

  mrl_free (mrl, 1);
}

static int
get_list_length (void *list)
{
  void **l = list;
  int n = 0;
  while (*l++)
    n++;
  return n;
}

void
mrl_add_subtitle (mrl_t *mrl, char *subtitle)
{
  char **subs;
  int n;

  if (!mrl || !subtitle)
    return;

  subs = mrl->subs;
  n = get_list_length (subs) + 1;
  subs = realloc (subs, (n + 1) * sizeof (*subs));
  subs[n] = NULL;
  subs[n - 1] = strdup (subtitle);
}

/*****************************************************************************/
/*                  MRL Public Multi-Threads Safe functions                  */
/*****************************************************************************/

uint32_t
mrl_get_property (player_t *player, mrl_t *mrl, mrl_properties_type_t p)
{
  supervisor_data_mrl_t in;
  uint32_t out;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return 0;

  in.mrl = mrl;
  in.value = p;

  supervisor_send (player, SV_MODE_WAIT_FOR_END,
                   SV_FUNC_MRL_GET_PROPERTY, &in, &out);

  return out;
}

char *
mrl_get_audio_codec (player_t *player, mrl_t *mrl)
{
  char *out;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return NULL;

  supervisor_send (player, SV_MODE_WAIT_FOR_END,
                   SV_FUNC_MRL_GET_AO_CODEC, mrl, &out);

  return out;
}

char *
mrl_get_video_codec (player_t *player, mrl_t *mrl)
{
  char *out;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return NULL;

  supervisor_send (player, SV_MODE_WAIT_FOR_END,
                   SV_FUNC_MRL_GET_VO_CODEC, mrl, &out);

  return out;
}

off_t
mrl_get_size (player_t *player, mrl_t *mrl)
{
  off_t out;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return 0;

  supervisor_send (player, SV_MODE_WAIT_FOR_END,
                   SV_FUNC_MRL_GET_SIZE, mrl, &out);

  return out;
}

char *
mrl_get_metadata (player_t *player, mrl_t *mrl, mrl_metadata_type_t m)
{
  supervisor_data_mrl_t in;
  char *out;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return NULL;

  in.mrl = mrl;
  in.value = m;

  supervisor_send (player, SV_MODE_WAIT_FOR_END,
                   SV_FUNC_MRL_GET_METADATA, &in, &out);

  return out;
}

char *
mrl_get_metadata_cd_track (player_t *player,
                           mrl_t *mrl, int trackid, uint32_t *length)
{
  supervisor_data_mrl_t in;
  supervisor_data_out_metadata_cd_t out;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return NULL;

  in.mrl = mrl;
  in.value = trackid;

  supervisor_send (player, SV_MODE_WAIT_FOR_END,
                   SV_FUNC_MRL_GET_METADATA_CD_TRACK, &in, &out);

  if (length)
    *length = out.length;

  return out.name;
}

uint32_t
mrl_get_metadata_cd (player_t *player, mrl_t *mrl, mrl_metadata_cd_type_t m)
{
  supervisor_data_mrl_t in;
  uint32_t out;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return 0;

  in.mrl = mrl;
  in.value = m;

  supervisor_send (player, SV_MODE_WAIT_FOR_END,
                   SV_FUNC_MRL_GET_METADATA_CD, &in, &out);

  return out;
}

mrl_t *
mrl_new (player_t *player, mrl_resource_t res, void *args)
{
  supervisor_data_args_t in;
  mrl_t *out;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player || !args)
    return NULL;

  in.res = res;
  in.args = args;

  supervisor_send (player, SV_MODE_WAIT_FOR_END,
                   SV_FUNC_MRL_NEW, &in, &out);

  return out;
}

mrl_type_t
mrl_get_type (player_t *player, mrl_t *mrl)
{
  mrl_type_t out;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return MRL_TYPE_UNKNOWN;

  supervisor_send (player, SV_MODE_WAIT_FOR_END,
                   SV_FUNC_MRL_GET_TYPE, mrl, &out);

  return out;
}

mrl_resource_t
mrl_get_resource (player_t *player, mrl_t *mrl)
{
  mrl_resource_t out;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return MRL_RESOURCE_UNKNOWN;

  supervisor_send (player, SV_MODE_WAIT_FOR_END,
                   SV_FUNC_MRL_GET_RESOURCE, mrl, &out);

  return out;
}
