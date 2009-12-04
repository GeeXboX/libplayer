Always build with:
LDFLAGS_APPEND="-Lsrc -lplayer" dpkg-buildpackage -rfakeroot -b -us -uc -nc -tc
