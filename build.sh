#!/bin/bash

: ${CC=gcc}
: ${CXX=g++}
: ${MAKE=make}
: ${BIN=libencoding.so}
: ${CMAKE_FLAGS=""}
: ${CONFIGURE_FLAGS=""}

 # We specifically rename this and LDFLAGS, because exotic build environments export these to subprocesses.
COMPILE_FLAGS="$CFLAGS -I`pwd`/lib/prefix/include -I`pwd`/lib/prefix/include/uchardet -I`pwd`/lib/lite-xl/resources/include -fPIC"
LINK_FLAGS="$LDFLAGS -lm -L`pwd`/lib/prefix/lib -L`pwd`/lib/prefix/lib64"  

[[ "$@" == "clean" ]] && rm -rf lib/prefix lib/libiconv/build lib/uchardet/build $BIN && exit 0

if [[ " $@" != *" -O"* ]]; then
  [[ " $@" != *" -g"* ]] && COMPILE_FLAGS="$COMPILE_FLAGS -O3" || COMPILE_FLAGS="$COMPILE_FLAGS -O0"
fi

# Build supporting libraries if we don't explicitly link them in some way.
if [[ "$@" != "-luchardet" ]]; then
  [ ! -e "lib/uchardet/build" ] && cd lib/uchardet && mkdir build && cd build &&  cmake .. $CMAKE_FLAGS -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=`pwd`/../../prefix && $MAKE && $MAKE install && cd ../../../
  LINK_FLAGS="$LINK_FLAGS -luchardet -lstdc++"
fi
if [[ "$@" != "-liconv" ]]; then
  [ ! -e "lib/libiconv/build" ] && cd lib/libiconv && mkdir build && cd build && ../configure --enable-static=yes --without-libiconv-prefix --without-libintl-prefix --disable-shared --prefix `pwd`/../../prefix $CONFIGURE_FLAGS && $MAKE && $MAKE install-lib && cd ../../../
  LINK_FLAGS="$LINK_FLAGS -liconv"
fi

$CC -shared -o $BIN $COMPILE_FLAGS -fPIC src/encoding.c $LINK_FLAGS  $@
