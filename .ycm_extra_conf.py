import os

default_cflags = ['-std=c99', '-D_GNU_SOURCE', '-pthread', '-I/usr/include/gdk-pixbuf-2.0', '-I/usr/include/libpng16', '-I/usr/include/glib-2.0', '-I/usr/lib64/glib-2.0/include']
default_cxxflags = ['-std=c++11', '-D_GNU_SOURCE']

def FlagsForFile(filename, **kwargs):
    return {
        flags: default_cflags[:],
        'do_cache': True
    }
