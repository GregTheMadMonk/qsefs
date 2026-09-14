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

file( GLOB_RECURSE SRC_XX CONFIGURE_DEPENDS src/*.xx )
file( GLOB_RECURSE SRC_CC CONFIGURE_DEPENDS src/*.cc )

set_source_files_properties( ${SRC_XX} PROPERTIES LANGUAGE CXX )

add_executable( qsefs ${SRC_CC} )
target_sources(
    qsefs PUBLIC
    FILE_SET qsefs_mod
    TYPE     CXX_MODULES
    FILES    ${SRC_XX}
)
target_compile_features( qsefs PRIVATE cxx_std_26 )
target_link_libraries(
    qsefs PRIVATE
    dot-xx::all PkgConfig::FUSE3 PkgConfig::LIBAV
)
