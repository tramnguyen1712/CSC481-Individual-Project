#pragma once

#include <atomic>
#include <mutex>
#include <unordered_map>

struct RemotePlayerState {
    float x = 0.0f;
    float y = 0.0f;
};

struct SharedData {
    std::atomic<bool> running{true};
    std::atomic<double> timeScale{1.0};

    // Local player state written by the game thread and read by networking.
    float playerX = 0.0f;
    float playerY = 0.0f;

    // Remote player states received directly from peers.
    std::unordered_map<int, RemotePlayerState> remotePlayers;

    // Shared platform state received from the authoritative server.
    float platformX = 1010.0f;
    float platformY = 650.0f;
    bool platformConnected = false;

    // Protects all non-atomic state shared by the game and network threads.
    std::mutex stateMutex;
};
