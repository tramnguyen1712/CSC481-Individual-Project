#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <vector>
#include <string>
#include <stdexcept>
#include <thread>

#include "Collision.h"
#include "EventManager.h"
#include "GameObjectFactory.h"
#include "GameObjectOps.h"
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
constexpr int CLOUD_SUN_FRAME_WIDTH = 543;
constexpr int CLOUD_SUN_FRAME_HEIGHT = 724;
constexpr int TREE_FRAME_WIDTH = 519;
constexpr int TREE_FRAME_HEIGHT = 756;

// M3: Components are data, the main loop supplies their behavior
struct SpawnPointComponent : Component {
    int index;
    explicit SpawnPointComponent(int value) : index(value) {}
};
struct DeathZoneComponent : Component {};
struct MovingPlatformComponent : Component {
    float originX, originY, amplitudeX, amplitudeY, period;
    MovingPlatformComponent(float x, float y, float ax, float ay, float seconds)
        : originX(x), originY(y), amplitudeX(ax), amplitudeY(ay), period(seconds) {}
};

GameObject makeSceneObject(const char* type, float x, float y, float w, float h,
                           bool visible = true, bool solid = false)
{
    GameObject object(type);
    object.addComponent<TransformComponent>(x, y, w, h);
    if (visible) object.addComponent<RenderableComponent>();
    if (solid) object.addComponent<CollidableComponent>();
    return object;
}

void setAnimationSpeed(GameObject& object, float fps)
{
    auto* renderable = object.getComponent<RenderableComponent>();
    if (renderable && fps > 0) renderable->frameDuration = 1.0f / fps;
}

void updateAnimation(GameObject& object, float deltaTime)
{
    auto* renderable = object.getComponent<RenderableComponent>();
    if (!renderable || renderable->frameCount <= 1 || renderable->frameDuration <= 0) return;
    renderable->animationTimer += deltaTime;
    while (renderable->animationTimer >= renderable->frameDuration) {
        renderable->currentFrame = (renderable->currentFrame + 1) % renderable->frameCount;
        renderable->animationTimer -= renderable->frameDuration;
    }
}

void renderWorld(GameObject& object, SDL_Renderer* renderer, float cameraX)
{
    auto* transform = object.getComponent<TransformComponent>();
    if (!transform) return;
    // Translate for drawing only, collisions and network packets stay in world space
    float worldX = transform->x;
    transform->x -= cameraX;
    go::render(object, renderer);
    transform->x = worldX;
}

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
    if (clientID == 3) return "Cat";
    return "Otter";
}
} // namespace

