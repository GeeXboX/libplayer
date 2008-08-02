#!/usr/bin/env python

import sys, time, player

print "Welcome to libplayer Python bindings example"
print "  See how easy it is to build a media player ..."

if not sys.argv[1:]:
    print "!! You must provide a media URI !!"
    sys.exit(1)

p = player.Player(player.PlayerMPlayer,
                  player.AoAlsa, player.VoXV, player.MsgError)

m = p.mrl_new_file(sys.argv[1])
p.set_mrl(m)
p.play()

t = m.get_type()
media_type = ( "Unknown", "Audio", "Video", "Image" )

seek = "no"
if m.get_prop(player.Seekable) == 1:
    seek =  "yes"

print " "
print "Playing " + sys.argv[1]
print " "
print " *** Media Properties ***"
print "   Type: " +  media_type[t] + " stream"
print "   Size: " + str(m.get_size() / 1024 / 1024) + " MB"
print "   Length: " + str(m.get_prop(player.Length) / 1000) + " seconds"
print "   Is Seekable: " + seek

print "   Audio: " + str(m.get_acodec()) \
      + ", " + str(m.get_prop(player.AudioBits)) + " bits" \
      + " at " + str(m.get_prop(player.Samplerate) / 1000.0) + " KHz" \
      + ", " + str(m.get_prop(player.AudioChannels)) + " channels" \
      + ", " + str(m.get_prop(player.AudioBitrate) / 1000) + " kbps"

if t == player.MrlVideo:
    print "   Video: " + str(m.get_vcodec()) \
          + ", " + str(m.get_prop(player.Width)) + "x" + str(m.get_prop(player.Height)) \
          + ", " + str(m.get_prop(player.VideoBitrate) / 1000) + " kbps" \
          + ", " + str(m.get_prop(player.FPS) / 90000.0) + " fps"

print " "
print " *** Media Metadata ***"
print "   Title: " + str(m.get_meta(player.MetaTitle))
print "   Artist: " + str(m.get_meta(player.MetaArtist))
print "   Genre: " + str(m.get_meta(player.MetaGenre))
print "   Album: " + str(m.get_meta(player.MetaAlbum))
print "   Year: " + str(m.get_meta(player.MetaYear))
print "   Track Number: " + str(m.get_meta(player.MetaTrack))
print "   Description: " + str(m.get_meta(player.MetaComment))

time.sleep(100)
