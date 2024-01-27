#!/bin/bash

# Script to prepare RetroShare Android package building toolchain
#
# Copyright (C) 2016-2022  Gioacchino Mazzurco <gio@eigenlab.org>
# Copyright (C) 2020-2022  Asociación Civil Altermundi <info@altermundi.net>
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License as published by the
# Free Software Foundation, version 3.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.
# See the GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License along
# with this program. If not, see <https://www.gnu.org/licenses/>
#
# SPDX-FileCopyrightText: Retroshare Team <contact@retroshare.cc>
# SPDX-License-Identifier: AGPL-3.0-only

## In theory if something goes unexpected fail early to detect and debug easier,
## in practice this is unreliable once `while` loop and functions are involved
## so do not forget to do proper error handling
set -o errexit
set -o errtrace

## Define default value for variable, take two arguments, $1 variable name,
## $2 default variable value, if the variable is not already define define it
## with default value.
function define_default_value()
{
	VAR_NAME="${1}"
	DEFAULT_VALUE="${2}"

	[ -z "${!VAR_NAME}" ] && export ${VAR_NAME}="${DEFAULT_VALUE}" || true
}

## You are supposed to provide the following variables according to your system setup
define_default_value ANDROID_SDK_PATH "/opt/android-sdk/"
define_default_value ANDROID_NDK_PATH "/opt/android-ndk/"
define_default_value ANDROID_NDK_ARCH "arm"
define_default_value ANDROID_PLATFORM_VER "16"
define_default_value NATIVE_LIBS_TOOLCHAIN_PATH "${HOME}/Builds/android-toolchains/retroshare-android-${ANDROID_PLATFORM_VER}-${ANDROID_NDK_ARCH}/"
define_default_value HOST_NUM_CPU $(nproc)

define_default_value ANDROID_CMD_TOOLS_VERSION "8092744"
define_default_value ANDROID_CMD_TOOLS_SHA256 d71f75333d79c9c6ef5c39d3456c6c58c613de30e6a751ea0dbd433e8f8b9cbf
define_default_value ANDROID_SDK_VERSION "29.0.3"
define_default_value ANDROID_NDK_VERSION "21.0.6113669"

define_default_value BZIP2_SOURCE_VERSION "1.0.6"
define_default_value BZIP2_SOURCE_SHA256 a2848f34fcd5d6cf47def00461fcb528a0484d8edef8208d6d2e2909dc61d9cd

define_default_value OPENSSL_SOURCE_VERSION "1.1.1n"
define_default_value OPENSSL_SOURCE_SHA256 40dceb51a4f6a5275bde0e6bf20ef4b91bfc32ed57c0552e2e8e15463372b17a

define_default_value SQLITE_SOURCE_YEAR "2018"
define_default_value SQLITE_SOURCE_VERSION "3250200"
define_default_value SQLITE_SOURCE_SHA256 da9a1484423d524d3ac793af518cdf870c8255d209e369bd6a193e9f9d0e3181

define_default_value SQLCIPHER_SOURCE_VERSION "4.4.3"
define_default_value SQLCIPHER_SOURCE_SHA256 b8df69b998c042ce7f8a99f07cf11f45dfebe51110ef92de95f1728358853133

define_default_value LIBUPNP_SOURCE_VERSION "1.8.4"
define_default_value LIBUPNP_SOURCE_SHA256 976c3e4555604cdd8391ed2f359c08c9dead3b6bf131c24ce78e64d6669af2ed

## TODO: Report that 4.8 doesn't compile for Android and test newer versions
define_default_value RESTBED_SOURCE_REPO "https://github.com/Corvusoft/restbed.git"
define_default_value RESTBED_SOURCE_VERSION f74f9329dac82e662c1d570b7cd72c192b729eb4

define_default_value UDP_DISCOVERY_CPP_SOURCE "https://github.com/truvorskameikin/udp-discovery-cpp.git"
define_default_value UDP_DISCOVERY_CPP_VERSION "develop"

define_default_value XAPIAN_SOURCE_VERSION "1.4.7"
define_default_value XAPIAN_SOURCE_SHA256 13f08a0b649c7afa804fa0e85678d693fd6069dd394c9b9e7d41973d74a3b5d3

define_default_value RAPIDJSON_SOURCE_VERSION "1.1.0"
define_default_value RAPIDJSON_SOURCE_SHA256 bf7ced29704a1e696fbccf2a2b4ea068e7774fa37f6d7dd4039d0787f8bed98e

