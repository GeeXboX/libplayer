/*
 * GeeXboX libplayer python binding module
 * Copyright (C) 2008 Benjamin Zores <ben@geexbox.org>
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

#include "pyplayer.h"

/**************************************************************************
 * Player Module
 **************************************************************************/

#ifndef PlayerMODINIT_FUNC /* declarations for DLL import/export */
#define PlayerMODINIT_FUNC void
#endif

static PyMethodDef player_methods[] = {
  { NULL }  /* Sentinel */
};

PlayerMODINIT_FUNC
initplayer (void)
{
  PyObject* module;

  Player_Type.tp_alloc = PyType_GenericAlloc;
  Mrl_Type.tp_alloc = PyType_GenericAlloc;

  module = Py_InitModule3 ("player", player_methods,
                           "libplayer A/V Multimedia Framework module.");

  if (!module)
    return;

  if (PyType_Ready (&Player_Type) < 0)
    return;
  if (PyType_Ready (&Mrl_Type) < 0)
    return;

  /* Types */
  Py_INCREF (&Player_Type);
  PyModule_AddObject (module, "Player", (PyObject *) &Player_Type);
  Py_INCREF (&Mrl_Type);
  PyModule_AddObject (module, "Mrl", (PyObject *) &Mrl_Type);

  /* Player Constants */
  PyModule_AddIntConstant (module, "PlayerDummy", PLAYER_TYPE_DUMMY);
  PyModule_AddIntConstant (module, "PlayerGstreamer", PLAYER_TYPE_GSTREAMER);
  PyModule_AddIntConstant (module, "PlayerMPlayer", PLAYER_TYPE_MPLAYER);
  PyModule_AddIntConstant (module, "PlayerVLC", PLAYER_TYPE_VLC);
  PyModule_AddIntConstant (module, "PlayerXine", PLAYER_TYPE_XINE);

  PyModule_AddIntConstant (module, "AoNull", PLAYER_AO_NULL);
  PyModule_AddIntConstant (module, "AoAuto", PLAYER_AO_AUTO);
  PyModule_AddIntConstant (module, "AoAlsa", PLAYER_AO_ALSA);
  PyModule_AddIntConstant (module, "AoOSS", PLAYER_AO_OSS);

  PyModule_AddIntConstant (module, "VoNull", PLAYER_VO_NULL);
  PyModule_AddIntConstant (module, "VoAuto", PLAYER_VO_AUTO);
  PyModule_AddIntConstant (module, "VoX11", PLAYER_VO_X11);
  PyModule_AddIntConstant (module, "VoX11SDL", PLAYER_VO_X11_SDL);
  PyModule_AddIntConstant (module, "VoXV", PLAYER_VO_XV);
  PyModule_AddIntConstant (module, "VoGL", PLAYER_VO_GL);
  PyModule_AddIntConstant (module, "VoFB", PLAYER_VO_FB);

  PyModule_AddIntConstant (module, "MsgNone", PLAYER_MSG_NONE);
  PyModule_AddIntConstant (module, "MsgInfo", PLAYER_MSG_INFO);
  PyModule_AddIntConstant (module, "MsgWarning", PLAYER_MSG_WARNING);
  PyModule_AddIntConstant (module, "MsgError", PLAYER_MSG_ERROR);
  PyModule_AddIntConstant (module, "MsgCritical", PLAYER_MSG_CRITICAL);

  /* Mrl Constants */
  PyModule_AddIntConstant (module, "MrlUnknown", MRL_TYPE_UNKNOWN);
  PyModule_AddIntConstant (module, "MrlAudio", MRL_TYPE_AUDIO);
  PyModule_AddIntConstant (module, "MrlVideo", MRL_TYPE_VIDEO);
  PyModule_AddIntConstant (module, "MrlImage", MRL_TYPE_IMAGE);

  PyModule_AddIntConstant (module, "ResUnknown", MRL_RESOURCE_UNKNOWN);
  PyModule_AddIntConstant (module, "ResFifo", MRL_RESOURCE_FIFO);
  PyModule_AddIntConstant (module, "ResFile", MRL_RESOURCE_FILE);
  PyModule_AddIntConstant (module, "ResStdin", MRL_RESOURCE_STDIN);

  PyModule_AddIntConstant (module, "ResCDDA", MRL_RESOURCE_CDDA);
  PyModule_AddIntConstant (module, "ResCDDB", MRL_RESOURCE_CDDB);

  PyModule_AddIntConstant (module, "ResDVD", MRL_RESOURCE_DVD);
  PyModule_AddIntConstant (module, "ResDVDNAV", MRL_RESOURCE_DVDNAV);
  PyModule_AddIntConstant (module, "ResVCD", MRL_RESOURCE_VCD);

  PyModule_AddIntConstant (module, "ResDVB", MRL_RESOURCE_DVB);
  PyModule_AddIntConstant (module, "ResPVR", MRL_RESOURCE_PVR);
  PyModule_AddIntConstant (module, "ResRadio", MRL_RESOURCE_RADIO);
  PyModule_AddIntConstant (module, "ResTV", MRL_RESOURCE_TV);

  PyModule_AddIntConstant (module, "ResFTP", MRL_RESOURCE_FTP);
  PyModule_AddIntConstant (module, "ResHTTP", MRL_RESOURCE_HTTP);
  PyModule_AddIntConstant (module, "ResMMS", MRL_RESOURCE_MMS);
  PyModule_AddIntConstant (module, "ResRTP", MRL_RESOURCE_RTP);
  PyModule_AddIntConstant (module, "ResRTSP", MRL_RESOURCE_RTSP);
  PyModule_AddIntConstant (module, "ResSMB", MRL_RESOURCE_SMB);
  PyModule_AddIntConstant (module, "ResTCP", MRL_RESOURCE_TCP);
  PyModule_AddIntConstant (module, "ResUDP", MRL_RESOURCE_UDP);
  PyModule_AddIntConstant (module, "ResUNSV", MRL_RESOURCE_UNSV);

  PyModule_AddIntConstant (module, "MetaTitle", MRL_METADATA_TITLE);
  PyModule_AddIntConstant (module, "MetaArtist", MRL_METADATA_ARTIST);
  PyModule_AddIntConstant (module, "MetaGenre", MRL_METADATA_GENRE);
  PyModule_AddIntConstant (module, "MetaAlbum", MRL_METADATA_ALBUM);
  PyModule_AddIntConstant (module, "MetaYear", MRL_METADATA_YEAR);
  PyModule_AddIntConstant (module, "MetaTrack", MRL_METADATA_TRACK);
  PyModule_AddIntConstant (module, "MetaComment", MRL_METADATA_COMMENT);
  PyModule_AddIntConstant (module, "MetaDiscID", MRL_METADATA_CD_DISCID);
  PyModule_AddIntConstant (module, "MetaCDTrack", MRL_METADATA_CD_TRACKS);

  PyModule_AddIntConstant (module, "Seekable", MRL_PROPERTY_SEEKABLE);
  PyModule_AddIntConstant (module, "Length", MRL_PROPERTY_LENGTH);
  PyModule_AddIntConstant (module, "AudioBitrate", MRL_PROPERTY_AUDIO_BITRATE);
  PyModule_AddIntConstant (module, "AudioBits", MRL_PROPERTY_AUDIO_BITS);
  PyModule_AddIntConstant (module, "AudioChannels", MRL_PROPERTY_AUDIO_CHANNELS);
  PyModule_AddIntConstant (module, "Samplerate", MRL_PROPERTY_AUDIO_SAMPLERATE);
  PyModule_AddIntConstant (module, "VideoBitrate", MRL_PROPERTY_VIDEO_BITRATE);
  PyModule_AddIntConstant (module, "Width", MRL_PROPERTY_VIDEO_WIDTH);
  PyModule_AddIntConstant (module, "Height", MRL_PROPERTY_VIDEO_HEIGHT);
  PyModule_AddIntConstant (module, "Aspect", MRL_PROPERTY_VIDEO_ASPECT);
  PyModule_AddIntConstant (module, "VideoChannels", MRL_PROPERTY_VIDEO_CHANNELS);
  PyModule_AddIntConstant (module, "VideoStreams", MRL_PROPERTY_VIDEO_STREAMS);
  PyModule_AddIntConstant (module, "FPS", MRL_PROPERTY_VIDEO_FRAMEDURATION);

}

#include "player.c"
#include "mrl.c"
