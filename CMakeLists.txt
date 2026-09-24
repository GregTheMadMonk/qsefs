CMAKE_MINIMUM_REQUIRED( VERSION 3.30 FATAL_ERROR )

set(
    CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Wno-missing-field-initializers -freflection -std=c++26"
)

project(
    qsefs
    LANGUAGES C CXX
    DESCRIPTION "FUSE filesystem for CUE files support, inspired by trackfs"
)

option( QSEFS_INSTALL  OFF )
option( QSEFS_SELFTEST OFF )

set( CMAKE_CXX_MODULE_STD        ON )
set( CMAKE_CXX_STANDARD          26 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )

find_package( PkgConfig REQUIRED )
pkg_check_modules( FUSE3 REQUIRED IMPORTED_TARGET fuse3 )
pkg_check_modules(
    LIBAV REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil
)

include( CPM.cmake/cmake/CPM.cmake )

set( DXX_COMPILE_FEATURES cxx_std_26 )
CPMAddPackage( "gh:gregthemadmonk/dot-xx#main" )

file( GLOB_RECURSE SRC_XX CONFIGURE_DEPENDS src/qsefs/*.xx )
file( GLOB_RECURSE SRC_CC CONFIGURE_DEPENDS src/qsefs/*.cc )

set_source_files_properties( ${SRC_XX} PROPERTIES LANGUAGE CXX )

add_library( qsefs STATIC ${SRC_CC} )
target_sources(
    qsefs PUBLIC
    FILE_SET qsefs_mod
    TYPE     CXX_MODULES
    FILES    ${SRC_XX}
)
target_compile_features( qsefs PRIVATE cxx_std_26 )
target_link_libraries(
    qsefs
    PUBLIC  dot-xx::all
    PRIVATE PkgConfig::FUSE3 PkgConfig::LIBAV
)

add_executable( qsefs-mount src/main.cc )
set_target_properties( qsefs-mount PROPERTIES OUTPUT_NAME "qsefs" )
target_link_libraries( qsefs-mount PRIVATE qsefs )
target_compile_features( qsefs-mount PRIVATE cxx_std_26 )

if( QSEFS_SELFTEST )
    file( GLOB_RECURSE TST_CC CONFIGURE_DEPENDS test/*.cc )
    add_executable( selftest ${TST_CC} )
    target_link_libraries( selftest PRIVATE qsefs dot-xx::selftest-main )
endif()

if( QSEFS_INSTALL )
    install( TARGETS qsefs-mount )
    # Create mount. symlink to allow to mount from fstab
    install( CODE "execute_process( WORKING_DIRECTORY \"${CMAKE_INSTALL_PREFIX}/bin/\" COMMAND \"${CMAKE_COMMAND}\" -E create_symlink \"$<TARGET_FILE_NAME:qsefs-mount>\" \"${CMAKE_INSTALL_PREFIX}/bin/mount.$<TARGET_FILE_NAME:qsefs-mount>\" )" )

    file( GLOB INST_SUBDIRS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/install/" "install/*" )
    foreach( INST_SUBDIR ${INST_SUBDIRS} )
        if( IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/install/${INST_SUBDIR}" )
            install(
                DIRECTORY   "${CMAKE_CURRENT_SOURCE_DIR}/install/${INST_SUBDIR}"
                DESTINATION "${CMAKE_INSTALL_PREFIX}"
            )
        endif()
    endforeach()
endif()
