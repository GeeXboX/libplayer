from distutils.core import setup, Extension
import os

# Get build variables (buildir, srcdir)
top_builddir = os.path.join( '..', '..' )
os.environ['top_builddir'] = top_builddir

# For out-of-tree compilations
srcdir = '.'

def get_pkgconfig():
    pkgconfig=None
    for p in os.environ['PATH'].split(':'):
        pkg = p + '/pkg-config'
        if os.path.exists(pkg):
            pkgconfig=pkg
            break
    if pkgconfig is None:
        print "*** Warning *** Cannot find pkg-config"
    return pkgconfig
   
def get_libplayer_version():
    pkgconfig=get_pkgconfig()
    if pkgconfig is None:
        return ""
    else:
        version=os.popen('%s --version' % pkgconfig, 'r').readline().strip()
        return version
    
def get_cflags():
    pkgconfig=get_pkgconfig()
    if pkgconfig is None:
        return []
    else:
        cflags=os.popen('%s --cflags libplayer' % pkgconfig, 'r').readline().rstrip().split()
        return cflags

def get_ldflags():
    pkgconfig=get_pkgconfig()
    if pkgconfig is None:
        return [ '-lplayer' ]
    else:
	ldflags = []
        ldflags.extend(os.popen('%s --libs libplayer' % pkgconfig, 'r').readline().rstrip().split())
        return ldflags

source_files = [ 'player_module.c' ]

# To compile in a local vlc tree
playerlocal = Extension('player',
		sources = [ os.path.join( srcdir, f ) for f in source_files ],
		include_dirs = [ top_builddir,
			      os.path.join( srcdir, '..', '..', 'src' ),
			      srcdir,
			      '/usr/win32/include' ],
		library_dirs = [ os.path.join( srcdir, '..', '..', 'src' ) ],
		extra_objects = [ ],
                extra_compile_args = get_cflags(),
		extra_link_args = get_ldflags(),
                )

setup (name = 'libplayer Python bindings',
       version = get_libplayer_version(),
       keywords = [ 'libplayer', 'video' ],
       license = "GPL", 
       description = """Python bindings for libplayer.

This module provides bindings for libplayer multimedia framework API.
More info can be found on http://libplayer.geexbox.org/
       """,
       ext_modules = [ playerlocal ])
