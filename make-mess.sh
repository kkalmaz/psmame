#!/bin/sh
make TARGET=mess NOWERROR=1 CROSS_BUILD=1 CROSSBUILD=1 TARGETOS=linux OSD=osdmini NO_X11=1 NO_OPENGL=1 -f makefile.ps3 -j3