define_default_value MINIUPNPC_SOURCE_VERSION "2.1.20190625"
define_default_value MINIUPNPC_SOURCE_SHA256 8723f5d7fd7970de23635547700878cd29a5c2bb708b5e5475b2d1d2510317fb

# zlib and libpng versions walks toghether
define_default_value ZLIB_SOURCE_VERSION "1.2.11"
define_default_value ZLIB_SOURCE_SHA256 c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1

define_default_value LIBPNG_SOURCE_VERSION "1.6.37"
define_default_value LIBPNG_SOURCE_SHA256 505e70834d35383537b6491e7ae8641f1a4bed1876dbfe361201fc80868d88ca

define_default_value LIBJPEG_SOURCE_VERSION "9e"
define_default_value LIBJPEG_SOURCE_SHA256 4077d6a6a75aeb01884f708919d25934c93305e49f7e3f36db9129320e6f4f3d

define_default_value TIFF_SOURCE_VERSION "4.2.0"
define_default_value TIFF_SOURCE_SHA256 eb0484e568ead8fa23b513e9b0041df7e327f4ee2d22db5a533929dfc19633cb

define_default_value CIMG_SOURCE_VERSION "2.9.7"
define_default_value CIMG_SOURCE_SHA256 595dda9718431a123b418fa0db88e248c44590d47d9b1646970fa0503e27fa5c

define_default_value PHASH_SOURCE_REPO "https://github.com/RetroShare/pHash.git"
define_default_value PHASH_SOURCE_VERSION origin/master

define_default_value MVPTREE_SOURCE_REPO "https://github.com/starkdg/mvptree.git"
define_default_value MVPTREE_SOURCE_VERSION origin/master

define_default_value REPORT_DIR "$(pwd)/$(basename ${NATIVE_LIBS_TOOLCHAIN_PATH})_build_report/"

define_default_value RS_SRC_DIR "$(realpath $(dirname $BASH_SOURCE)/../../)"
define_default_value RS_EXTRA_CMAKE_OPTS ""

# Debug or Release we should give support at least at those two builds type
define_default_value TOOLCHAIN_BUILD_TYPE Release

cArch=""
eABI=""

case "${ANDROID_NDK_ARCH}" in
"arm")
	cArch="${ANDROID_NDK_ARCH}"
	eABI="eabi"
	;;
"arm64")
	cArch="aarch64"
	eABI=""
	;;
"x86")
	cArch="i686"
	eABI=""
	;;
"x86_64")
	echo "ANDROID_NDK_ARCH=${ANDROID_NDK_ARCH} not supported yet"
	exit 1
	cArch="??"
	eABI=""
esac

export SYSROOT="${NATIVE_LIBS_TOOLCHAIN_PATH}/sysroot/"
export PREFIX="${SYSROOT}/usr/"
export CC="${NATIVE_LIBS_TOOLCHAIN_PATH}/bin/${cArch}-linux-android${eABI}-clang"
export CXX="${NATIVE_LIBS_TOOLCHAIN_PATH}/bin/${cArch}-linux-android${eABI}-clang++"
export AR="${NATIVE_LIBS_TOOLCHAIN_PATH}/bin/${cArch}-linux-android${eABI}-ar"
export RANLIB="${NATIVE_LIBS_TOOLCHAIN_PATH}/bin/${cArch}-linux-android${eABI}-ranlib"
# More interesting GNU Make variables at http://www.gnu.org/software/make/manual/make.html#Implicit-Variables

# Used to instruct cmake to explicitely ignore host libraries
export HOST_IGNORE_PREFIX="/usr/"

export ARMv7_OPTIMIZATION_FLAGS="-march=armv7-a -mfloat-abi=softfp -mfpu=vfp"


## $1 filename, $2 sha256 hash
function check_sha256()
{
	echo ${2} "${1}" | sha256sum -c &> /dev/null
}

## $1 filename, $2 sha256 hash, $3 url
function verified_download()
{
	FILENAME="$1"
	SHA256="$2"
	URL="$3"

	check_sha256 "${FILENAME}" "${SHA256}" ||
	{
		rm -rf "${FILENAME}"

		wget -O "${FILENAME}" "$URL" ||
		{
			mRet=$?
			echo "Failed downloading ${FILENAME} from $URL"
			return $mRet
		}

		check_sha256 "${FILENAME}" "${SHA256}" ||
		{
			mRet=$?
			echo "SHA256 mismatch for ${FILENAME} from ${URL} expected sha256 ${SHA256} got $(sha256sum ${FILENAME} | awk '{print $1}')"
			return $mRet
		}
	}

	return 0
}

