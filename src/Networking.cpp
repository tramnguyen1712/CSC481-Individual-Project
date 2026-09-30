#include "Networking.h"

#include <zmq.hpp>

#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {
constexpr int SERVER_BASE_PORT = 5555;
constexpr int PEER_BASE_PORT = 6000;
constexpr int TOTAL_CLIENTS = 3;

void receivePeerPositions(zmq::socket_t& subscriber,
                          SharedData& sharedData,
                          int clientID)
{
    while (true) {
        zmq::message_t message;
        auto result = subscriber.recv(message, zmq::recv_flags::dontwait);
        if (!result) {
            break;
        }

        std::string text(static_cast<char*>(message.data()), message.size());
        std::istringstream input(text);
        int remoteID = 0;
        float remoteX = 0.0f;
        float remoteY = 0.0f;

        if (input >> remoteID >> remoteX >> remoteY &&
            remoteID >= 1 && remoteID <= TOTAL_CLIENTS &&
            remoteID != clientID) {
            std::lock_guard<std::mutex> lock(sharedData.stateMutex);
            sharedData.remotePlayers[remoteID] = {remoteX, remoteY};
        }
    }
}

void receiveServerPlatform(const std::string& reply, SharedData& sharedData)
{
    std::istringstream entries(reply);
    std::string entry;

    while (std::getline(entries, entry, ';')) {
        if (entry.empty()) {
            continue;
        }

        std::istringstream values(entry);
        int objectID = -1;
        float x = 0.0f;
        float y = 0.0f;

        if (values >> objectID >> x >> y && objectID == 0) {
            std::lock_guard<std::mutex> lock(sharedData.stateMutex);
            sharedData.platformX = x;
            sharedData.platformY = y;
            sharedData.platformConnected = true;
        }
    }
}
} // namespace

void networkingThread(SharedData& sharedData, int clientID)
{
    try {
        zmq::context_t context(1);

        // Hybrid Section 5 design: player positions travel directly peer-to-peer.
        zmq::socket_t publisher(context, zmq::socket_type::pub);
        publisher.set(zmq::sockopt::linger, 0);
        publisher.bind("tcp://*:" + std::to_string(PEER_BASE_PORT + clientID));

        zmq::socket_t subscriber(context, zmq::socket_type::sub);
        subscriber.set(zmq::sockopt::linger, 0);
        subscriber.set(zmq::sockopt::subscribe, "");

        for (int peerID = 1; peerID <= TOTAL_CLIENTS; ++peerID) {
            if (peerID != clientID) {
                subscriber.connect(
                    "tcp://localhost:" +
                    std::to_string(PEER_BASE_PORT + peerID));
            }
        }

        // Section 4: each client has its own synchronous server connection.
        // The server dedicates one REP socket/thread to every client.
        zmq::socket_t serverRequest(context, zmq::socket_type::req);
        serverRequest.set(zmq::sockopt::linger, 0);
        serverRequest.set(zmq::sockopt::req_relaxed, 1);
        serverRequest.set(zmq::sockopt::req_correlate, 1);
        serverRequest.set(zmq::sockopt::rcvtimeo, 100);
        serverRequest.set(zmq::sockopt::sndtimeo, 100);
        serverRequest.connect(
            "tcp://localhost:" +
            std::to_string(SERVER_BASE_PORT + clientID));

        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        std::cout << "Client " << clientID
                  << " networking thread started" << std::endl;

        while (sharedData.running.load()) {
            float localX = 0.0f;
            float localY = 0.0f;
            {
                std::lock_guard<std::mutex> lock(sharedData.stateMutex);
                localX = sharedData.playerX;
                localY = sharedData.playerY;
            }

            std::ostringstream position;
            position << clientID << ' ' << localX << ' ' << localY;
            const std::string message = position.str();

            publisher.send(zmq::buffer(message), zmq::send_flags::none);
            receivePeerPositions(subscriber, sharedData, clientID);

            // Send a synchronous request only when the socket is ready.
            // After a successful send, receive the server-authoritative platform.
            auto sent = serverRequest.send(
                zmq::buffer(message),
                zmq::send_flags::dontwait);

            if (sent) {
                zmq::message_t reply;
                auto received = serverRequest.recv(reply, zmq::recv_flags::none);
                if (received) {
                    receiveServerPlatform(
                        std::string(static_cast<char*>(reply.data()), reply.size()),
                        sharedData);
                }
            }

            double scale = sharedData.timeScale.load();
            if (scale <= 0.0) {
                scale = 1.0;
            }

            const int delayMs = static_cast<int>(16.0 / scale);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(delayMs));
        }

        serverRequest.close();
        subscriber.close();
        publisher.close();
        context.close();
    }
    catch (const zmq::error_t& error) {
        if (sharedData.running.load()) {
            std::cerr << "Networking error for client " << clientID
                      << ": " << error.what() << std::endl;
        }
    }
}
