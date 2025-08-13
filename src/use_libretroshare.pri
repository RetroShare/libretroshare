# SPDX-FileCopyrightText: (C) 2004-2019 Retroshare Team <contact@retroshare.cc>
# SPDX-License-Identifier: CC0-1.0

RS_SRC_PATH=$$clean_path($${PWD}/../../)
RS_BUILD_PATH=$$clean_path($${OUT_PWD}/../../)

DEPENDPATH *= $$clean_path($${RS_SRC_PATH}/libretroshare/src/)
INCLUDEPATH  *= $$clean_path($${RS_SRC_PATH}/libretroshare/src)

equals(TARGET, retroshare):equals(TEMPLATE, lib){
} else {
	LIBS *= -L$$clean_path($${RS_BUILD_PATH}/libretroshare/src/lib/) -lretroshare
	win32-g++|win32-clang-g++:!isEmpty(QMAKE_SH):libretroshare_shared {
		# Windows msys2
		LIBRETROSHARE_TARGET=libretroshare.dll.a
	} else {
		LIBRETROSHARE_TARGET=libretroshare.a
	}
    PRE_TARGETDEPS *= $$clean_path($${RS_BUILD_PATH}/libretroshare/src/lib/$${LIBRETROSHARE_TARGET})
}

bitdht {
    !include("../../libbitdht/src/use_libbitdht.pri"):error("Including")
}

# when rapidjson is mainstream on all distribs, we will not need the sources
# anymore in the meantime, they are part of the RS directory so that it is
# always possible to find them
RAPIDJSON_AVAILABLE = $$system(pkg-config --atleast-version 1.1 RapidJSON && echo yes)
isEmpty(RAPIDJSON_AVAILABLE) {
    message("using rapidjson from submodule")
    INCLUDEPATH *= $$clean_path($${PWD}/../../supportlibs/rapidjson/include)
} else {
    message("using system rapidjson")
}

rs_openpgpsdk {
        !include("../../openpgpsdk/src/use_openpgpsdk.pri"):error("Including")
        DEFINES *= USE_OPENPGPSDK
}

sLibs =
mLibs = $$RS_SQL_LIB ssl crypto $$RS_THREAD_LIB $$RS_UPNP_LIB
dLibs =

rs_rnplib {
    LIBRNP_SRC_PATH=$$clean_path($${RS_SRC_PATH}/supportlibs/librnp)
    LIBRNP_BUILD_PATH=$$clean_path($${RS_BUILD_PATH}/supportlibs/librnp/Build)
    INCLUDEPATH *= $$clean_path($${LIBRNP_SRC_PATH}/include/)
    INCLUDEPATH *= $$clean_path($${LIBRNP_BUILD_PATH}/src/lib/)
    DEPENDPATH *= $$clean_path($${LIBRNP_BUILD_PATH})
    QMAKE_LIBDIR *= $$clean_path($${LIBRNP_BUILD_PATH}/src/lib/)
#    INCLUDEPATH *= $$clean_path($${LIBRNP_SRC_PATH}/src/lib/)
#    DEPENDPATH *= $$clean_path($${LIBRNP_BUILD_PATH}/include/)
#    QMAKE_LIBDIR *= $$clean_path($${LIBRNP_BUILD_PATH}/)

    LIBRNP_LIBS = -L$$clean_path($${LIBRNP_BUILD_PATH}/src/lib) -lrnp
    LIBRNP_LIBS *= -L$$clean_path($${LIBRNP_BUILD_PATH}/src/libsexpp) -lsexpp
    LIBRNP_LIBS *= -lbz2 -lz -ljson-c

    # botan
    win32-g++|win32-clang-g++:!isEmpty(QMAKE_SH) {
        # Windows msys2
        LIBRNP_LIBS *= -lbotan-3
    } else {
        LIBRNP_LIBS *= -lbotan-2
    }

    win32-g++|win32-clang-g++ {
        # Use librnp as shared library for Windows
        CONFIG += librnp_shared
    }

    !libretroshare_shared {
        # libretroshare is used as a static library. Link the external libraries to the executable.
        LIBS *= $${LIBRNP_LIBS}
    }

	#PRE_TARGETDEPS += $$clean_path($${LIBRNP_BUILD_PATH}/src/lib/librnp.a)

    message("Using librnp. Configuring paths for submodule.")
    message("      LIBRNP_SRC_PATH   = "$${LIBRNP_SRC_PATH})
    message("      LIBRNP_BUILD_PATH = "$${LIBRNP_BUILD_PATH})
    message("      INCLUDEPATH      *= "$$clean_path($${LIBRNP_SRC_PATH}/include/))
    message("      INCLUDEPATH      *= "$$clean_path($${LIBRNP_SRC_PATH}/src/lib/))
}

