#include <zmq.hpp>

#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr int MAX_CLIENTS = 3;
constexpr int BASE_PORT = 5555;
constexpr int PLATFORM_ID = 0;
constexpr float PLATFORM_MIN_X = 980.0f;
constexpr float PLATFORM_MAX_X = 1050.0f;
constexpr float PLATFORM_Y = 590.0f;
constexpr float PLATFORM_SPEED = 100.0f;

struct PlatformState {
    float x = PLATFORM_MIN_X;
    float y = PLATFORM_Y;
};

PlatformState platform;
std::mutex platformMutex;
std::mutex outputMutex;

void updatePlatform()
{
    int direction = 1;
    auto previous = std::chrono::steady_clock::now();

    while (true) {
        auto current = std::chrono::steady_clock::now();
        float deltaTime =
            std::chrono::duration<float>(current - previous).count();
        previous = current;

        {
            std::lock_guard<std::mutex> lock(platformMutex);
            platform.x += PLATFORM_SPEED * direction * deltaTime;

            if (platform.x >= PLATFORM_MAX_X) {
                platform.x = PLATFORM_MAX_X;
                direction = -1;
            }
            else if (platform.x <= PLATFORM_MIN_X) {
                platform.x = PLATFORM_MIN_X;
                direction = 1;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

void handleClient(zmq::context_t& context, int clientID)
{
    zmq::socket_t responder(context, zmq::socket_type::rep);
    const int port = BASE_PORT + clientID;
    responder.bind("tcp://*:" + std::to_string(port));

    {
        std::lock_guard<std::mutex> lock(outputMutex);
        std::cout << "Client " << clientID
                  << " server thread listening on port " << port
                  << std::endl;
    }

    while (true) {
        zmq::message_t request;
        auto received = responder.recv(request, zmq::recv_flags::none);
        if (!received) {
            continue;
        }

        std::string requestText(
            static_cast<char*>(request.data()), request.size());
        std::istringstream input(requestText);
        int receivedID = 0;
        float playerX = 0.0f;
        float playerY = 0.0f;

        if (!(input >> receivedID >> playerX >> playerY)) {
            const std::string error = "ERROR";
            responder.send(zmq::buffer(error), zmq::send_flags::none);
            continue;
        }

        std::ostringstream output;
        {
            std::lock_guard<std::mutex> lock(platformMutex);
            output << PLATFORM_ID << ' '
                   << platform.x << ' '
                   << platform.y << ';';
        }

        const std::string reply = output.str();
        responder.send(zmq::buffer(reply), zmq::send_flags::none);
    }
}
} // namespace

int main()
{
    zmq::context_t context(1);
    std::cout << "Bunny Hop headless server starting" << std::endl;

    std::thread platformThread(updatePlatform);
    std::vector<std::thread> clientThreads;

    for (int clientID = 1; clientID <= MAX_CLIENTS; ++clientID) {
        clientThreads.emplace_back(handleClient, std::ref(context), clientID);
    }

    for (auto& thread : clientThreads) {
        thread.join();
    }
    platformThread.join();
    return 0;
}
