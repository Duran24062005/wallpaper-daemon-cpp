# CMake desde el estado real del proyecto

## 1. Comprueba herramientas y ubicación

Desde la raíz de `wallpaper-daemon-cpp` ejecuta `cmake --version`, `c++ --version` y `pwd`. El proyecto exige C++17. `CMakeLists.txt` ya existe; no hay que crearlo de nuevo. Los headers públicos están en `include/`, no en `src/`.

## 2. Lee el archivo actual

```cmake
cmake_minimum_required(VERSION 3.16)
project(wallpaper-daemon-cpp VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(wallpaper-daemon
    src/main.cpp
    src/config.cpp
    src/scanner.cpp
    src/selector.cpp
)
```

Es la misma selección de fuentes que contiene hoy el repositorio (el espaciado del `project` puede variar). `cmake_minimum_required` fija la versión mínima del generador; `project` declara el lenguaje; `CMAKE_CXX_STANDARD` pide C++17; `add_executable` enumera **cada** `.cpp` que debe enlazarse. Los `#include "../include/..."` del código actual funcionan, aunque el PRD 01 los sustituirá por `#include "..."` y `target_include_directories`.

## 3. Configura, compila y ejecuta

```bash
cmake -S . -B build
cmake --build build
./build/wallpaper-daemon
```

`-S .` señala las fuentes y `-B build` deja los artefactos generados fuera de `src/`. `build/` está ignorado por Git. La compilación debe terminar en `Built target wallpaper-daemon`; la ejecución actual muestra siete candidatos si se conservan los siete JPEG de `assets/`. En la primera configuración CMake puede mostrar mensajes de detección del compilador; eso es normal.

## 4. Añade un módulo, sin olvidar el enlazado

En el [PRD 00](../prds/00-mvp-y-robustez.md), cuando crees `src/wallpapers.cpp` y `src/scheduler.cpp`, coloca sus líneas **dentro de** `add_executable`, justo debajo de `src/selector.cpp`:

```cmake
    src/wallpapers.cpp
    src/scheduler.cpp
```

Vuelve a ejecutar `cmake -S . -B build` y `cmake --build build`. Si declaras una función en un `.hpp` y olvidas su `.cpp`, el compilador puede aceptar las llamadas, pero el enlazador indicará `undefined reference`: revisa `add_executable` antes de cambiar la firma.

## 5. Tests después de separar el núcleo

El [PRD 01](../prds/01-workspace-y-arquitectura-core.md) convierte los módulos comprobables en una biblioteca CMake y agrega `enable_testing()` y `add_test`. En ese momento `ctest --test-dir build --output-on-failure` será el comando de pruebas. **Hoy no hay tests registrados**: ejecutar `ctest` ahora no demostraría que el proyecto está probado.

No hace falta compilar a mano con `g++ src/main.cpp`: ese comando deja fuera `config.cpp`, `scanner.cpp` y `selector.cpp`. Cuando cambies `CMakeLists.txt`, configura de nuevo antes de compilar.
