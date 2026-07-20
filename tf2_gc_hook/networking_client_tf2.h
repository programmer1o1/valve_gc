#pragma once

#include <cstdint>

// Minimal stand-in for csgo_gc/networking_client.h's NetworkingClient.
//
// CS:GO's NetworkingClient relays your equipped items to whatever listen
// server you're connected to, so other players in the match see your skins.
// That's a separate feature (multiplayer cosmetic visibility) from this
// milestone's goal (make your own TF2 backpack show up locally), so it's
// intentionally a no-op here. Revisit if/when TF2 in-match visibility is
// implemented.
class ClientGCTF2;
class ISteamNetworkingMessages;

class NetworkingClientTF2
{
public:
    explicit NetworkingClientTF2(ISteamNetworkingMessages *networkingMessages) { (void)networkingMessages; }

    void Update(ClientGCTF2 *) {}
    void SendMessage(const void *, uint32_t) {}
    void SetAuthTicket(uint32_t, const void *, uint32_t) {}
    void ClearAuthTicket(uint32_t) {}
};
