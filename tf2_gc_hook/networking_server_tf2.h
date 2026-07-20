#pragma once

#include <cstdint>

// Minimal stand-in for csgo_gc/networking_server.h's NetworkingServer.
// TF2 listen/dedicated server relay (showing your backpack cosmetics to
// other players in a match) is out of scope for this milestone; see
// networking_client_tf2.h for the same reasoning.
class ISteamNetworkingMessages;
struct SteamNetworkingMessage_t;

class NetworkingServerTF2
{
public:
    explicit NetworkingServerTF2(ISteamNetworkingMessages *networkingMessages) { (void)networkingMessages; }

    bool ReceiveMessage(SteamNetworkingMessage_t *&) { return false; }

    void ClientConnected(uint64_t, const void *, uint32_t) {}
    void ClientDisconnected(uint64_t) {}

    void SendMessage(uint64_t, const void *, uint32_t) {}
};
