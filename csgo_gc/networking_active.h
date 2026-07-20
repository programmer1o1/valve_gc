#pragma once

// Same pattern as gc_active.h, split out because these need to be included
// after the steam/* headers (NetworkingClient/NetworkingServer use
// STEAM_CALLBACK macros), while gc_active.h needs to come before them.
#if defined(TF2_GC_BUILD)
#include "../tf2_gc_hook/networking_client_tf2.h"
#include "../tf2_gc_hook/networking_server_tf2.h"
using ActiveNetworkingClient = NetworkingClientTF2;
using ActiveNetworkingServer = NetworkingServerTF2;
#else
#include "networking_client.h"
#include "networking_server.h"
using ActiveNetworkingClient = NetworkingClient;
using ActiveNetworkingServer = NetworkingServer;
#endif
