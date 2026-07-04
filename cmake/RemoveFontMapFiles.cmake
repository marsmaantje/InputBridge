# RemoveFontMapFiles.cmake
# Deletes Kenney glyph map files (*_map.txt) from the post-build fonts output
# directory.  The map files are only needed when looking up codepoints to
# update KenneyIcons.h - they serve no purpose at runtime.
# License files (OFL.txt, LICENSE.txt, etc.) are intentionally left in place
# to give proper credit to the original font creators.
file(GLOB_RECURSE MAP_FILES "${FONT_DIR}/*_map.txt")
foreach(MAP_FILE ${MAP_FILES})
    file(REMOVE "${MAP_FILE}")
endforeach()