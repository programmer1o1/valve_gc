project(tf2_gc)

# Live TF2 GC hook. Reuses the generic GCSDK-level plumbing from ../csgo_gc
# unmodified (steam_hook.cpp, gc_shared.cpp, gc_message.cpp, game_profile.cpp,
# config.cpp, appid.cpp, keyvalue.cpp, main.cpp, platform_*.cpp) -- these are
# the same hooking/threading/job-id machinery CS:GO/CS2 use, since the GCSDK
# handshake+SO-cache wire protocol is identical across Valve GC games (see
# docs/tf2_live_hook.md). NONE of csgo_gc's CS:GO-specific business logic
# (gc_client.cpp, gc_server.cpp, inventory.cpp, item_schema.cpp,
# case_opening.cpp, graffiti.cpp, networking_client.cpp/networking_server.cpp)
# is reused -- this target provides its own TF2 equivalents instead.
add_library(tf2_gc SHARED
    gc_client_tf2.cpp
    gc_const_tf2.h
    gc_server_tf2.h
    networking_client_tf2.h
    networking_server_tf2.h

    # milestone 1's standalone TF2 schema/inventory parser (see tf2_gc/)
    ../tf2_gc/item_schema.cpp
    ../tf2_gc/inventory.cpp

    # reused csgo_gc plumbing, unmodified
    ../csgo_gc/appid.cpp
    ../csgo_gc/config.cpp
    ../csgo_gc/game_profile.cpp
    ../csgo_gc/gc_message.cpp
    ../csgo_gc/gc_shared.cpp
    ../csgo_gc/keyvalue.cpp
    ../csgo_gc/main.cpp
    ../csgo_gc/steam_hook.cpp)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_sources(tf2_gc PRIVATE ../csgo_gc/platform_windows.cpp)
else()
    target_sources(tf2_gc PRIVATE ../csgo_gc/platform_unix.cpp)
endif()

target_include_directories(tf2_gc PRIVATE ../csgo_gc)
target_compile_definitions(tf2_gc PRIVATE
    TF2_GC_BUILD
    GC_MODULE_NAME="tf2_gc"
    GC_GAME_DIR="tf")

# reused sources above still #include "stdafx.h" (their normal PCH);
# reuse the same PCH here instead of recompiling it from scratch.
target_precompile_headers(tf2_gc PRIVATE ../csgo_gc/stdafx.h)

set_target_properties(tf2_gc PROPERTIES PREFIX "")

if (MSVC)
    target_compile_options(tf2_gc PRIVATE /W4 /wd4244)
    target_compile_definitions(tf2_gc PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(tf2_gc PRIVATE -Wall -Wextra)
endif()

file(GLOB TF2_GC_PROTOBUFS ../protobufs/*.cc)
target_include_directories(tf2_gc PRIVATE ../protobufs)
target_sources(tf2_gc PRIVATE ${TF2_GC_PROTOBUFS})
source_group("Protobufs" FILES ${TF2_GC_PROTOBUFS})

target_link_libraries(tf2_gc PRIVATE
    cryptopp
    funchook-static
    libprotobuf-lite
    steam_api)

if (APPLE)
    target_link_libraries(tf2_gc PRIVATE "-framework Foundation")
endif()

if (OUTDIR)
    add_custom_command(TARGET tf2_gc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:tf2_gc>
        ${OUTDIR}/tf2_gc/${GC_LIB_DIR}/$<TARGET_FILE_NAME:tf2_gc>)
endif()
