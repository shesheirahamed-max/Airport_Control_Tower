# 3D Airport Air Traffic Control (ATC) Tower Simulation

A 3D Computer Graphics project developed using C++ and OpenGL (FreeGLUT) for the Computer Graphics and Animation Lab.

## 📌 Project Overview
This project simulates an elevated Air Traffic Control (ATC) tower interior with a panoramic view of an outdoor airport runway. It includes interactive 3D camera controls, continuous rotation animation, material shading with dual light sources, and surface textures.

## 🚀 Features
- **Full 3D Rendering**: Built using OpenGL 3D perspective pipeline (`gluPerspective`, `gluLookAt`).
- **3D Scene Architecture**: Detailed indoor control desk, radar panels, and monitors along with an outdoor runway, taxiways, and an airplane.
- **Continuous Animation**: Continuously rotating 3D Radar Antenna.
- **Lighting & Material Shading**: Dual light sources (ambient environmental light and focused spotlight/point light) with diffuse and specular reflections.
- **Textures**: Textures applied on the runway, metallic control consoles, and walls.
- **Interactive Controls**: Keyboard and mouse navigation for camera movement and airplane translation.

## 🛠️ Prerequisites & Dependencies
- C++ Compiler (GCC / MinGW / Code::Blocks / Visual Studio)
- OpenGL Library
- FreeGLUT / GLUT Library
- GLU Library

### Linker Flags (for Code::Blocks / MinGW)
```bash
-lfreeglut -lopengl32 -lglu32