# This function is the result of reading and testing many many stuff be very
# careful editing it
function andro_cmake()
{
# Using android.toolchain.cmake as documented here
# https://developer.android.com/ndk/guides/cmake seens to break more things then
# it fixes :-\

	cmakeProc=""
	case "${ANDROID_NDK_ARCH}" in
	"arm")
		cmakeProc="armv7-a"
		export CFLAGS="$ARMv7_OPTIMIZATION_FLAGS"
		export CXXFLAGS="$ARMv7_OPTIMIZATION_FLAGS"
	;;
	"arm64")
		cmakeProc="aarch64"
	;;
	"x86")
		cmakeProc="i686"
	;;
	"x86_64")
		cmakeProc="x86_64"
	;;
	*)
		echo "Unhandled NDK architecture ${ANDROID_NDK_ARCH}"
		return 1
	;;
	esac

	_hi="$HOST_IGNORE_PREFIX"

	cmakeBuildType=""
	[ "$TOOLCHAIN_BUILD_TYPE" == "" ] ||
		cmakeBuildType="-DCMAKE_BUILD_TYPE=$TOOLCHAIN_BUILD_TYPE"

	cmakeOptimizationsOpt=""
	cmakeDebugOptions=""
	case "$TOOLCHAIN_BUILD_TYPE" in
	"Release"|"RelWithDebInfo")
		cmakeOptimizationsOpt="-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON"
		cmakeDebugOptions="-DRS_SPLIT_DEBUG=ON"
		;;
	esac

	cmake \
		$cmakeBuildType $cmakeCompileOptions $cmakeDebugOptions \
		-DCMAKE_C_COMPILER_AR=$AR \
		-DCMAKE_C_COMPILER_RANLIB=$RANLIB \
		-DCMAKE_SYSTEM_PROCESSOR=$cmakeProc \
		-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
		-DCMAKE_PREFIX_PATH="${PREFIX}" \
		-DCMAKE_SYSTEM_PREFIX_PATH="${PREFIX}" \
		-DCMAKE_INCLUDE_PATH="${PREFIX}/include" \
		-DCMAKE_SYSTEM_INCLUDE_PATH="${PREFIX}/include" \
		-DCMAKE_LIBRARY_PATH="${PREFIX}/lib" \
		-DCMAKE_SYSTEM_LIBRARY_PATH="${PREFIX}/lib" \
		-DCMAKE_INSTALL_PREFIX="${PREFIX}" \
		-DCMAKE_IGNORE_PATH="$_hi/include;$_hi/lib;$_hi/lib64" \
		$@

	# It is probably ok to do not touch CMAKE_PROGRAM_PATH and
	# CMAKE_SYSTEM_PROGRAM_PATH
}

function git_source_get()
{
	sourceDir="$1" ; shift #$1
	sourceRepo="$1"  ; shift  #$2
	sourceVersion="$1"  ; shift  #$3
	# extra paramethers are treated as submodules

	[ -d "$sourceDir" ] &&
	{
		pushd "$sourceDir"
		actUrl="$(git remote get-url origin)"
		[ "$actUrl" != "$sourceRepo" ] && rm -rf "${sourceDir}"
		popd
	} || true

	[ -d $sourceDir ] || git clone "$sourceRepo" "$sourceDir"
	pushd $sourceDir
	
	git fetch --all || return $?
	git reset --hard ${sourceVersion} || return $?

	while [ "$1" != "" ] ; do
		git submodule update --init "$1" || return $?
		pushd "$1"                       || return $?
		git reset --hard                 || return $?
		shift
		popd
	done

	popd
}

declare -A TASK_REGISTER

function task_register()
{
	TASK_REGISTER[$1]=true
}

function task_unregister()
{
	# we may simply wipe them but we could benefit from keeping track of
	# unregistered tasks too
	TASK_REGISTER[$1]=false
}

function task_logfile()
{
	echo "$REPORT_DIR/$1.log"
}

function task_run()
{
	mTask="$1" ; shift

	[ "${TASK_REGISTER[$mTask]}" != "true" ] &&
	{
		echo "Attempt to run not registered task $mTask $@"
		return 1
	}

	logFile="$(task_logfile $mTask)"
	if [ -f "$logFile" ] ; then
		echo "Task $mTask already run more details at $logFile"
	else
		date | tee > "$logFile"
		$mTask $@ |& tee --append "$logFile"
		mRetval="${PIPESTATUS[0]}"
		echo "Task $mTask return ${mRetval} more details at $logFile"
		date | tee --append "$logFile"
		return ${mRetval}
	fi
}

