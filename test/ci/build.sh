#!/bin/sh

set -e # Stop on first error
set -x # Print commands

./autogen.sh
# ----------------------------------------------------------------------
v=with-html-tests
mkdir build-$v install-$v
cd build-$v
../configure --prefix=$(readlink -f ../install-$v) --enable-html-tests
make
make install
DILLOBINDIR=$(readlink -f ../install-$v/bin) make check
# The source archive includes the pinned QuickJS and Lexbor dependencies.
make dist-gzip
size=$(stat -c %s dillo-*.tar.gz)
limit=$((16 * 1024 * 1024))
echo "Source archive size: $size / $limit bytes"
test "$size" -lt "$limit"
cd ..
rm -rf build-$v install-$v
# ----------------------------------------------------------------------
v=distcheck
mkdir build-$v install-$v
cd build-$v
../configure --prefix=$(readlink -f ../install-$v) --enable-html-tests
make distcheck DISTCHECK_CONFIGURE_FLAGS=--enable-html-tests
cd ..
rm -rf build-$v install-$v
# ----------------------------------------------------------------------
v=asan
mkdir build-$v install-$v
cd build-$v
../configure CFLAGS="-fsanitize=address" CXXFLAGS="-fsanitize=address" \
  --prefix=$(readlink -f ../install-$v) --enable-html-tests
make
make install
DILLOBINDIR=$(readlink -f ../install-$v/bin) make check
cd ..
rm -rf build-$v install-$v
# ----------------------------------------------------------------------
exit 0