rs_jsonapi {
    no_rs_cross_compiling {
        RESTBED_SRC_PATH=$$clean_path($${RS_SRC_PATH}/supportlibs/restbed)
        RESTBED_BUILD_PATH=$$clean_path($${RS_BUILD_PATH}/supportlibs/restbed)
        INCLUDEPATH *= $$clean_path($${RESTBED_BUILD_PATH}/include/)
        DEPENDPATH *= $$clean_path($${RESTBED_BUILD_PATH}/include/)
        QMAKE_LIBDIR *= $$clean_path($${RESTBED_BUILD_PATH}/)
        # Using sLibs would fail as librestbed.a is generated at compile-time
        LIBS *= -L$$clean_path($${RESTBED_BUILD_PATH}/) -lrestbed

        win32-g++ {
            # Avoid compile errors "definition is marked dllimport" of inline methods in restbed
            DEFINES *= WIN_DLL_EXPORT
        }
    } else:sLibs *= restbed

    win32-g++|win32-clang-g++:dLibs *= wsock32
}

linux-* {
    mLibs += dl
}

rs_deep_channels_index | rs_deep_files_index | rs_deep_forums_index {
    mLibs += xapian
    win32-g++|win32-clang-g++:mLibs += rpcrt4
}

rs_deep_files_index_ogg {
    mLibs += vorbisfile
}

rs_deep_files_index_flac {
    mLibs += FLAC++
}

rs_deep_files_index_taglib {
    mLibs += tag
}

rs_broadcast_discovery {
    no_rs_cross_compiling {
        UDP_DISCOVERY_SRC_PATH=$$clean_path($${RS_SRC_PATH}/supportlibs/udp-discovery-cpp/)
        UDP_DISCOVERY_BUILD_PATH=$$clean_path($${RS_BUILD_PATH}/supportlibs/udp-discovery-cpp/)
        INCLUDEPATH *= $$clean_path($${UDP_DISCOVERY_SRC_PATH})
        DEPENDPATH *= $$clean_path($${UDP_DISCOVERY_BUILD_PATH})
        QMAKE_LIBDIR *= $$clean_path($${UDP_DISCOVERY_BUILD_PATH})
        # Using sLibs would fail as libudp-discovery.a is generated at compile-time
        LIBS *= -L$$clean_path($${UDP_DISCOVERY_BUILD_PATH}) -ludp-discovery
    } else:sLibs *= udp-discovery

    win32-g++|win32-clang-g++:dLibs *= wsock32
}

rs_sam3_libsam3 {
    LIBSAM3_SRC_PATH=$$clean_path($${RS_SRC_PATH}/supportlibs/libsam3/)
    LIBSAM3_BUILD_PATH=$$clean_path($${RS_BUILD_PATH}/supportlibs/libsam3/)
    INCLUDEPATH *= $$clean_path($${LIBSAM3_SRC_PATH}/src/libsam3/)
    DEPENDPATH *= $$clean_path($${LIBSAM3_BUILD_PATH})
    QMAKE_LIBDIR *= $$clean_path($${LIBSAM3_BUILD_PATH})
    LIBS *= -L$$clean_path($${LIBSAM3_BUILD_PATH}) -lsam3
}

static {
    sLibs *= $$mLibs
} else {
    dLibs *= $$mLibs
}

LIBS += $$linkStaticLibs(sLibs)
PRE_TARGETDEPS += $$pretargetStaticLibs(sLibs)

LIBS += $$linkDynamicLibs(dLibs)

android-* {
    INCLUDEPATH *= $$clean_path($${RS_SRC_PATH}/supportlibs/jni.hpp/include/)
}

################################### Pkg-Config Stuff #############################
!isEmpty(PKGCONFIG) {
    LIBS *= $$system(pkg-config --libs $$PKGCONFIG)
}