function task_zap()
{
	rm -f "$(task_logfile $1)"
}

task_register install_android_sdk
install_android_sdk()
{
# old SDK manager has been deprecated use new cmdline-tools as per
# https://stackoverflow.com/a/65782803
# https://developer.android.com/studio#command-tools

	tFile="commandlinetools-linux-${ANDROID_CMD_TOOLS_VERSION}_latest.zip"

	verified_download "${tFile}" "${ANDROID_CMD_TOOLS_SHA256}" \
		"https://dl.google.com/android/repository/${tFile}" || return $?

	unzip "${tFile}"  || return $?
	rm -rf "$ANDROID_SDK_PATH"
	CMD_TOOLS_DIR="$ANDROID_SDK_PATH/cmdline-tools/latest/"
	mkdir -p "$CMD_TOOLS_DIR"
	rm -rf "$CMD_TOOLS_DIR"
	mv --verbose cmdline-tools/ "$CMD_TOOLS_DIR"

	ANDROID_SDK_MANAGER="$CMD_TOOLS_DIR/bin/sdkmanager --sdk_root=$ANDROID_SDK_PATH"

	# Install Android SDK
	yes | $ANDROID_SDK_MANAGER --licenses && \
		$ANDROID_SDK_MANAGER --update
	$ANDROID_SDK_MANAGER "platforms;android-$ANDROID_PLATFORM_VER"
	$ANDROID_SDK_MANAGER "build-tools;$ANDROID_SDK_VERSION"
	$ANDROID_SDK_MANAGER "ndk;$ANDROID_NDK_VERSION"
}

## More information available at https://android.googlesource.com/platform/ndk/+/ics-mr0/docs/STANDALONE-TOOLCHAIN.html
task_register bootstrap_toolchain
bootstrap_toolchain()
{
	rm -rf "${NATIVE_LIBS_TOOLCHAIN_PATH}"
	${ANDROID_NDK_PATH}/build/tools/make_standalone_toolchain.py --verbose \
		--arch ${ANDROID_NDK_ARCH} --install-dir ${NATIVE_LIBS_TOOLCHAIN_PATH} \
		--api ${ANDROID_PLATFORM_VER} || return $?

	# Avoid problems with arm64 some libraries installing on lib64
	ln -s "${PREFIX}/lib/" "${PREFIX}/lib64" || return $?
}

## More information available at retroshare://file?name=Android%20Native%20Development%20Kit%20Cookbook.pdf&size=29214468&hash=0123361c1b14366ce36118e82b90faf7c7b1b136
task_register build_bzlib
build_bzlib()
{
	B_dir="bzip2-${BZIP2_SOURCE_VERSION}"
	rm -rf $B_dir

	verified_download $B_dir.tar.gz $BZIP2_SOURCE_SHA256 \
		http://distfiles.gentoo.org/distfiles/bzip2-${BZIP2_SOURCE_VERSION}.tar.gz \
		|| return $?

	tar -xf $B_dir.tar.gz || return $?
	pushd $B_dir || return $?
	sed -i "/^CC=.*/d" Makefile || return $?
	sed -i "/^AR=.*/d" Makefile || return $?
	sed -i "/^RANLIB=.*/d" Makefile || return $?
	sed -i "/^LDFLAGS=.*/d" Makefile || return $?
	sed -i "s/^all: libbz2.a bzip2 bzip2recover test/all: libbz2.a bzip2 bzip2recover/" Makefile \
		|| return $?
	make -j${HOST_NUM_CPU} || return $?
	make install PREFIX=${PREFIX} || return $?
#	sed -i "/^CC=.*/d" Makefile-libbz2_so
#	make -f Makefile-libbz2_so -j${HOST_NUM_CPU}
#	cp libbz2.so.1.0.6 ${SYSROOT}/usr/lib/libbz2.so
	popd
}

