---
order: 4
description: A function that takes every packet that has arrived on a non-blocking socket without stalling the frame
packages: sfml-network
sfml: 3
includes: <SFML/Network.hpp> <vector>
---
// Collects every packet that has arrived, without waiting, so the game loop never stalls on the network.
// After connecting: socket.setBlocking(false);  then once per frame:
//   std::vector<sf::Packet> packets;
//   if (!receiveAvailable(socket, packets)) { /* the other side disconnected */ }
// Sending on a non-blocking socket can return Status::Partial: send the same packet again until it's Done.
bool receiveAvailable(sf::TcpSocket& socket, std::vector<sf::Packet>& packets)
{
    while (true)
    {
        sf::Packet packet;
        switch (socket.receive(packet))
        {
        case sf::Socket::Status::Done:
            packets.push_back(std::move(packet));
            break;
        case sf::Socket::Status::NotReady:
        case sf::Socket::Status::Partial: // the socket keeps the part it has until the rest arrives
            return true;
        case sf::Socket::Status::Disconnected:
        case sf::Socket::Status::Error:
            return false;
        }
    }
}
