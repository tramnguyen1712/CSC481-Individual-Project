#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>

#include <functional>
#include <string>
#include <thread>

#include "Collision.h"
#include "Entity.h"
#include "Input.h"
#include "Networking.h"
#include "Physics.h"
#include "Scaling.h"
#include "SharedData.h"
#include "Timeline.h"

namespace {
constexpr int WINDOW_WIDTH = 1920;
constexpr int WINDOW_HEIGHT = 1080;
constexpr int CHARACTER_FRAME_COUNT = 4;
constexpr int CHARACTER_FRAME_WIDTH = 543;
constexpr int CHARACTER_FRAME_HEIGHT = 724;
constexpr int CLOUD_SUN_FRAME_WIDTH = 512;
constexpr int CLOUD_SUN_FRAME_HEIGHT = 682;
constexpr int TREE_FRAME_WIDTH = 512;
constexpr int TREE_FRAME_HEIGHT = 744;

SDL_Texture* loadTexture(SDL_Renderer* renderer, const char* path)
{
    SDL_Texture* texture = IMG_LoadTexture(renderer, path);
    if (!texture) {
        SDL_Log("Could not load %s: %s", path, SDL_GetError());
    }
    return texture;
}

const char* characterName(int clientID)
{
    if (clientID == 1) return "Bunny";
    if (clientID == 2) return "Fox";
    return "Cat";
}
} // namespace

