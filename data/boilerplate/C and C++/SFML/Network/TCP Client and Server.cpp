---
order: 1
description: A console program that is a TCP server or client: the client sends lines, the server answers each one
file: main.cpp
whole-file: yes
packages: sfml-network
sfml: 3
---
// A TCP server and client in one program, talking in sf::Packets on port 53000. Start one copy as the server,
// then another as the client (on this computer or another one) and type lines into the client.
// Arguments skip the question:  app server  |  app client [address]
#include <SFML/Network.hpp>

#include <iostream>
#include <optional>
#include <string>

namespace
{
constexpr unsigned short Port = 53000;

int runServer()
{
    sf::TcpListener listener;
    if (listener.listen(Port) != sf::Socket::Status::Done)
    {
        std::cerr << "Can't listen on port " << Port << " (is another server running?)\n";
        return 1;
    }
    std::cout << "Waiting for a client on port " << Port << "...\n";

    sf::TcpSocket client;
    if (listener.accept(client) != sf::Socket::Status::Done)
    {
        std::cerr << "Accepting the client failed\n";
        return 1;
    }
    if (const std::optional<sf::IpAddress> address = client.getRemoteAddress())
    {
        std::cout << "Client connected from " << *address << '\n';
    }

    // receive() waits until a whole packet has arrived (the socket is blocking).
    sf::Packet request;
    while (client.receive(request) == sf::Socket::Status::Done)
    {
        std::string text;
        if (!(request >> text))
        {
            continue; // not a string packet
        }
        std::cout << "Received: " << text << '\n';
        {{cursor}}
        sf::Packet reply;
        reply << "The server got " + std::to_string(text.size()) + " characters: " + text;
        if (client.send(reply) != sf::Socket::Status::Done)
        {
            break;
        }
    }
    std::cout << "Client disconnected\n";
    return 0;
}

int runClient(const std::string& host)
{
    // Numbers only; sf::Dns::resolve("example.com") looks up host names.
    const std::optional<sf::IpAddress> address = sf::IpAddress::fromString(host);
    if (!address)
    {
        std::cerr << "Not an IP address: " << host << '\n';
        return 1;
    }

    sf::TcpSocket socket;
    if (socket.connect(*address, Port, sf::seconds(5)) != sf::Socket::Status::Done)
    {
        std::cerr << "Can't connect to " << host << ':' << Port << " (is the server running?)\n";
        return 1;
    }
    std::cout << "Connected. Type a line and press Enter; Ctrl+D quits.\n";

    std::string line;
    while (std::getline(std::cin, line))
    {
        sf::Packet request;
        request << line;
        sf::Packet reply;
        std::string text;
        if (socket.send(request) != sf::Socket::Status::Done || socket.receive(reply) != sf::Socket::Status::Done ||
            !(reply >> text))
        {
            std::cerr << "The server went away\n";
            return 1;
        }
        std::cout << text << '\n';
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
        std::cout << "Run as server or client? ";
        std::getline(std::cin, mode);
        if (mode == "client")
        {
            std::cout << "Server address (Enter for " << host << "): ";
            std::string typed;
            std::getline(std::cin, typed);
            if (!typed.empty())
            {
                host = typed;
            }
        }
    }

    if (mode == "server")
    {
        return runServer();
    }
    if (mode == "client")
    {
        return runClient(host);
    }
    std::cerr << "Usage: " << argv[0] << " server | client [address]\n";
    return 1;
}
