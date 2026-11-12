---
order: 3
description: operator<< and operator>> that put a struct into a sf::Packet and read it back
packages: sfml-network
sfml: 3
includes: <SFML/Network.hpp> <cstdint> <string>
---
// Lets a struct travel in a sf::Packet:  packet << state;  if (packet >> state) { ... }
// Fixed-size types (std::uint32_t, float, not int or long) keep the format the same on every machine.
struct PlayerState
{
    std::uint32_t id = 0;
    sf::Vector2f position;
    float angle = 0.f;
    std::string name;
};

sf::Packet& operator<<(sf::Packet& packet, const PlayerState& state)
{
    return packet << state.id << state.position.x << state.position.y << state.angle << state.name;
}

// Read in the same order as written. The packet tests false afterwards if it was too short.
sf::Packet& operator>>(sf::Packet& packet, PlayerState& state)
{
    return packet >> state.id >> state.position.x >> state.position.y >> state.angle >> state.name;
}
