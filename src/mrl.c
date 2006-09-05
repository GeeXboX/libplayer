/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006 Benjamin Zores <ben@geexbox.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"

#define MODULE_NAME "player"

mrl_properties_audio_t *
mrl_properties_audio_new (void)
{
  mrl_properties_audio_t *audio;

  audio = (mrl_properties_audio_t *)
    malloc (sizeof (mrl_properties_audio_t));
  audio->codec = NULL;
  audio->bitrate = 0;
  audio->bits = 0;
  audio->channels = 0;
  audio->samplerate = 0;

  return audio;
}

void
mrl_properties_audio_free (mrl_properties_audio_t *audio)
{
  if (!audio)
    return;

  if (audio->codec)
    free (audio->codec);
  free (audio);
}

mrl_properties_video_t *
mrl_properties_video_new (void)
{
  mrl_properties_video_t *video;

  video = (mrl_properties_video_t *)
    malloc (sizeof (mrl_properties_video_t));
  video->codec = NULL;
  video->bitrate = 0;
  video->width = 0;
  video->height = 0;
  video->channels = 0;
  video->streams = 0;

  return video;
}

void
mrl_properties_video_free (mrl_properties_video_t *video)
{
  if (!video)
    return;

  if (video->codec)
    free (video->codec);
  free (video);
}

mrl_properties_t *
mrl_properties_new (void)
{
  mrl_properties_t *prop;

  prop = (mrl_properties_t *)
    malloc (sizeof (mrl_properties_t));
  prop->size = 0;
  prop->seekable = 0;
  prop->audio = NULL;
  prop->video = NULL;

  return prop;
}

void
mrl_properties_free (mrl_properties_t *prop)
{
  if (!prop)
    return;

  if (prop->audio)
    mrl_properties_audio_free (prop->audio);
  if (prop->video)
    mrl_properties_video_free (prop->video);
  free (prop);
}

mrl_metadata_t *
mrl_metadata_new (void)
{
  mrl_metadata_t *meta;

  meta = (mrl_metadata_t *) malloc (sizeof (mrl_metadata_t));
  meta->title = NULL;
  meta->artist = NULL;
  meta->genre = NULL;
  meta->album = NULL;
  meta->year = NULL;
  meta->track = NULL;

  return meta;
}

void
mrl_metadata_free (mrl_metadata_t *meta)
{
  if (!meta)
    return;

  if (meta->title)
    free (meta->title);
  if (meta->artist)
    free (meta->artist);
  if (meta->genre)
    free (meta->genre);
  if (meta->album)
    free (meta->album);
  if (meta->year)
    free (meta->year);
  if (meta->track)
    free (meta->track);
  free (meta);
}

static mrl_t *
mrl_new (player_t *player, char *name,
         player_mrl_type_t type, char *cover)
{
  mrl_t *mrl = NULL;

  /* check for minimal requirements */
  if (!player || !name)
    return NULL;
  
  mrl = (mrl_t *) malloc (sizeof (mrl_t));
  mrl->name = strdup (name);
  mrl->cover = cover ? strdup (cover) : NULL;
  mrl->type = type;
  mrl->prop = NULL;
  mrl->meta = NULL;
  mrl->prev = NULL;
  mrl->next = NULL;

  return mrl;
}

static void
mrl_free (mrl_t *mrl, int recursive)
{
  if (!mrl)
    return;
  
  if (mrl->name)
    free (mrl->name);
  if (mrl->cover)
    free (mrl->cover);

  if (mrl->prop)
    mrl_properties_free (mrl->prop);
  if (mrl->meta)
    mrl_metadata_free (mrl->meta);
  
  if (recursive && mrl->next)
    mrl_free (mrl->next, 1);
    
  free (mrl);
}

void
mrl_list_free (mrl_t *mrl)
{
  /* go to the very begining of the playlist in case of recursive free() */
  while (mrl->prev)
    mrl = mrl->prev;

  mrl_free (mrl, 1);
}

void
player_mrl_append (player_t *player,
                   char *location, player_mrl_type_t type,
                   char *cover, player_add_mrl_t when)
{
  mrl_t *mrl = NULL;

  plog (MODULE_NAME, "player_mrl_append");
    
  if (!player || !location)
    return;

  mrl = mrl_new (player, location, type, cover);
  if (!mrl)
    return;
  
  /* create/expand the playlist */
  if (!player->mrl) /* empty list */
    player->mrl = mrl;
  else /* create double-linked playlist, appending new MRL to the bottom */
  {
    mrl_t *list;

    list = player->mrl;
    while (list->next)
      list = list->next;
    list->next = mrl;
    mrl->prev = list;
  }

  /* play it now ? */
  if (when == PLAYER_ADD_MRL_NOW)
  {
    player_playback_stop (player);
    player->mrl = mrl;
    player_playback_start (player);
  }
}
   
void
player_mrl_previous (player_t *player)
{
  mrl_t *mrl;

  plog (MODULE_NAME, "player_mrl_previous");
  
  if (!player)
    return;

  mrl = player->mrl;
  if (!mrl || !mrl->prev)
    return;

  player_playback_stop (player);
  player->mrl = mrl->prev;
  player_playback_start (player);
}

void
player_mrl_next (player_t *player)
{
  mrl_t *mrl;

  plog (MODULE_NAME, "player_mrl_next");
  
  if (!player)
    return;

  mrl = player->mrl;
  if (!mrl || !mrl->next)
    return;

  player_playback_stop (player);
  player->mrl = mrl->next;
  player_playback_start (player);
}

void
player_mrl_get_properties (player_t *player, mrl_t *mrl)
{
  plog (MODULE_NAME, "player_mrl_get_properties");
  
  if (!player || !mrl)
    return;

  if (mrl->prop) /* already retrieved */
    return;

  mrl->prop = mrl_properties_new ();
  
  /* player specific init */
  if (player->funcs->mrl_get_props)
    player->funcs->mrl_get_props (player, mrl);
}

void
player_mrl_get_metadata (player_t *player, mrl_t *mrl)
{
  plog (MODULE_NAME, "player_mrl_get_metadata");
  
  if (!player || !mrl)
    return;

  if (mrl->meta) /* already retrieved */
    return;

  mrl->meta = mrl_metadata_new ();
  
  /* player specific init */
  if (player->funcs->mrl_get_meta)
    player->funcs->mrl_get_meta (player, mrl);
}
