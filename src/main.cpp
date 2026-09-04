#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>

#include "Entity.h"
#include "Physics.h"
#include "Input.h"
#include "Collision.h"
#include "Scaling.h"

const int WINDOW_WIDTH = 1920;
const int WINDOW_HEIGHT = 1080;

// bunny, cloud, and sun each contain 4 horizonal frames
const int FRAME_COUNT = 4;
const int FRAME_WIDTH = 543;
const int FRAME_HEIGHT = 724;


int main(int argc, char *argv[])
{
    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Could not initialize SDL: %s", SDL_GetError());
        return 1;
    }

    // Create window and renderer
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;

    if (!SDL_CreateWindowAndRenderer(
            "Bunny Hop",
            WINDOW_WIDTH,
            WINDOW_HEIGHT,
            SDL_WINDOW_RESIZABLE,
            &window,
            &renderer)) {

        SDL_Log("Could not create window/renderer: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Set the reference resolution for proportional scaling
    Scaling::setReferenceResolution(WINDOW_WIDTH, WINDOW_HEIGHT);


    // game measuments
    const float LAND_Y = 760.0f;
    const float GROUND_Y = 840.0f;

    const float GAP_START = 970.0f;
    const float GAP_END = 1210.0f;

    const float BUNNY_WIDTH = 220.0f;
    const float BUNNY_HEIGHT = 280.0f;

    const float START_X = 170.0f;
    const float START_Y = GROUND_Y - BUNNY_HEIGHT;

    const float WALK_SPEED = 350.0f;
    const float RUN_SPEED = 600.0f;
    const float JUMP_STRENGTH = 700.0f;

    // Create generic entity
    Entity bunny(START_X, START_Y, BUNNY_WIDTH, BUNNY_HEIGHT);

    // Controllable entity: player (bunny)
    bunny.setGravityEnabled(true);
    bunny.setGrounded(true); // Start on the ground
    bunny.setSpriteSheet(FRAME_COUNT, FRAME_WIDTH, FRAME_HEIGHT);
    bunny.setAnimationSpeed(8.0f);

    // Static entity: rock
    Entity rock(
        650.0f,
        GROUND_Y - 140.0f,
        165.0f,
        140.0f
    );

    rock.setGravityEnabled(false);

    // Automatically moving entity: cloud
    Entity cloud(
        350.0f,
        100.0f,
        500.0f,
        280.0f
    );

    cloud.setGravityEnabled(false);
    cloud.setSpriteSheet(
        FRAME_COUNT,
        FRAME_WIDTH,
        FRAME_HEIGHT
    );
    cloud.setAnimationSpeed(4.0f);

    // Decorative animated sun
    Entity sun(
        1250.0f,
        100.0f,
        310.0f,
        310.0f
    );

    sun.setGravityEnabled(false);
    sun.setSpriteSheet(
        FRAME_COUNT,
        FRAME_WIDTH,
        FRAME_HEIGHT
    );
    sun.setAnimationSpeed(5.0f);


    // Two ground sections create the gap.
    Entity leftLand(
        0.0f,
        LAND_Y,
        GAP_START,
        320.0f
    );

    Entity rightLand(
        GAP_END,
        LAND_Y,
        WINDOW_WIDTH - GAP_END,
        320.0f
    );

    leftLand.setGravityEnabled(false);
    rightLand.setGravityEnabled(false);

    // Entity: Tree
    Entity tree(
        1500.0f,
        GROUND_Y - 360.0f,
        300.0f,
        360.0f
    );

    tree.setGravityEnabled(false);
    tree.setSpriteSheet(
        FRAME_COUNT,
        FRAME_WIDTH,
        FRAME_HEIGHT
    );

    tree.setAnimationSpeed(3.0f);


    // Load textures
    SDL_Texture* bunnyTexture =
        IMG_LoadTexture(renderer, "../assets/bunny.png");

    SDL_Texture* cloudTexture =
        IMG_LoadTexture(renderer, "../assets/cloud.png");

    SDL_Texture* sunTexture =
        IMG_LoadTexture(renderer, "../assets/sun.png");

    SDL_Texture* landTexture =
        IMG_LoadTexture(renderer, "../assets/land.png");

    SDL_Texture* rockTexture =
        IMG_LoadTexture(renderer, "../assets/rock.png");
    
    SDL_Texture* treeTexture =
        IMG_LoadTexture(renderer, "../assets/tree.png");


    // Check if the texture loaded correctly
    if (!bunnyTexture) {
        SDL_Log("Could not load texture: %s", SDL_GetError());
    }
    else {
        bunny.setTexture(bunnyTexture);
    }

    if (!cloudTexture) {
        SDL_Log("Could not load cloud texture: %s",SDL_GetError()
        );
    }
    else {
        cloud.setTexture(cloudTexture);
    }

    if (!sunTexture) {
        SDL_Log("Could not load sun texture: %s", SDL_GetError());
    }
    else {
        sun.setTexture(sunTexture);
    }

    if (!landTexture) {
        SDL_Log("Could not load land texture: %s", SDL_GetError());
    }
    else {
        leftLand.setTexture(landTexture);
        rightLand.setTexture(landTexture);
    }

    if (!rockTexture) {
        SDL_Log("Could not load rock texture: %s", SDL_GetError());
    }
    else {
        rock.setTexture(rockTexture);
    }

    if (!treeTexture) {
        SDL_Log("Could not load tree texture: %s", SDL_GetError());
    }
    else {
        tree.setTexture(treeTexture);
    }


    // Physics and game state
    Physics physics;
    physics.setGravity(1000.0f); 

    float cloudSpeed = 90.0f;
    int cloudDirection = 1;

    const float CLOUD_MIN_X = 250.0f;
    const float CLOUD_MAX_X = 900.0f;

    bool running = true;
    bool gameOver = false;
    bool scalingKeyWasPressed = false;

    SDL_Event event;
    Uint64 lastTime = SDL_GetTicks();


    // Main game loop
    while (running) {

        // Check if user closes window
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        Uint64 currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;

        // Prevent large movements after pausing or dragging window.
        if (deltaTime > 0.05f) {
            deltaTime = 0.05f;
        }

        // Toggle scaling with T
        bool scalingKeyIsPressed =
            Input::isKeyPressed(SDL_SCANCODE_T);

        if (scalingKeyIsPressed && !scalingKeyWasPressed) {
            Scaling::toggleMode();

            SDL_Log(
                "Scaling mode: %s",
                Scaling::getMode() == ScalingMode::PROPORTIONAL
                    ? "PROPORTIONAL"
                    : "PIXEL"
            );
        }

        scalingKeyWasPressed = scalingKeyIsPressed;

        // Restart after falling to the gap by pressing R.
        if (gameOver && Input::isKeyPressed(SDL_SCANCODE_R)) {
            bunny.setPosition(START_X, START_Y);
            bunny.setVelocity(0.0f, 0.0f);
            bunny.setGrounded(true);
            gameOver = false;

            SDL_Log("Game restarted!");
        }

        // A: walk left
        // D: walk right
        // W: jump
        // W + A: jump left
        // W + D: jump right
        // Shift + A: run left
        // Shift + D: run right

        if (!gameOver) {
            float previousX = bunny.getX();
            // Walk normally and run while either Shift key is held.
            float moveSpeed = WALK_SPEED;

            // Shift key held down increases speed to run
            if (Input::isKeyPressed(SDL_SCANCODE_LSHIFT) || Input::isKeyPressed(SDL_SCANCODE_RSHIFT)) {
                moveSpeed = RUN_SPEED;
            }

            // A moves left
            if (Input::isKeyPressed(SDL_SCANCODE_A)) {
                bunny.move(-moveSpeed * deltaTime, 0.0f);
            }

            // D moves right
            if (Input::isKeyPressed(SDL_SCANCODE_D)) {
                bunny.move(moveSpeed * deltaTime, 0.0f);
            }

            // W jumps
            if (Input::isKeyPressed(SDL_SCANCODE_W)) {
                physics.jump(bunny, JUMP_STRENGTH);
            }

            // Keep the bunny inside the left and right boundaries.
            if (bunny.getX() < 0.0f) {
                bunny.setPosition(0.0f, bunny.getY());
            }

            if (bunny.getX() + bunny.getWidth() > WINDOW_WIDTH) {
                bunny.setPosition(
                    WINDOW_WIDTH - bunny.getWidth(),
                    bunny.getY()
                );
            }

            physics.update(bunny, deltaTime);


            // Ground and gap detection
            float bunnyLeft = bunny.getX();
            float bunnyRight =
                bunny.getX() + bunny.getWidth();
            float bunnyBottom =
                bunny.getY() + bunny.getHeight();

            // Bunny is completely inside the gap when it has
            // moved past the left platform and has not reached
            // the right platform.
            float bunnyCenterX =
                bunny.getX() + bunny.getWidth() / 2.0f;

            bool overGap =
                bunnyCenterX > GAP_START &&
                bunnyCenterX < GAP_END;

            if (!overGap &&
                bunnyBottom >= GROUND_Y &&
                bunny.getVelocityY() >= 0.0f) {

                bunny.setPosition(
                    bunny.getX(),
                    GROUND_Y - bunny.getHeight()
                );

                bunny.setVelocityY(0.0f);
                bunny.setGrounded(true);
            }
            else {
                bunny.setGrounded(false);
            }

            // Collision response: rock blocks the bunny
            if (Collision::checkCollision(bunny, rock)) {
                bunny.setPosition(previousX, bunny.getY());
            }


            // Falling into the gap causes game over
            if (bunny.getY() > WINDOW_HEIGHT) {
                gameOver = true;
                bunny.setVelocity(0.0f, 0.0f);

                SDL_Log(
                    "GAME OVER! Press R to restart."
                );
            }
        }

        // Automatically move the cloud
        cloud.move(
            cloudSpeed * cloudDirection * deltaTime,
            0.0f
        );

        if (cloud.getX() >= CLOUD_MAX_X) {
            cloud.setPosition(CLOUD_MAX_X, cloud.getY());
            cloudDirection = -1;
        }
        else if (cloud.getX() <= CLOUD_MIN_X) {
            cloud.setPosition(CLOUD_MIN_X, cloud.getY());
            cloudDirection = 1;
        }


        // Update sprite animations
        bunny.updateAnimation(deltaTime);
        cloud.updateAnimation(deltaTime);
        sun.updateAnimation(deltaTime);
        tree.updateAnimation(deltaTime);


        // Pink and White ombre gradient background
        const int BACKGROUND_BANDS = 40;
        const float BAND_HEIGHT =
            static_cast<float>(WINDOW_HEIGHT) /
            BACKGROUND_BANDS;

        for (int i = 0; i < BACKGROUND_BANDS; i++) {
            float amount =
                static_cast<float>(i) /
                static_cast<float>(BACKGROUND_BANDS - 1);

            Uint8 red = 255;
            Uint8 green =
                static_cast<Uint8>(250.0f - 55.0f * amount);
            Uint8 blue =
                static_cast<Uint8>(252.0f - 35.0f * amount);

            SDL_SetRenderDrawColor(
                renderer,
                red,
                green,
                blue,
                255
            );

            SDL_FRect band = {
                0.0f,
                i * BAND_HEIGHT,
                static_cast<float>(WINDOW_WIDTH),
                BAND_HEIGHT + 1.0f
            };

            SDL_RenderFillRect(renderer, &band);
        }

        // Render the game scene
        cloud.render(renderer);
        sun.render(renderer);

        leftLand.render(renderer);
        rightLand.render(renderer);
        tree.render(renderer);
        rock.render(renderer);

        if (!gameOver) {
            bunny.render(renderer);
        }

        // Pink overlay when the game is over.
        if (gameOver) {
            SDL_SetRenderDrawBlendMode(
                renderer,
                SDL_BLENDMODE_BLEND
            );

            SDL_SetRenderDrawColor(
                renderer,
                255,
                180,
                205,
                110
            );

            SDL_FRect overlay = {
                0.0f,
                0.0f,
                static_cast<float>(WINDOW_WIDTH),
                static_cast<float>(WINDOW_HEIGHT)
            };

            SDL_RenderFillRect(renderer, &overlay);
        }

        SDL_RenderPresent(renderer);
    }


    // Clean up
    SDL_DestroyTexture(bunnyTexture);
    SDL_DestroyTexture(cloudTexture);
    SDL_DestroyTexture(sunTexture);
    SDL_DestroyTexture(landTexture);
    SDL_DestroyTexture(rockTexture);
    SDL_DestroyTexture(treeTexture);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}