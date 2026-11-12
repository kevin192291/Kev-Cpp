---
order: 2
description: A console program that sends typed lines as UDP datagrams, or receives and prints them
file: main.cpp
whole-file: yes
packages: sfml-network
sfml: 3
---
// UDP datagrams on port 54000: no connection, and each message arrives whole, or not at all, in any order.
// Start one copy as the receiver, then another as the sender and type lines into it.
// Arguments skip the question:  app receive  |  app send [address]   (255.255.255.255 sends to the whole network)
#include <SFML/Network.hpp>

#include <iostream>
#include <optional>
#include <string>

namespace
{
constexpr unsigned short Port = 54000;

int runReceiver()
{
    sf::UdpSocket socket;
    if (socket.bind(Port) != sf::Socket::Status::Done)
    {
        std::cerr << "Can't use port " << Port << " (is another receiver running?)\n";
        return 1;
    }
    std::cout << "Listening on UDP port " << Port << "...\n";

    while (true)
    {
        sf::Packet packet;
        std::optional<sf::IpAddress> sender;
        unsigned short senderPort = 0;
        if (socket.receive(packet, sender, senderPort) != sf::Socket::Status::Done)
        {
            continue;
        }
        std::string text;
        if (sender && packet >> text)
        {
            std::cout << *sender << ':' << senderPort << " says: " << text << '\n';
            {{cursor}}
        }
    }
}

int runSender(const std::string& host)
{
    const std::optional<sf::IpAddress> address = sf::IpAddress::fromString(host);
    if (!address)
    {
        std::cerr << "Not an IP address: " << host << '\n';
        return 1;
    }

    sf::UdpSocket socket;
    std::cout << "Type a line and press Enter; Ctrl+D quits.\n";
    std::string line;
    while (std::getline(std::cin, line))
    {
        sf::Packet packet;
        packet << line;
        if (socket.send(packet, *address, Port) != sf::Socket::Status::Done)
        {
            std::cerr << "Sending failed\n";
        }
    }
    return 0;
}
}

int main(int argc, char* argv[])
{
    std::string mode = argc > 1 ? argv[1] : "";
    std::string host = argc > 2 ? argv[2] : "127.0.0.1";
    if (mode.empty())
    {
        std::cout << "Run as receive or send? ";
        std::getline(std::cin, mode);
        if (mode == "send")
        {
            std::cout << "Address to send to (Enter for " << host << "): ";
            std::string typed;
            std::getline(std::cin, typed);
            if (!typed.empty())
            {
                host = typed;
            }
        }
    }

    if (mode == "receive")
    {
        return runReceiver();
    }
    if (mode == "send")
    {
        return runSender(host);
    }
    std::cerr << "Usage: " << argv[0] << " receive | send [address]\n";
    return 1;
}
