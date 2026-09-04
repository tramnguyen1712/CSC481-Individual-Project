# CSC481 Individual Project - Bunny Hop
Bunny Hop is an individual game created for the CSC 481 Game Engine Construction Project. The game uses the engine foundation developed by Team 8 and demonstrates entity management, configurable physics, keyboard input, collision detection, animation, and display scaling.

The player controls a bunny that can walk, run, and jump. The bunny must avoid a rock and cross the gap between the two platforms. Falling into the gap causes the game to reset.

## Author

Tram Nguyen, tnnguy22

##  Setup Instructions

Follow these steps to clone, build, and run the project on your machine:

### 1. Clone the repository

```bash
git clone https://github.com/tramnguyen1712/CSC481-Individual-Project.git
```

----------

### 2. Install SDL3

> SDL3 is the latest major release. You’ll need both SDL3 and optionally `SDL3_image` if the sprite sheet is in PNG format.

#### On **macOS** (with Homebrew):

```bash
brew install sdl3

```

#### On **Ubuntu/Debian**:

```bash
sudo apt install libsdl3-dev

```

#### On **Arch Linux**:

```bash
sudo pacman -S sdl3

```

#### On **Windows**:

Use [MSYS2](https://www.msys2.org/) or [vcpkg](https://vcpkg.io/) for easiest installation.

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL3

```

Then launch the MinGW64 shell and continue.

----------

### 3. Build the project

```bash
mkdir build && cd build
cmake ..
make

```

> Ensure `SDL3_DIR` is correctly set if CMake cannot find SDL.

----------

### 5. Run the animation

```bash
./main

```

You should see a ran successfully in the console.

