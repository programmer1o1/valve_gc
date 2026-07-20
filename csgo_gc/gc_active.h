#pragma once

// steam_hook.cpp uses GetConfig() directly and previously got "config.h"
// transitively through gc_client.h; include it explicitly here so that
// still holds regardless of which branch below is active.
#include "config.h"

// Selects which concrete ClientGC/ServerGC implementation steam_hook.cpp
// wires up. The csgo_gc target never defines TF2_GC_BUILD, so this expands
// to exactly the original #include "gc_client.h" / "gc_server.h" pair with
// no behavior change for CS:GO/CS2. The tf2_gc target (see
// ../tf2_gc_hook/CMakeLists.txt) defines TF2_GC_BUILD to swap in the TF2
// stand-ins instead. See docs/tf2_live_hook.md.
#if defined(TF2_GC_BUILD)
#include "../tf2_gc_hook/gc_client_tf2.h"
#include "../tf2_gc_hook/gc_server_tf2.h"
using ActiveClientGC = ClientGCTF2;
using ActiveServerGC = ServerGCTF2;
#else
#include "gc_client.h"
#include "gc_server.h"
using ActiveClientGC = ClientGC;
using ActiveServerGC = ServerGC;
#endif
