project(launcher)

# messy
function(launcher_target NAME DEDICATED)
    if (WIN32)
        add_executable(${NAME} WIN32 launcher_win.cpp launcher.rc)
    else()
        add_executable(${NAME} launcher_unix.cpp)
    endif()
    
    set_target_properties(${NAME} PROPERTIES OUTPUT_NAME ${NAME}${GC_EXE_SUFFIX})
    
    if (MSVC AND IS_32BIT)
        # large address awareness only matters for 32-bit; larger stack to not crash
        target_link_options(${NAME} PRIVATE /LARGEADDRESSAWARE /STACK:1572864)
    endif()
    
    # apple message box
    if (APPLE)
        target_link_libraries(${NAME} PRIVATE "-framework Foundation")
    endif()

    if (DEDICATED)
        target_compile_definitions(${NAME} PRIVATE DEDICATED)
    endif()
    
    target_compile_definitions(${NAME} PRIVATE
        GC_LIB_DIR="${GC_LIB_DIR}"
        GC_GAME_BIN_DIR="${GC_LIB_DIR}"
        GC_LIB_SUFFIX="${GC_LIB_SUFFIX}"
        GC_LIB_EXTENSION="${CMAKE_SHARED_LIBRARY_SUFFIX}")
    
    if (OUTDIR)
        add_custom_command(TARGET ${NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:${NAME}>
            ${OUTDIR}/$<TARGET_FILE_NAME:${NAME}>)
    endif()
endfunction()

launcher_target(csgo OFF)
launcher_target(cs2 OFF)
if (WIN32 AND NOT IS_32BIT)
    # CS2 on Windows: exe lives in game\bin\win64\ next to tier0.dll and engine2.dll.
    # It loads them directly rather than going through a launcher.dll.
    target_compile_definitions(cs2 PRIVATE
        GC_GAME_BIN_DIR="win64"
        CS2_LAUNCHER)
endif()

if (WIN32 OR (IS_32BIT AND NOT APPLE))
    launcher_target(srcds ON)
endif()

# CS2 dedicated server is always 64-bit
if (NOT IS_32BIT)
    launcher_target(cs2ds ON)
endif()