## More information available at http://doc.qt.io/qt-5/opensslsupport.html
## The following article might be interesting for future updates
## https://proandroiddev.com/tutorial-compile-openssl-to-1-1-1-for-android-application-87137968fee
task_register build_openssl
build_openssl()
{
	B_dir="openssl-${OPENSSL_SOURCE_VERSION}"
	rm -rf $B_dir

	verified_download $B_dir.tar.gz $OPENSSL_SOURCE_SHA256 \
		https://www.openssl.org/source/$B_dir.tar.gz || return $?

	tar -xf $B_dir.tar.gz || return $?
	pushd $B_dir
## We link openssl statically to avoid android silently sneaking in his own
## version of libssl.so (we noticed this because it had some missing symbol
## that made RS crash), the crash in some android version is only one of the
## possible problems the fact that android insert his own binary libssl.so pose
## non neglegible security concerns.
	oBits="32"
	[[ ${ANDROID_NDK_ARCH} =~ .*64.* ]] && oBits=64
	
	armOptimizationFlags=""
	[[ "${ANDROID_NDK_ARCH}" != "arm" ]] || armOptimizationFlags="$ARMv7_OPTIMIZATION_FLAGS"

	ANDROID_NDK="${ANDROID_NDK_PATH}" PATH="${SYSROOT}/bin/:${PATH}" \
	./Configure linux-generic${oBits} -fPIC $armOptimizationFlags \
		--prefix="${PREFIX}" --openssldir="${SYSROOT}/etc/ssl" || return $?
#	sed -i 's/LIBNAME=$$i LIBVERSION=$(SHLIB_MAJOR).$(SHLIB_MINOR) \\/LIBNAME=$$i \\/g' Makefile
#	sed -i '/LIBCOMPATVERSIONS=";$(SHLIB_VERSION_HISTORY)" \\/d' Makefile

	# Avoid documentation build which is unneded and time consuming
	echo "exit 0; " > util/process_docs.pl
	
	make -j${HOST_NUM_CPU} || return $?
	make install || return $?
	rm -f ${PREFIX}/lib/libssl.so*
	rm -f ${PREFIX}/lib/libcrypto.so*
	popd
}

task_register build_sqlite
build_sqlite()
{
	B_dir="sqlite-autoconf-${SQLITE_SOURCE_VERSION}"
	rm -rf $B_dir

	verified_download $B_dir.tar.gz $SQLITE_SOURCE_SHA256 \
		https://www.sqlite.org/${SQLITE_SOURCE_YEAR}/$B_dir.tar.gz || return $?

	tar -xf $B_dir.tar.gz || return $?
	pushd $B_dir || return $?
	./configure --with-pic --prefix="${PREFIX}" --host=${cArch}-linux || return $?
	make -j${HOST_NUM_CPU} || return $?
	make install || return $?
	rm -f ${PREFIX}/lib/libsqlite3.so*
	popd
}

task_register build_sqlcipher
build_sqlcipher()
{
	task_run build_sqlite

	B_dir="sqlcipher-${SQLCIPHER_SOURCE_VERSION}"
	rm -rf $B_dir

	T_file="${B_dir}.tar.gz"

	verified_download $T_file $SQLCIPHER_SOURCE_SHA256 \
		https://github.com/sqlcipher/sqlcipher/archive/v${SQLCIPHER_SOURCE_VERSION}.tar.gz \
		|| return $?

	tar -xf $T_file || return $?
	pushd $B_dir
#	case "${ANDROID_NDK_ARCH}" in
#	"arm64")
#	# SQLCipher config.sub is outdated and doesn't recognize newer architectures
#		rm config.sub
#		autoreconf --verbose --install --force
#		automake --add-missing --copy --force-missing
#	;;
#	esac
	./configure --with-pic --build=$(sh ./config.guess) \
		--host=${cArch}-linux \
		--prefix="${PREFIX}" --with-sysroot="${SYSROOT}" \
		--enable-tempstore=yes \
		--disable-tcl --disable-shared \
		CFLAGS="-DSQLITE_HAS_CODEC" LDFLAGS="${PREFIX}/lib/libcrypto.a" \
		|| return $?
	make -j${HOST_NUM_CPU} || return $?
	make install || return $?
	popd
}

task_register build_libupnp
build_libupnp()
{
	B_dir="pupnp-release-${LIBUPNP_SOURCE_VERSION}"
	B_ext=".tar.gz"
	B_file="${B_dir}${B_ext}"
	rm -rf $B_dir

	verified_download $B_file $LIBUPNP_SOURCE_SHA256 \
		https://github.com/mrjimenez/pupnp/archive/release-${LIBUPNP_SOURCE_VERSION}${B_ext} \
		|| return $?

	tar -xf $B_file || return $?
	pusdh $B_dir || return $?
	./bootstrap || return $?
## libupnp must be configured as static library because if not the linker will
## look for libthreadutils.so.6 at runtime that cannot be packaged on android
## as it supports only libname.so format for libraries, thus resulting in a
## crash at startup.
	./configure --with-pic --enable-static --disable-shared --disable-samples \
		--disable-largefile \
		--prefix="${PREFIX}" --host=${cArch}-linux || return $?
	make -j${HOST_NUM_CPU} || return $?
	make install || return $?
	popd
}