int main(int argc, char* argv[])
{
    if (argc < 2) {
        SDL_Log("Usage: ./main <clientID 1-3>");
        return 1;
    }

    int clientID = 0;
    try {
        clientID = std::stoi(argv[1]);
    }
    catch (...) {
        SDL_Log("Client ID must be 1, 2, or 3");
        return 1;
    }

    if (clientID < 1 || clientID > 3) {
        SDL_Log("Client ID must be 1, 2, or 3");
        return 1;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Could not initialize SDL: %s", SDL_GetError());
        return 1;
    }

    const std::string windowTitle =
        "Bunny Hop - Client " + std::to_string(clientID) +
        " - " + characterName(clientID);

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(
            windowTitle.c_str(),
            WINDOW_WIDTH,
            WINDOW_HEIGHT,
            SDL_WINDOW_RESIZABLE,
            &window,
            &renderer)) {
        SDL_Log("Could not create window/renderer: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    Scaling::setReferenceResolution(WINDOW_WIDTH, WINDOW_HEIGHT);

    constexpr float LAND_Y = 760.0f;
    constexpr float GROUND_Y = 840.0f;
    constexpr float GAP_START = 970.0f;
    constexpr float GAP_END = 1210.0f;
    constexpr float PLAYER_WIDTH = 220.0f;
    constexpr float PLAYER_HEIGHT = 280.0f;
    constexpr float START_X = 170.0f;
    constexpr float START_Y = GROUND_Y - PLAYER_HEIGHT;
    constexpr float WALK_SPEED = 350.0f;
    constexpr float RUN_SPEED = 600.0f;
    constexpr float JUMP_STRENGTH = 700.0f;
    constexpr float PLATFORM_WIDTH = 170.0f;
    constexpr float PLATFORM_HEIGHT = 35.0f;

    Entity player(START_X, START_Y, PLAYER_WIDTH, PLAYER_HEIGHT);
    player.setGravityEnabled(true);
    player.setGrounded(true);
    player.setSpriteSheet(
        CHARACTER_FRAME_COUNT,
        CHARACTER_FRAME_WIDTH,
        CHARACTER_FRAME_HEIGHT);
    player.setAnimationSpeed(8.0f);

    Entity rock(650.0f, GROUND_Y - 140.0f, 165.0f, 140.0f);
    Entity cloud(350.0f, 100.0f, 500.0f, 280.0f);
    Entity sun(1250.0f, 100.0f, 310.0f, 310.0f);
    Entity leftLand(0.0f, LAND_Y, GAP_START, 320.0f);
    Entity rightLand(GAP_END, LAND_Y, WINDOW_WIDTH - GAP_END, 320.0f);
    Entity tree(1500.0f, GROUND_Y - 360.0f, 300.0f, 360.0f);
    Entity platform(1010.0f, 590.0f, PLATFORM_WIDTH, PLATFORM_HEIGHT);

    rock.setGravityEnabled(false);
    cloud.setGravityEnabled(false);
    sun.setGravityEnabled(false);
    leftLand.setGravityEnabled(false);
    rightLand.setGravityEnabled(false);
    tree.setGravityEnabled(false);
    platform.setGravityEnabled(false);

    cloud.setSpriteSheet(4, CLOUD_SUN_FRAME_WIDTH, CLOUD_SUN_FRAME_HEIGHT);
    cloud.setAnimationSpeed(4.0f);
    sun.setSpriteSheet(4, CLOUD_SUN_FRAME_WIDTH, CLOUD_SUN_FRAME_HEIGHT);
    sun.setAnimationSpeed(5.0f);
    tree.setSpriteSheet(4, TREE_FRAME_WIDTH, TREE_FRAME_HEIGHT);
    tree.setAnimationSpeed(3.0f);

    SDL_Texture* characterTextures[4] = {
        nullptr,
        loadTexture(renderer, "../assets/bunny.png"),
        loadTexture(renderer, "../assets/fox.png"),
        loadTexture(renderer, "../assets/cat.png")
    };

    SDL_Texture* cloudTexture = loadTexture(renderer, "../assets/cloud.png");
    SDL_Texture* sunTexture = loadTexture(renderer, "../assets/sun.png");
    SDL_Texture* landTexture = loadTexture(renderer, "../assets/land.png");
    SDL_Texture* rockTexture = loadTexture(renderer, "../assets/rock.png");
    SDL_Texture* treeTexture = loadTexture(renderer, "../assets/tree.png");

    player.setTexture(characterTextures[clientID]);
    cloud.setTexture(cloudTexture);
    sun.setTexture(sunTexture);
    leftLand.setTexture(landTexture);
    rightLand.setTexture(landTexture);
    platform.setTexture(landTexture); //
    rock.setTexture(rockTexture);
    tree.setTexture(treeTexture);

    // These are created once so their animation does not reset every frame
    Entity remoteBunny(
        0.0f, 0.0f,
        PLAYER_WIDTH, PLAYER_HEIGHT
    );

    Entity remoteFox(
        0.0f, 0.0f,
        PLAYER_WIDTH, PLAYER_HEIGHT
    );

    Entity remoteCat(
        0.0f, 0.0f,
        PLAYER_WIDTH, PLAYER_HEIGHT
    );

    Entity* remoteEntities[4] = {
        nullptr,
        &remoteBunny,
        &remoteFox,
        &remoteCat
    };

    bool remoteVisible[4] = {
        false,
        false,
        false,
        false
    };

    for (int id = 1; id <= 3; ++id) {
        remoteEntities[id]->setTexture(characterTextures[id]);

        remoteEntities[id]->setSpriteSheet(
            CHARACTER_FRAME_COUNT,
            CHARACTER_FRAME_WIDTH,
            CHARACTER_FRAME_HEIGHT
        );

        remoteEntities[id]->setAnimationSpeed(8.0f);
    }

    Physics physics(1000.0f);
    Timeline gameTime;

    SharedData sharedData;
    {
        std::lock_guard<std::mutex> lock(sharedData.stateMutex);
        sharedData.playerX = player.getX();
        sharedData.playerY = player.getY();
    }

    std::thread networkThread(networkingThread, std::ref(sharedData), clientID);

    bool running = true;
    bool gameOver = false;
    bool pauseKeyWasPressed = false;
    bool minusKeyWasPressed = false;
    bool plusKeyWasPressed = false;
    bool scalingKeyWasPressed = false;

    float cloudSpeed = 90.0f;
    int cloudDirection = 1;
    constexpr float CLOUD_MIN_X = 250.0f;
    constexpr float CLOUD_MAX_X = 900.0f;

    float previousPlatformX = platform.getX();
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        bool pausePressed = Input::isKeyPressed(SDL_SCANCODE_P);
        if (pausePressed && !pauseKeyWasPressed) {
            if (gameTime.isPaused()) {
                gameTime.unpause();
                SDL_Log("Game resumed");
            }
            else {
                gameTime.pause();
                SDL_Log("Game paused");
            }
        }
        pauseKeyWasPressed = pausePressed;

        bool minusPressed = Input::isKeyPressed(SDL_SCANCODE_MINUS);
        if (minusPressed && !minusKeyWasPressed) {
            if (gameTime.getScale() == 2.0) gameTime.setScale(1.0);
            else if (gameTime.getScale() == 1.0) gameTime.setScale(0.5);
            SDL_Log("Timeline scale: %.1fx", gameTime.getScale());
        }
        minusKeyWasPressed = minusPressed;

        bool plusPressed = Input::isKeyPressed(SDL_SCANCODE_EQUALS);
        if (plusPressed && !plusKeyWasPressed) {
            if (gameTime.getScale() == 0.5) gameTime.setScale(1.0);
            else if (gameTime.getScale() == 1.0) gameTime.setScale(2.0);
            SDL_Log("Timeline scale: %.1fx", gameTime.getScale());
        }
        plusKeyWasPressed = plusPressed;
        sharedData.timeScale.store(gameTime.getScale());

        bool scalingPressed = Input::isKeyPressed(SDL_SCANCODE_T);
        if (scalingPressed && !scalingKeyWasPressed) {
            Scaling::toggleMode();
            SDL_Log(
                "Scaling mode: %s",
                Scaling::getMode() == ScalingMode::PROPORTIONAL
                    ? "PROPORTIONAL" : "PIXEL");
        }
        scalingKeyWasPressed = scalingPressed;

        float deltaTime = static_cast<float>(gameTime.getDeltaTime());
        if (deltaTime > 0.05f) deltaTime = 0.05f;

        if (gameOver && Input::isKeyPressed(SDL_SCANCODE_R)) {
            player.setPosition(START_X, START_Y);
            player.setVelocity(0.0f, 0.0f);
            player.setGrounded(true);
            gameOver = false;
        }

        if (!gameOver && !gameTime.isPaused()) {
            float previousX = player.getX();
            float moveSpeed = WALK_SPEED;
            if (Input::isKeyPressed(SDL_SCANCODE_LSHIFT) ||
                Input::isKeyPressed(SDL_SCANCODE_RSHIFT)) {
                moveSpeed = RUN_SPEED;
            }

            if (Input::isKeyPressed(SDL_SCANCODE_A)) {
                player.move(-moveSpeed * deltaTime, 0.0f);
            }
            if (Input::isKeyPressed(SDL_SCANCODE_D)) {
                player.move(moveSpeed * deltaTime, 0.0f);
            }
            if (Input::isKeyPressed(SDL_SCANCODE_W)) {
                physics.jump(player, JUMP_STRENGTH);
            }

            if (player.getX() < 0.0f) {
                player.setPosition(0.0f, player.getY());
            }
            if (player.getX() + player.getWidth() > WINDOW_WIDTH) {
                player.setPosition(WINDOW_WIDTH - player.getWidth(), player.getY());
            }

            physics.update(player, deltaTime);

            float centerX = player.getX() + player.getWidth() / 2.0f;
            float bottom = player.getY() + player.getHeight();
            bool overGap = centerX > GAP_START && centerX < GAP_END;

            if (!overGap && bottom >= GROUND_Y &&
                player.getVelocityY() >= 0.0f) {
                player.setPosition(player.getX(), GROUND_Y - player.getHeight());
                player.setVelocityY(0.0f);
                player.setGrounded(true);
            }
            else {
                player.setGrounded(false);
            }

            if (Collision::checkCollision(player, rock)) {
                player.setPosition(previousX, player.getY());
            }

            if (player.getY() > WINDOW_HEIGHT) {
                gameOver = true;
                player.setVelocity(0.0f, 0.0f);
                SDL_Log("GAME OVER! Press R to restart.");
            }
        }

        // Read the server-authoritative platform position
        float serverPlatformX = platform.getX();
        float serverPlatformY = platform.getY();
        {
            std::lock_guard<std::mutex> lock(sharedData.stateMutex);
            serverPlatformX = sharedData.platformX;
            serverPlatformY = sharedData.platformY;
        }

        float platformDeltaX = serverPlatformX - previousPlatformX;
        platform.setPosition(serverPlatformX, serverPlatformY);
        previousPlatformX = serverPlatformX;

        float playerLeft = player.getX();
        float playerRight = player.getX() + player.getWidth();
        float playerBottom = player.getY() + player.getHeight();
        bool overPlatform =
            playerRight > platform.getX() &&
            playerLeft < platform.getX() + platform.getWidth();
        bool landingOnPlatform =
            playerBottom >= platform.getY() - 20.0f &&
            playerBottom <= platform.getY() + 20.0f &&
            player.getVelocityY() >= 0.0f;

        if (!gameOver && overPlatform && landingOnPlatform) {
            player.setPosition(
                player.getX() + platformDeltaX,
                platform.getY() - player.getHeight());
            player.setVelocityY(0.0f);
            player.setGrounded(true);
        }

        // Reset visibility before reading the newest network information
        for (int id = 1; id <= 3; ++id) {
            remoteVisible[id] = false;
        }

        {
            std::lock_guard<std::mutex> lock(
                sharedData.stateMutex
            );

            for (const auto& entry : sharedData.remotePlayers) {
                int remoteID = entry.first;

                if (remoteID < 1 || remoteID > 3) {
                    continue;
                }

                if (remoteID == clientID) {
                    continue;
                }

                const RemotePlayerState& remote = entry.second;

                remoteEntities[remoteID]->setPosition(
                    remote.x,
                    remote.y
                );

                remoteVisible[remoteID] = true;
            }
        }

        if (!gameTime.isPaused()) {
            cloud.move(cloudSpeed * cloudDirection * deltaTime, 0.0f);
            if (cloud.getX() >= CLOUD_MAX_X) {
                cloud.setPosition(CLOUD_MAX_X, cloud.getY());
                cloudDirection = -1;
            }
            else if (cloud.getX() <= CLOUD_MIN_X) {
                cloud.setPosition(CLOUD_MIN_X, cloud.getY());
                cloudDirection = 1;
            }

            player.updateAnimation(deltaTime);
            cloud.updateAnimation(deltaTime);
            sun.updateAnimation(deltaTime);
            tree.updateAnimation(deltaTime);

            // Animate remote characters even when they are standing still
            for (int id = 1; id <= 3; ++id) {
                if (id != clientID &&
                    remoteVisible[id] &&
                    remoteEntities[id] != nullptr) {
                    remoteEntities[id]->updateAnimation(deltaTime);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(sharedData.stateMutex);
            sharedData.playerX = player.getX();
            sharedData.playerY = player.getY();
        }

        // Pink and white ombre background
        constexpr int BACKGROUND_BANDS = 40;
        constexpr float BAND_HEIGHT =
            static_cast<float>(WINDOW_HEIGHT) / BACKGROUND_BANDS;

        for (int i = 0; i < BACKGROUND_BANDS; ++i) {
            float amount = static_cast<float>(i) /
                           static_cast<float>(BACKGROUND_BANDS - 1);
            SDL_SetRenderDrawColor(
                renderer,
                255,
                static_cast<Uint8>(250.0f - 55.0f * amount),
                static_cast<Uint8>(252.0f - 35.0f * amount),
                255);
            SDL_FRect band = {
                0.0f,
                i * BAND_HEIGHT,
                static_cast<float>(WINDOW_WIDTH),
                BAND_HEIGHT + 1.0f
            };
            SDL_RenderFillRect(renderer, &band);
        }

        cloud.render(renderer);
        sun.render(renderer);
        leftLand.render(renderer);
        rightLand.render(renderer);
        tree.render(renderer);
        rock.render(renderer);
        platform.render(renderer);

        // Render the persistent remote entities.
        for (int id = 1; id <= 3; ++id) {
            if (id != clientID &&
                remoteVisible[id] &&
                remoteEntities[id] != nullptr) {
                remoteEntities[id]->render(renderer);
            }
        }

        if (!gameOver) {
            player.render(renderer);
        }

        if (gameOver) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 255, 180, 205, 110);
            SDL_FRect overlay = {
                0.0f, 0.0f,
                static_cast<float>(WINDOW_WIDTH),
                static_cast<float>(WINDOW_HEIGHT)
            };
            SDL_RenderFillRect(renderer, &overlay);
        }

        SDL_RenderPresent(renderer);
    }

    sharedData.running.store(false);
    if (networkThread.joinable()) {
        networkThread.join();
    }

    for (int id = 1; id <= 3; ++id) {
        if (characterTextures[id]) SDL_DestroyTexture(characterTextures[id]);
    }
    if (cloudTexture) SDL_DestroyTexture(cloudTexture);
    if (sunTexture) SDL_DestroyTexture(sunTexture);
    if (landTexture) SDL_DestroyTexture(landTexture);
    if (rockTexture) SDL_DestroyTexture(rockTexture);
    if (treeTexture) SDL_DestroyTexture(treeTexture);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
