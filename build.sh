#!/usr/bin/bash

BIN=libencoding.a
COMPILE_FLAGS="$CFLAGS -I`pwd`/lib/prefix/include -I`pwd`/lib/prefix/include/uchardet -I`pwd`/lib/lite-xl/resources/include -fPIC" # We specifically rename this and LDFLAGS, because exotic build environments export these to subprocesses.
LINK_FLAGS="$LDFLAGS -lm -L`pwd`/lib/prefix/lib -L`pwd`/lib/prefix/lib64"   # And ideally we don't want to mess with the underlying build processes, unless we're explicit about it.
CMAKE_DEFAULT_FLAGS="-DCMAKE_POSITION_INDEPENDENT_CODE=ON"

[[ "$@" == "clean" ]] && rm -rf lib/prefix lib/libiconv/build lib/uchardet/build && exit 0

if [[ " $@" != *" -O"* ]]; then
  [[ " $@" != *" -g"* ]] && COMPILE_FLAGS="$COMPILE_FLAGS -O3" || COMPILE_FLAGS="$COMPILE_FLAGS -O0"
fi

if [[ "$@" != "-liconv" ]]; then
  [ ! -e "lib/libiconv/build" ] && cd lib/libiconv && mkdir build && cd build && ../configure --enable-static=yes --disable-shared --prefix `pwd`/../../prefix && make && make install && cd ../../../
  LINK_FLAGS="$LINK_FLAGS -liconv -lcharset"
fi
if [[ "$@" != "-luchardet" ]]; then
  [ ! -e "lib/uchardet/build" ] && cd lib/uchardet && mkdir build && cd build && cmake .. $CMAKE_DEFAULT_FLAGS -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=`pwd`/../../prefix && make && make install && cd ../../../
  LINK_FLAGS="$LINK_FLAGS -luchardet"
fi

$CC -o $BIN $COMPILE_FLAGS -fPIC src/encoding.c $LINK_FLAGS -shared
