#!/usr/bin/bash

BIN=libencoding.a
SRCS="src/*.c"
COMPILE_FLAGS="$CFLAGS -I`pwd`/lib/prefix/include" # We specifically rename this and LDFLAGS, because exotic build environments export these to subprocesses.
LINK_FLAGS="$LDFLAGS -lm -L`pwd`/lib/prefix/lib -L`pwd`/lib/prefix/lib64"   # And ideally we don't want to mess with the underlying build processes, unless we're explicit about it.

[[ "$@" == "clean" ]] && rm -rf lib/prefix && exit 0

if [[ " $@" != *" -O"* ]]; then
  [[ " $@" != *" -g"* ]] && COMPILE_FLAGS="$COMPILE_FLAGS -O3" || COMPILE_FLAGS="$COMPILE_FLAGS -O0"
fi

if [[ "$@" != "-liconv" ]]; then
  cd lib/libiconv && ./configure --enable-static=yes --prefix `pwd`/../prefix && make && make install
fi
if [[ "$@" != "-luchardet" ]]; then
  cd lib/libuchardet && mkdir build && cd build && cmake .. --CMAKE_INSTALL_PREFIX=`pwd`/../../prefix && make && make install
fi

$CC -o $BIN $COMPILE_FLAGS $SRCS $LINK_FLAGS