task_register build_rapidjson
build_rapidjson()
{
	B_dir="rapidjson-${RAPIDJSON_SOURCE_VERSION}"
	D_file="${B_dir}.tar.gz"
	verified_download $D_file $RAPIDJSON_SOURCE_SHA256 \
		https://github.com/Tencent/rapidjson/archive/v${RAPIDJSON_SOURCE_VERSION}.tar.gz \
		 || return $?
	tar -xf $D_file || return $?
	cp -r "${B_dir}/include/rapidjson/" "${PREFIX}/include/rapidjson" || return $?
}

task_register build_restbed
build_restbed()
{
	S_dir="restbed"
	B_dir="${S_dir}-build"
	git_source_get "$S_dir" "$RESTBED_SOURCE_REPO" "${RESTBED_SOURCE_VERSION}" \
		"dependency/asio" "dependency/catch" || return $?

	rm -rf "$B_dir"; mkdir "$B_dir"
	pushd "$B_dir"
	andro_cmake -DBUILD_TESTS=OFF -DBUILD_SSL=OFF -B. -H../${S_dir} || return $?
	make -j${HOST_NUM_CPU} || return $?
	make install || return $?
	popd
}

task_register build_udp-discovery-cpp
build_udp-discovery-cpp()
{
	S_dir="udp-discovery-cpp"
	git_source_get "$S_dir" \
		"$UDP_DISCOVERY_CPP_SOURCE" "$UDP_DISCOVERY_CPP_VERSION" || return $?

	B_dir="udp-discovery-cpp-build"
	rm -rf ${B_dir};
	mkdir ${B_dir} || return $?
	pushd ${B_dir} || return $?
	andro_cmake -B. -H../$S_dir || return $?
	make -j${HOST_NUM_CPU} || return $?
	cp libudp-discovery.a "${PREFIX}/lib/" || return $?
	cp ../$S_dir/*.hpp "${PREFIX}/include/" || return $?
	popd
}

task_register build_xapian
build_xapian()
{
	B_dir="xapian-core-${XAPIAN_SOURCE_VERSION}"
	D_file="$B_dir.tar.xz"
	verified_download $D_file $XAPIAN_SOURCE_SHA256 \
		https://oligarchy.co.uk/xapian/${XAPIAN_SOURCE_VERSION}/$D_file || return $?
	rm -rf $B_dir
	tar -xf $D_file || return $?
	pushd $B_dir || return $?
	B_endiannes_detection_failure_workaround="ac_cv_c_bigendian=no"
	B_large_file=""
	[ "${ANDROID_PLATFORM_VER}" -ge "24" ] || B_large_file="--disable-largefile"
	./configure ${B_endiannes_detection_failure_workaround} ${B_large_file} \
		--with-pic \
		--disable-backend-inmemory --disable-backend-remote \
		--disable--backend-chert --enable-backend-glass \
		--host=${cArch}-linux --enable-static --disable-shared \
		--prefix="${PREFIX}" --with-sysroot="${SYSROOT}" || return $?
	make -j${HOST_NUM_CPU} || return $?
	make install || return $?

	# TODO: Fix upstream Xapian CMake package to find static library
	sed -i 's/libxapian.so/libxapian.a/g' \
		"${SYSROOT}/usr/lib/cmake/xapian/xapian-config.cmake" || return $?
	popd
}

task_register build_miniupnpc
build_miniupnpc()
{
	S_dir="miniupnpc-${MINIUPNPC_SOURCE_VERSION}"
	B_dir="miniupnpc-${MINIUPNPC_SOURCE_VERSION}-build"
	D_file="$S_dir.tar.gz"
	verified_download $D_file $MINIUPNPC_SOURCE_SHA256 \
		http://miniupnp.free.fr/files/${D_file} || return $?
	rm -rf $S_dir $B_dir
	tar -xf $D_file || return $?
	mkdir $B_dir || return $?
	pushd $B_dir
	andro_cmake \
		-DUPNPC_BUILD_STATIC=TRUE \
		-DUPNPC_BUILD_SHARED=FALSE \
		-DUPNPC_BUILD_TESTS=FALSE \
		-DUPNPC_BUILD_SAMPLE=FALSE \
		-B. -S../$S_dir || return $?
	make -j${HOST_NUM_CPU} || return $?
	make install || return $?
	popd
}

task_register build_zlib
build_zlib()
{
	S_dir="zlib-${ZLIB_SOURCE_VERSION}"
	B_dir="zlib-${ZLIB_SOURCE_VERSION}-build"
	D_file="$S_dir.tar.gz"
	verified_download $D_file $ZLIB_SOURCE_SHA256 \
		http://distfiles.gentoo.org/distfiles/${D_file} || return $?
	rm -rf $S_dir $B_dir
	tar -xf $D_file || return $?
	mkdir $B_dir || return $?
	pushd $B_dir || return $?
	andro_cmake -B. -S../$S_dir || return $?
	make -j${HOST_NUM_CPU} || return $?
	make install || return $?
	rm -fv ${PREFIX}/lib/libz.so*
	popd
}

task_register build_libpng
build_libpng()
{
	task_run build_zlib || return $?

	S_dir="libpng-${LIBPNG_SOURCE_VERSION}"
	B_dir="libpng-${LIBPNG_SOURCE_VERSION}-build"
	D_file="$S_dir.tar.xz"
	verified_download $D_file $LIBPNG_SOURCE_SHA256 \
		http://distfiles.gentoo.org/distfiles/${D_file} || return $?
	rm -rf $S_dir $B_dir
	tar -xf $D_file || return $?

	# libm is part of bionic on android
	sed -i -e 's/find_library(M_LIBRARY m)/set(M_LIBRARY "")/' \
		$S_dir/CMakeLists.txt || return $?
	
	# Disable hardware acceleration as they are problematic for Android
	# compilation and are not supported by all phones, it is necessary to fiddle
	# with CMakeLists.txt as libpng 1.6.37 passing it as cmake options seems not
	# working properly
	# https://github.com/imagemin/optipng-bin/issues/97
	# https://github.com/opencv/opencv/issues/7600
	echo "add_definitions(-DPNG_ARM_NEON_OPT=0)" >> $S_dir/CMakeLists.txt

	mkdir $B_dir
	pushd $B_dir

	HW_OPT="OFF"
#	[ "$ANDROID_PLATFORM_VER" -ge "22" ] && HW_OPT="ON"

	andro_cmake \
		-DPNG_SHARED=OFF \
		-DPNG_STATIC=ON \
		-DPNG_TESTS=OFF \
		-DPNG_HARDWARE_OPTIMIZATIONS=$HW_OPT \
		-DCMAKE_VERBOSE_MAKEFILE:BOOL=ON \
		-B. -S../$S_dir || return $?
	make -j${HOST_NUM_CPU} || return $?
	make install || return $?
	popd
}

task_register build_libjpeg
build_libjpeg()
{
	S_dir="jpeg-${LIBJPEG_SOURCE_VERSION}"
	D_file="jpegsrc.v${LIBJPEG_SOURCE_VERSION}.tar.gz"
	verified_download $D_file $LIBJPEG_SOURCE_SHA256 \
		https://www.ijg.org/files/$D_file || return $?
	rm -rf $S_dir
	tar -xf $D_file || return $?
	pushd $S_dir
	./configure --with-pic --prefix="${PREFIX}" --host=${cArch}-linux || return $?
	make -j${HOST_NUM_CPU} || return $?
	make install || return $?
	rm -f ${PREFIX}/lib/libjpeg.so*
	popd
}

task_register build_tiff
build_tiff()
{
	S_dir="tiff-${TIFF_SOURCE_VERSION}"
	B_dir="${S_dir}-build"
	D_file="tiff-${TIFF_SOURCE_VERSION}.tar.gz"

	verified_download $D_file $TIFF_SOURCE_SHA256 \
		https://download.osgeo.org/libtiff/${D_file} || return $?

	rm -rf $S_dir $B_dir
	tar -xf $D_file || return $?
	mkdir $B_dir

	# Disable tools building, not needed for retroshare, and depending on some
	# OpenGL headers not available on Android
	echo "" > $S_dir/tools/CMakeLists.txt

	# Disable tests building, not needed for retroshare, and causing linker
	# errors
	echo "" > $S_dir/test/CMakeLists.txt
	
	# Disable extra tools building, not needed for retroshare, and causing
	# linker errors
	echo "" > $S_dir/contrib/CMakeLists.txt
	
	# Disable more unneded stuff 
	echo "" > $S_dir/build/CMakeLists.txt
	echo "" > $S_dir/html/CMakeLists.txt
	echo "" > $S_dir/man/CMakeLists.txt
	echo "" > $S_dir/port/CMakeLists.txt

	# Change to static library build
	sed -i 's\add_library(tiff\add_library(tiff STATIC\' \
		$S_dir/libtiff/CMakeLists.txt || return $?

	pushd $B_dir
	#TODO: build dependecies to support more formats
	andro_cmake \
		-Dlibdeflate=OFF -Djbig=OFF -Dlzma=OFF -Dzstd=OFF -Dwebp=OFF \
		-Djpeg12=OFF \
		-Dcxx=OFF \
		-B. -S../$S_dir    || return $?
	make -j${HOST_NUM_CPU} || return $?
	make install           || return $?
	popd
}

task_register build_cimg
build_cimg()
{
	task_run build_libpng  || return $?
	task_run build_libjpeg || return $?
	task_run build_tiff    || return $?

	S_dir="CImg-${CIMG_SOURCE_VERSION}"
	D_file="CImg_${CIMG_SOURCE_VERSION}.zip"

	verified_download $D_file $CIMG_SOURCE_SHA256 \
		https://cimg.eu/files/${D_file} || return $?

	unzip -o $D_file || return $?

	cp --archive --verbose "$S_dir/CImg.h" "$PREFIX/include/" || return $?
}

task_register build_phash
build_phash()
{
	task_run build_cimg || return $?

	S_dir="pHash"
	B_dir="${S_dir}-build"

	git_source_get "$S_dir" "$PHASH_SOURCE_REPO" "${PHASH_SOURCE_VERSION}" \
		 || return $?

	rm -rf $B_dir;
	mkdir $B_dir || return $?
	pushd $B_dir || return $?
	andro_cmake -DPHASH_DYNAMIC=OFF -DPHASH_STATIC=ON  -B. -H../pHash \
		|| return $?
	make -j${HOST_NUM_CPU} || return $?
	make install || return $?
	popd
}

task_register build_mvptree
build_mvptree()
{
	S_dir="mvptree"
	B_dir="${S_dir}-build"

	git_source_get "$S_dir" "$MVPTREE_SOURCE_REPO" "${MVPTREE_SOURCE_VERSION}" \
		 || return $?
	rm -rf $B_dir
	mkdir $B_dir || return $?
	pushd $B_dir || return $?
	andro_cmake -B. -H../${S_dir} || return $?
	make -j${HOST_NUM_CPU} || return $?
	make install || return $?
	popd
}

task_register build_libretroshare
build_libretroshare()
{
	task_run build_zlib      || return $?
	task_run build_bzlib     || return $?
	task_run build_openssl   || return $?
	task_run build_sqlcipher || return $?
	task_run build_rapidjson || return $?
	task_run build_restbed   || return $?
	task_run build_xapian    || return $?
	task_run build_miniupnpc || return $?
	task_run build_phash     || return $?

	S_dir="$RS_SRC_DIR"
	B_dir="libretroshare-build"

	rm -rf $B_dir
	mkdir $B_dir || return $?
	pushd $B_dir || return $?
	andro_cmake -B. -H${S_dir} \
		-D RS_ANDROID=ON -D RS_WARN_DEPRECATED=OFF -D RS_WARN_LESS=ON \
		-D RS_LIBRETROSHARE_STATIC=OFF -D RS_LIBRETROSHARE_SHARED=ON \
		-D RS_BRODCAST_DISCOVERY=ON -D RS_EXPORT_JNI_ONLOAD=ON \
		-D RS_SQLCIPHER=OFF -D RS_DH_PRIME_INIT_CHECK=OFF \
		-D RS_FORUM_DEEP_INDEX=ON -D RS_JSON_API=ON \
		-D RS_LIBRETROSHARE_STANDALONE_INSTALL=ON \
		$RS_EXTRA_CMAKE_OPTS || return $?
	make -j${HOST_NUM_CPU} || return $?
	make install || return $?
	popd
}

task_register get_native_libs_toolchain_path
get_native_libs_toolchain_path()
{
	echo ${NATIVE_LIBS_TOOLCHAIN_PATH}
}

task_register build_default_toolchain
build_default_toolchain()
{
	task_run bootstrap_toolchain || return $?
	task_run build_libretroshare || return $?
	task_run get_native_libs_toolchain_path || return $?
}

if [ "$1" == "" ]; then
	rm -rf "$REPORT_DIR"
	mkdir -p "$REPORT_DIR"
	cat "$0" > "$REPORT_DIR/build_script"
	env > "$REPORT_DIR/build_env"
	build_default_toolchain || exit $?
else
	# do not delete report directory in this case so we can reuse material
	# produced by previous run, like deduplicated includes
	mkdir -p "$REPORT_DIR"
	while [ "$1" != "" ] ; do
		task_zap $1
		task_run $1 || exit $?
		shift
	done
fi
