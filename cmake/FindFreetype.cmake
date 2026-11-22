# Locate in-tree vendored FreeType target when cross-compiling or building statically.
set(FREETYPE_FOUND TRUE)
set(FREETYPE_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/vendor/freetype-2.14.3/include")
set(FREETYPE_LIBRARIES freetype)
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()