int main(int argc, char* argv[])
{
    if (argc < 2) {
        SDL_Log("Usage: ./main <clientID 1-4> [text|binary] [--offline]");
        return 1;
    }

    bool offline = false;
    NetworkFormat networkFormat = NetworkFormat::TEXT;
    for (int i = 2; i < argc; ++i) {
        std::string argument = argv[i];
        if (argument == "--offline") offline = true;
        else if (argument == "text" || argument == "binary") networkFormat = NetFormat::parseFormat(argument);
        else { SDL_Log("Unknown option: %s", argv[i]); return 1; }
    }

    int clientID = 0;
    try {
        size_t consumed = 0;
        clientID = std::stoi(argv[1], &consumed);
        if (consumed != std::string(argv[1]).size()) throw std::invalid_argument("client ID");
    }
    catch (...) {
        SDL_Log("Client ID must be 1, 2, 3 or 4");
        return 1;
    }

    if (clientID < 1 || clientID > 4) {
        SDL_Log("Client ID must be 1, 2, 3 or 4");
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

    SDL_SetRenderVSync(renderer, 1);
    Scaling::setReferenceResolution(WINDOW_WIDTH, WINDOW_HEIGHT);
    Scaling::setMode(ScalingMode::PROPORTIONAL);

    constexpr float WORLD_WIDTH = 4400.0f;
    constexpr float SECOND_GAP_START = 2760.0f;
    constexpr float SECOND_GAP_END = 3000.0f;
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

    GameObject player = makePlayer(clientID, START_X, START_Y, PLAYER_WIDTH, PLAYER_HEIGHT);
    go::setGrounded(player, true);
    go::setSpriteSheet(player, CHARACTER_FRAME_COUNT,
        CHARACTER_FRAME_WIDTH,
        CHARACTER_FRAME_HEIGHT);
    setAnimationSpeed(player, 8.0f);

    GameObject rock = makeSceneObject("rock", 650.0f, GROUND_Y - 140.0f, 165.0f, 140.0f, true, true);
    GameObject cloud = makeSceneObject("cloud", 350.0f, 100.0f, 500.0f, 280.0f);
    GameObject sun = makeSceneObject("sun", 1250.0f, 100.0f, 310.0f, 310.0f);
    GameObject leftLand = makeSceneObject("leftLand", 0.0f, LAND_Y, GAP_START, 320.0f);
    GameObject rightLand = makeSceneObject("rightLand", GAP_END, LAND_Y, SECOND_GAP_START - GAP_END, 320.0f);
    GameObject tree = makeSceneObject("tree", 1500.0f, GROUND_Y - 360.0f, 300.0f, 360.0f);
    GameObject platform = makePlatform(1010.0f, 700.0f, PLATFORM_WIDTH, PLATFORM_HEIGHT);

    go::setSpriteSheet(cloud, 4, CLOUD_SUN_FRAME_WIDTH, CLOUD_SUN_FRAME_HEIGHT);
    setAnimationSpeed(cloud, 4.0f);
    go::setSpriteSheet(sun, 4, 542, CLOUD_SUN_FRAME_HEIGHT);
    setAnimationSpeed(sun, 5.0f);
    go::setSpriteSheet(tree, 4, TREE_FRAME_WIDTH, TREE_FRAME_HEIGHT);
    setAnimationSpeed(tree, 3.0f);

    SDL_Texture* characterTextures[5] = {
        nullptr,
        loadTexture(renderer, "../assets/bunny.png"),
        loadTexture(renderer, "../assets/fox.png"),
        loadTexture(renderer, "../assets/cat.png"),
        loadTexture(renderer, "../assets/otter.png")
    };

    SDL_Texture* cloudTexture = loadTexture(renderer, "../assets/cloud.png");
    SDL_Texture* sunTexture = loadTexture(renderer, "../assets/sun.png");
    SDL_Texture* landTexture = loadTexture(renderer, "../assets/land.png");
    SDL_Texture* rockTexture = loadTexture(renderer, "../assets/rock.png");
    SDL_Texture* treeTexture = loadTexture(renderer, "../assets/tree.png");

    go::setTexture(player, characterTextures[clientID]);
    go::setTexture(cloud, cloudTexture);
    go::setTexture(sun, sunTexture);
    go::setTexture(leftLand, landTexture);
    go::setTexture(rightLand, landTexture);
    go::setTexture(platform, landTexture);
    go::setTexture(rock, rockTexture);
    go::setTexture(tree, treeTexture);

    // These are created once so their animation does not reset every frame
    GameObject remoteBunny = makePlayer(1,
        0.0f, 0.0f,
        PLAYER_WIDTH, PLAYER_HEIGHT
    );

    GameObject remoteFox = makePlayer(2,
        0.0f, 0.0f,
        PLAYER_WIDTH, PLAYER_HEIGHT
    );

    GameObject remoteCat = makePlayer(3,
        0.0f, 0.0f,
        PLAYER_WIDTH, PLAYER_HEIGHT
    );

    GameObject remoteOtter = makePlayer(4,
        0.0f, 0.0f,
        PLAYER_WIDTH, PLAYER_HEIGHT
    );

    GameObject* remoteEntities[5] = {
        nullptr,
        &remoteBunny,
        &remoteFox,
        &remoteCat,
        &remoteOtter
    };

    bool remoteVisible[5] = {
        false,
        false,
        false,
        false,
        false
    };

    for (int id = 1; id <= 4; ++id) {
        remoteEntities[id]->removeComponent<GravityComponent>();
        remoteEntities[id]->getComponent<NetworkComponent>()->authority = NetworkAuthority::RemoteMirrored;
        go::setTexture(*remoteEntities[id], characterTextures[id]);

        go::setSpriteSheet(*remoteEntities[id], CHARACTER_FRAME_COUNT,
            CHARACTER_FRAME_WIDTH,
            CHARACTER_FRAME_HEIGHT);

        setAnimationSpeed(*remoteEntities[id], 8.0f);
    }

    Physics physics(1000.0f);
    Timeline gameTime;
    Timeline eventClock; // Unscaled, monotonic timestamps even when gameplay pauses.
    EventManager events;

    SharedData sharedData;
    sharedData.networkFormat = networkFormat;
    sharedData.eventManager = &events;
    events.setNetworkSink([&](const GameEvent& e) { sharedData.queueOutgoingEvent(e); });

    std::thread networkThread;

    bool running = true;
    bool pauseKeyWasPressed = false;
    bool minusKeyWasPressed = false;
    bool plusKeyWasPressed = false;
    bool scalingKeyWasPressed = false;

    float cloudSpeed = 90.0f;
    int cloudDirection = 1;
    constexpr float CLOUD_MIN_X = 250.0f;
    constexpr float CLOUD_MAX_X = 900.0f;

    // M3: additional scene objects 
    GameObject thirdLand = makeSceneObject("thirdLand", SECOND_GAP_END, LAND_Y,
                                          WORLD_WIDTH - SECOND_GAP_END, 320);
    go::setTexture(thirdLand, landTexture);
    GameObject distantTree = makeSceneObject("tree", 3850, GROUND_Y - 360, 300, 360);
    go::setTexture(distantTree, treeTexture);
    go::setSpriteSheet(distantTree, 4, TREE_FRAME_WIDTH, TREE_FRAME_HEIGHT);
    setAnimationSpeed(distantTree, 3);
    GameObject ledge1 = makeSceneObject("ledge", 430, 650, 200, 35, true, true);
    GameObject ledge2 = makeSceneObject("ledge", 1550, 640, 260, 42, true, true);
    GameObject ledge3 = makeSceneObject("ledge", 3550, 650, 300, 48, true, true);
    for (GameObject* ledge : {&ledge1, &ledge2, &ledge3}) go::setTexture(*ledge, landTexture);
    GameObject verticalPlatform = makePlatform(2820, 650, 190, 35);
    go::setTexture(verticalPlatform, landTexture);
    platform.addComponent<MovingPlatformComponent>(1010, 700, 95, 0, 5);
    verticalPlatform.addComponent<MovingPlatformComponent>(2820, 650, 0, 100, 6);

    // Hidden ground colliders preserve that same visual/collision arrangement
    GameObject ground1 = makeSceneObject("ground", 0, GROUND_Y, GAP_START, 400, false, true);
    GameObject ground2 = makeSceneObject("ground", GAP_END, GROUND_Y,
                                        SECOND_GAP_START - GAP_END, 400, false, true);
    GameObject ground3 = makeSceneObject("ground", SECOND_GAP_END, GROUND_Y,
                                        WORLD_WIDTH - SECOND_GAP_END, 400, false, true);
    std::vector<GameObject*> solidObjects = {
        &ground1, &ground2, &ground3, &rock, &ledge1, &ledge2, &ledge3,
        &platform, &verticalPlatform
    };
    std::array<GameObject*, 2> movingPlatforms = {&platform, &verticalPlatform};

    std::vector<GameObject> spawnPoints;
    for (int i = 0; i < 4; ++i) {
        GameObject spawn = makeSceneObject("spawn", START_X - i * 50, START_Y,
                                           PLAYER_WIDTH, PLAYER_HEIGHT, false);
        spawn.addComponent<SpawnPointComponent>(i);
        spawnPoints.push_back(std::move(spawn));
    }
    GameObject deathZone = makeSceneObject("death-zone", -300, 1040,
                                          WORLD_WIDTH + 600, 500, false, true);
    deathZone.getComponent<CollidableComponent>()->solid = false;
    deathZone.addComponent<DeathZoneComponent>();

    // The bomb patrols the second island, away from spawn points and ledges.
    // Its fuse and transparent sprite margins are outside the lethal body.
    SDL_Texture* bombTexture = loadTexture(renderer, "../assets/bomb.png");
    GameObject bomb = makeSceneObject("bomb", 2200, GROUND_Y - 174, 135, 180);
    go::setTexture(bomb, bombTexture);
    go::setSpriteSheet(bomb, 4, 512, 682);
    setAnimationSpeed(bomb, 8);
    GameObject bombBody = makeSceneObject("bomb-body", 2222, GROUND_Y - 110,
                                         99, 110, false, true);
    bombBody.getComponent<CollidableComponent>()->solid = false;

    float cameraX = 0;
    GameObject* standingOn = nullptr;
    bool jumpKeyWasPressed = false;
    bool restartKeyWasPressed = false;
    bool cheerKeyWasPressed = false;
    bool debugKeyWasPressed = false;
    bool showHiddenObjects = false;

    // The Milestone 4 team manager orders events by priority, timestamp and
    // insertion order. These handlers run on the main thread, including those
    // raised by the networking worker.
    float horizontalSpeed = 0.0f;
    int boostFrom = 0;
    std::string status = "Q: cheer for teammates";
    std::string displayedTitle;
    using Clock = std::chrono::steady_clock;
    auto nextCheer = Clock::now();

    events.subscribe(EventType::Spawn, [&](const GameEvent& e) {
        if (e.sourceId != clientID || e.targetId != clientID) return;
        const GameObject& spawn = spawnPoints.at(clientID - 1);
        go::setPosition(player, go::getX(spawn), go::getY(spawn));
        go::setVelocity(player, 0, 0);
        go::setGrounded(player, true);
        standingOn = &ground1;
        cameraX = 0;
        boostFrom = 0;
        status = e.detail == "bomb" ? "Hit by bomb | Respawned" :
                 e.detail == "death-zone" ? "Respawned after fall" : "At spawn | Q: cheer";
        SDL_Log("[Spawn] %s returned to spawn %d (%s)", characterName(clientID), clientID, e.detail.c_str());
    });

    events.subscribe(EventType::Death, [&](const GameEvent& e) {
        if (e.sourceId != clientID || e.targetId != clientID ||
            (e.detail != "death-zone" && e.detail != "bomb")) return;
        SDL_Log("[Death] %s %s", characterName(clientID),
                e.detail == "bomb" ? "was hit by the bomb" : "fell into the death zone");
        events.raise({EventType::Spawn, EventPriority::Critical, eventClock.getTime(),
                      clientID, clientID, e.detail});
    });

    events.subscribe(EventType::Collision, [&](const GameEvent& e) {
        if (e.sourceId != clientID) return;
        if (e.detail == "death-zone" || e.detail == "bomb") {
            if (e.targetId != clientID) return;
            SDL_Log("[Collision] %s touched %s", characterName(clientID),
                    e.detail == "bomb" ? "the bomb" : "the death zone");
            events.raise({EventType::Death, EventPriority::Critical, eventClock.getTime(),
                          clientID, clientID, e.detail});
        } else if (e.detail == "side") {
            go::setPosition(player, e.x, go::getY(player));
        } else if (e.detail == "ceiling") {
            go::setPosition(player, go::getX(player), e.y);
            go::setVelocityY(player, 0);
        } else if (e.detail == "land" && e.targetId >= 0 &&
                   static_cast<std::size_t>(e.targetId) < solidObjects.size()) {
            if (showHiddenObjects && standingOn != solidObjects[e.targetId])
                SDL_Log("[Collision] %s landed on %s", characterName(clientID),
                        solidObjects[e.targetId]->getType().c_str());
            go::setPosition(player, go::getX(player), e.y - PLAYER_HEIGHT);
            go::setVelocityY(player, 0);
            go::setGrounded(player, true);
            standingOn = solidObjects[e.targetId];
        }
    });

    events.subscribe(EventType::Input, [&](const GameEvent& e) {
        // A cheer is the only remote command that changes local gameplay.
        // The source receives its own event too, but cannot boost itself.
        if (e.detail == "bunny.cheer") {
            if (e.sourceId == clientID || e.sourceId < 1 || e.sourceId > 4 || e.targetId != 0) return;
            boostFrom = e.sourceId; // One charge; repeated cheers do not stack.
            status = std::string("Boost ready from ") + characterName(boostFrom) + " | W or Space";
            SDL_Log("[Network Input] Cheer from %s: next grounded jump boosted", characterName(boostFrom));
            return;
        }
        if (e.sourceId != clientID || e.targetId != clientID) return;
        if (e.detail == "move") {
            horizontalSpeed = e.x;
        } else if (e.detail == "jump" && !gameTime.isPaused() &&
                   player.getComponent<GravityComponent>()->grounded) {
            physics.jump(player, boostFrom ? JUMP_STRENGTH * 1.30f : JUMP_STRENGTH);
            standingOn = nullptr;
            SDL_Log("[Input] %s jumped%s", characterName(clientID), boostFrom ? " with a cheer boost" : "");
            if (boostFrom) status = "Boost used | Q: cheer";
            boostFrom = 0;
        } else if (e.detail == "restart") {
            events.raise({EventType::Spawn, EventPriority::High, eventClock.getTime(),
                          clientID, clientID, "manual restart"});
        } else if (e.detail == "cheer") {
            if (offline) {
                status = "Cheer needs online mode";
                return;
            }
            // PUB/SUB is best effort. Check that a peer has been seen before
            // inviting the player to try the boost with a second window.
            {
                std::lock_guard<std::mutex> lock(sharedData.playerMutex);
                if (sharedData.remotePlayers.empty()) {
                    status = "Open a second client to cheer";
                    return;
                }
            }
            const auto now = Clock::now();
            if (now < nextCheer) return;
            nextCheer = now + std::chrono::seconds(2);
            events.raiseNetworked({EventType::Input, EventPriority::High, eventClock.getTime(),
                                   clientID, 0, "bunny.cheer"});
            if (!boostFrom) status = "Cheer sent | Watch the other client";
            SDL_Log("[Network Input] %s sent a cheer", characterName(clientID));
        }
    });

    events.raise({EventType::Spawn, EventPriority::High, eventClock.getTime(),
                  clientID, clientID, "start"});
    events.dispatch();
    {
        std::lock_guard<std::mutex> lock(sharedData.playerMutex);
        sharedData.playerX = go::getX(player);
        sharedData.playerY = go::getY(player);
    }
    if (!offline) networkThread = std::thread(networkingThread, std::ref(sharedData), clientID);
    // Initialize phase before the first update so a newly joined player is not
    // carried by a large initial platform displacement.
    auto updateMovingPlatforms = [&](double seconds, bool carryRider) {
        for (GameObject* object : movingPlatforms) {
            auto* motion = object->getComponent<MovingPlatformComponent>();
            double phase = std::fmod(seconds, motion->period) / motion->period * 6.283185307179586;
            float x = motion->originX + static_cast<float>(std::sin(phase)) * motion->amplitudeX;
            float y = motion->originY + static_cast<float>(std::sin(phase)) * motion->amplitudeY;
            if (carryRider && standingOn == object &&
                player.getComponent<GravityComponent>()->grounded) {
                go::move(player, x - go::getX(*object), y - go::getY(*object));
            }
            go::setPosition(*object, x, y);
        }
    };
    auto sharedSeconds = []() {
        return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    };
    updateMovingPlatforms(sharedSeconds(), false);
    SDL_Log("M4: A/D move, W/Space jump, Shift run, Q cheer, R respawn, P pause, -/+ speed, T scale, F1 debug");
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        if (!running) break;

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

        bool debugPressed = Input::isKeyPressed(SDL_SCANCODE_F1);
        if (debugPressed && !debugKeyWasPressed) showHiddenObjects = !showHiddenObjects;
        debugKeyWasPressed = debugPressed;

        // SDL polls the keys; all gameplay input goes through the team queue.
        auto raiseOnPress = [&](bool pressed, bool& wasPressed, const char* detail) {
            if (pressed && !wasPressed)
                events.raise({EventType::Input, EventPriority::Normal, eventClock.getTime(),
                              clientID, clientID, detail});
            wasPressed = pressed;
        };
        raiseOnPress(Input::isKeyPressed(SDL_SCANCODE_R), restartKeyWasPressed, "restart");
        raiseOnPress(Input::isKeyPressed(SDL_SCANCODE_Q), cheerKeyWasPressed, "cheer");
        const double nowSeconds = sharedSeconds();
        updateMovingPlatforms(nowSeconds, true);
        // Use the shared wall clock so all localhost clients see the same patrol.
        const float bombX = 2200 + 200 * std::sin(std::fmod(nowSeconds, 6.0) * 6.283185307179586 / 6.0);
        go::setPosition(bomb, bombX, GROUND_Y - 174);
        go::setPosition(bombBody, bombX + 22, GROUND_Y - 110);
        raiseOnPress(Input::isKeyPressed(SDL_SCANCODE_W) || Input::isKeyPressed(SDL_SCANCODE_SPACE),
                     jumpKeyWasPressed, "jump");

        horizontalSpeed = 0;
        if (!gameTime.isPaused()) {
            const bool runningInput = Input::isKeyPressed(SDL_SCANCODE_LSHIFT) ||
                                      Input::isKeyPressed(SDL_SCANCODE_RSHIFT);
            const int direction = static_cast<int>(Input::isKeyPressed(SDL_SCANCODE_D)) -
                                  static_cast<int>(Input::isKeyPressed(SDL_SCANCODE_A));
            if (direction)
                events.raise({EventType::Input, EventPriority::Normal, eventClock.getTime(),
                              clientID, clientID, "move", direction * (runningInput ? RUN_SPEED : WALK_SPEED)});
        }
        events.dispatch(); // Input and network messages.
        events.dispatch(); // Manual restart requests queued by an input handler.

        if (!gameTime.isPaused()) {
            // Keep the Milestone 3 substeps. Resolve queued collisions before
            // continuing the same substep so the player cannot sink into a ledge.
            const int steps = std::max(1, static_cast<int>(std::ceil(deltaTime / (1.0f / 120.0f))));
            const float stepTime = deltaTime / steps;
            for (int step = 0; step < steps && stepTime > 0; ++step) {
                const float previousX = go::getX(player);
                const float previousY = go::getY(player);
                go::move(player, horizontalSpeed * stepTime, 0);
                for (GameObject* object : solidObjects) {
                    if (!Collision::checkCollision(player, *object)) continue;
                    const float left = go::getX(*object);
                    const float right = left + go::getWidth(*object);
                    if (previousX + PLAYER_WIDTH <= left + 0.5f) {
                        events.raise({EventType::Collision, EventPriority::High, eventClock.getTime(),
                                      clientID, 0, "side", left - PLAYER_WIDTH});
                        events.dispatch();
                    } else if (previousX >= right - 0.5f) {
                        events.raise({EventType::Collision, EventPriority::High, eventClock.getTime(),
                                      clientID, 0, "side", right});
                        events.dispatch();
                    }
                }
                go::setPosition(player, std::clamp(go::getX(player), 0.0f,
                                WORLD_WIDTH - PLAYER_WIDTH), go::getY(player));
                physics.update(player, stepTime);

                int landingIndex = -1;
                float landingY = 100000;
                for (std::size_t i = 0; i < solidObjects.size(); ++i) {
                    const GameObject& object = *solidObjects[i];
                    const bool overlap = go::getX(player) + PLAYER_WIDTH > go::getX(object) &&
                        go::getX(player) < go::getX(object) + go::getWidth(object);
                    if (!overlap) continue;
                    const float top = go::getY(object);
                    const float bottom = top + go::getHeight(object);
                    if (go::getVelocityY(player) >= 0 && previousY + PLAYER_HEIGHT <= top + 1 &&
                        go::getY(player) + PLAYER_HEIGHT >= top && top < landingY) {
                        landingY = top;
                        landingIndex = static_cast<int>(i);
                    }
                    if (go::getVelocityY(player) < 0 && previousY >= bottom - 1 && go::getY(player) < bottom) {
                        events.raise({EventType::Collision, EventPriority::High, eventClock.getTime(),
                                      clientID, 0, "ceiling", 0, bottom});
                        events.dispatch();
                    }
                }
                if (landingIndex >= 0) {
                    events.raise({EventType::Collision, EventPriority::High, eventClock.getTime(),
                                  clientID, landingIndex, "land", 0, landingY});
                    events.dispatch();
                } else {
                    go::setGrounded(player, false);
                    standingOn = nullptr;
                }

                const char* hazard = Collision::checkCollision(player, bombBody) ? "bomb" :
                    Collision::checkCollision(player, deathZone) ? "death-zone" : nullptr;
                if (hazard) {
                    events.raise({EventType::Collision, EventPriority::Critical, eventClock.getTime(),
                                  clientID, clientID, hazard, go::getX(player), go::getY(player)});
                    events.dispatch(); // Collision -> queue Death
                    events.dispatch(); // Death -> queue Spawn
                    events.dispatch(); // Spawn -> reset position and velocity
                    break;
                }
            }
        }
        cameraX = std::clamp(go::getX(player) + PLAYER_WIDTH / 2 - WINDOW_WIDTH / 2,
                             0.0f, WORLD_WIDTH - WINDOW_WIDTH);

        // Reset visibility before reading the newest network information
        for (int id = 1; id <= 4; ++id) {
            remoteVisible[id] = false;
        }

        {
            std::lock_guard<std::mutex> lock(
                sharedData.playerMutex
            );

            for (const auto& entry : sharedData.remotePlayers) {
                int remoteID = entry.first;

                if (remoteID < 1 || remoteID > 4) {
                    continue;
                }

                if (remoteID == clientID) {
                    continue;
                }

                const RemotePlayerState& remote = entry.second;

                go::setPosition(*remoteEntities[remoteID], remote.x,
                    remote.y);

                remoteVisible[remoteID] = true;
            }
        }

        if (!gameTime.isPaused()) {
            go::move(cloud, cloudSpeed * cloudDirection * deltaTime, 0.0f);
            if (go::getX(cloud) >= CLOUD_MAX_X) {
                go::setPosition(cloud, CLOUD_MAX_X, go::getY(cloud));
                cloudDirection = -1;
            }
            else if (go::getX(cloud) <= CLOUD_MIN_X) {
                go::setPosition(cloud, CLOUD_MIN_X, go::getY(cloud));
                cloudDirection = 1;
            }

            updateAnimation(player, deltaTime);
            updateAnimation(cloud, deltaTime);
            updateAnimation(sun, deltaTime);
            updateAnimation(tree, deltaTime);
            updateAnimation(distantTree, deltaTime);
            updateAnimation(bomb, deltaTime);

            // Animate remote characters even when they are standing still
            for (int id = 1; id <= 4; ++id) {
                if (id != clientID &&
                    remoteVisible[id] &&
                    remoteEntities[id] != nullptr) {
                    updateAnimation(*remoteEntities[id], deltaTime);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(sharedData.playerMutex);
            sharedData.playerX = go::getX(player);
            sharedData.playerY = go::getY(player);
        }

        // Pink and white ombre background
        constexpr int BACKGROUND_BANDS = 40;
        int outputWidth, outputHeight;
        SDL_GetRenderOutputSize(renderer, &outputWidth, &outputHeight);
        const float BAND_HEIGHT =
            static_cast<float>(outputHeight) / BACKGROUND_BANDS;

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
                static_cast<float>(outputWidth),
                BAND_HEIGHT + 1.0f
            };
            SDL_RenderFillRect(renderer, &band);
        }

        renderWorld(cloud, renderer, cameraX);
        renderWorld(sun, renderer, cameraX);
        renderWorld(leftLand, renderer, cameraX);
        renderWorld(rightLand, renderer, cameraX);
        renderWorld(tree, renderer, cameraX);
        renderWorld(rock, renderer, cameraX);
        renderWorld(platform, renderer, cameraX);
        renderWorld(thirdLand, renderer, cameraX);
        renderWorld(distantTree, renderer, cameraX);
        renderWorld(ledge1, renderer, cameraX);
        renderWorld(ledge2, renderer, cameraX);
        renderWorld(ledge3, renderer, cameraX);
        renderWorld(verticalPlatform, renderer, cameraX);
        renderWorld(bomb, renderer, cameraX);

        // Render the persistent remote entities
        for (int id = 1; id <= 4; ++id) {
            if (id != clientID &&
                remoteVisible[id] &&
                remoteEntities[id] != nullptr) {
                renderWorld(*remoteEntities[id], renderer, cameraX);
            }
        }

        renderWorld(player, renderer, cameraX);

        // A gold sparkle shows that this player has a cheer jump ready.
        if (boostFrom) {
            float sx = 1, sy = 1;
            if (Scaling::getMode() == ScalingMode::PROPORTIONAL)
                Scaling::getScaleFactors(outputWidth, outputHeight, sx, sy);
            const float x = (go::getX(player) + PLAYER_WIDTH / 2 - cameraX) * sx;
            const float y = (go::getY(player) + 12) * sy;
            SDL_SetRenderDrawColor(renderer, 242, 170, 35, 255);
            const SDL_FRect across{x - 12 * sx, y - 3 * sy, 24 * sx, 6 * sy};
            const SDL_FRect upright{x - 3 * sx, y - 12 * sy, 6 * sx, 24 * sy};
            SDL_RenderFillRect(renderer, &across);
            SDL_RenderFillRect(renderer, &upright);
        }

        // Debug outlines do not add RenderableComponent to hidden gameplay objects
        if (showHiddenObjects) {
            float sx = 1, sy = 1;
            if (Scaling::getMode() == ScalingMode::PROPORTIONAL) {
                int w, h;
                SDL_GetRenderOutputSize(renderer, &w, &h);
                Scaling::getScaleFactors(w, h, sx, sy);
            }
            auto outline = [&](const GameObject& object) {
                SDL_FRect rectangle = {(go::getX(object) - cameraX) * sx, go::getY(object) * sy,
                                       go::getWidth(object) * sx, go::getHeight(object) * sy};
                SDL_RenderRect(renderer, &rectangle);
            };
            SDL_SetRenderDrawColor(renderer, 50, 140, 220, 255);
            for (const auto& spawn : spawnPoints) outline(spawn);
            SDL_SetRenderDrawColor(renderer, 235, 40, 90, 255);
            outline(deathZone);
            outline(bombBody);
            SDL_SetRenderDrawColor(renderer, 100, 170, 90, 255);
            for (GameObject* object : solidObjects) outline(*object);
        }

        std::string title = windowTitle + " | " + status;
        if (boostFrom)
            title = windowTitle + " | Boost ready from " + characterName(boostFrom) + " | W or Space";
        if (title != displayedTitle) {
            SDL_SetWindowTitle(window, title.c_str());
            displayedTitle = std::move(title);
        }
        SDL_RenderPresent(renderer);
        SDL_Delay(1);
    }

    sharedData.running.store(false);
    if (networkThread.joinable()) {
        networkThread.join();
    }

    for (int id = 1; id <= 4; ++id) {
        if (characterTextures[id]) SDL_DestroyTexture(characterTextures[id]);
    }
    if (cloudTexture) SDL_DestroyTexture(cloudTexture);
    if (sunTexture) SDL_DestroyTexture(sunTexture);
    if (landTexture) SDL_DestroyTexture(landTexture);
    if (rockTexture) SDL_DestroyTexture(rockTexture);
    if (treeTexture) SDL_DestroyTexture(treeTexture);
    if (bombTexture) SDL_DestroyTexture(bombTexture);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
