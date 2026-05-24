# 3D-Car-Parking
Computer Graphics
# 3D Car Parking Simulator

A realistic, interactive 3D car parking simulator built with C++ and OpenGL/GLUT. Practice your parking skills in a simulated environment featuring multiple cars, obstacles, and dynamic visual effects.

## Features

- 3D parking environment with buildings, trees, lane markings, and animated sky
- Five selectable cars, each with independent controls and parking logic
- Realistic driving physics: steering, acceleration, braking, reverse, collision detection
- On-screen HUD with parking spot mini-map, messages, and controls reference
- Camera: mouse drag to orbit, pan/zoom, and quick keyboard presets (top/front/side/rear)
- Visual and text feedback for successful/failed parking attempts
- Adjustable cloud animation and car speed
- Cross-platform: works on Windows, Linux, macOS

## Controls

| Key             | Action                                         |
|-----------------|------------------------------------------------|
| 1,2,3,4,5       | Select active car                              |
| Arrow keys      | Drive/steer selected car                       |
| U then 1-5      | Unpark a parked car                            |
| R               | Reset all cars and camera                      |
| + / - / 0       | Increase / decrease / reset car speed          |
| [ / ]           | Adjust cloud animation speed                   |
| , / .           | Adjust cloud size                              |
| T               | Top-down camera view                           |
| F / B / L / R   | Front/Back/Left/Right camera preset            |
| Mouse           | Drag to orbit (left button), pan (middle), zoom (scroll) |
| Esc             | Exit                                           |

## Prerequisites

- C++ compiler (GCC/Clang/MSVC)
- OpenGL
- GLUT, FreeGLUT, or an equivalent windowing/input library

## Build & Run

### Linux/macOS
```bash
sudo apt-get install freeglut3-dev      # Ubuntu/Debian
brew install freeglut                   # macOS
g++ car_parking.cpp -o car_parking -lGL -lGLU -lglut
./car_parking
```

### Windows (MSYS2/MinGW example)
```bash
pacman -S mingw-w64-x86_64-freeglut
g++ car_parking.cpp -o car_parking.exe -lfreeglut -lopengl32 -lglu32
car_parking.exe
```

## File Structure

- `car_parking.cpp` — Main program source
- `README.md` — This documentation



## Credits

Created by [srimanth009](https://github.com/srimanth009)  
Built using OpenGL and GLUT

---

**Have fun parking in 3D!**
