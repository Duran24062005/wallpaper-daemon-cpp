# PRD 02 — Detectar pantallas sin cambiar ningún fondo

## Necesidad y contrato

El [PRD 01](01-workspace-y-arquitectura-core.md) ya distingue núcleo y backend, pero todavía no sabe qué pantallas existen. Una asignación por monitor necesita **identificador, geometría y estado activo** antes de modificar el escritorio. Este PRD crea un proveedor X11 con `xrandr --query`, un parser comprobable y un comando diagnóstico `screen-inspect`. **No cambia fondos**. Para Wayland no se utiliza `xrandr` como sustituto universal; cada compositor necesita su proveedor.

**Entrada:** salida de `xrandr --query`. **Salida:** pantallas conectadas con modo activo, o error explicativo. **Actores:** desarrollador y usuario que diagnostica monitores. **Fuera de alcance:** aplicar imágenes, hotplug automático y soporte Wayland genérico. El identificador X11 (`HDMI-1`, etc.) es estable durante una sesión, pero puede cambiar tras reconfigurar el hardware.

## Paso 1 — Modelar una pantalla

Crea `include/screen.hpp`:

```cpp
#pragma once
#include <string>
#include <vector>

struct ScreenInfo {
    std::string id;
    int width;
    int height;
    int x;
    int y;
    bool primary;
};

class ScreenProvider {
public:
    virtual ~ScreenProvider() = default;
    virtual std::vector<ScreenInfo> list_screens() = 0;
};

std::vector<ScreenInfo> parse_xrandr(const std::string& output);

class XrandrScreenProvider final : public ScreenProvider {
public:
    std::vector<ScreenInfo> list_screens() override;
};
```

Un proveedor es sustituible en tests. `x` o `y` pueden ser negativos cuando una pantalla está a la izquierda o encima de la principal. Una línea `connected` sin modo activo no entra en la lista de superficies aplicables.

## Paso 2 — Separar parser y proceso

Crea `src/screen.cpp` completo. `popen` ejecuta aquí **solo el texto constante** `xrandr --query`; nunca concatena IDs, rutas ni entrada del usuario. Los comandos con parámetros dinámicos de PRD 03 usan argumentos separados con `posix_spawnp`.

```cpp
#include "screen.hpp"
#include "error.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>

std::vector<ScreenInfo> parse_xrandr(const std::string& output) {
    const std::regex active(
        R"(^(\S+)\s+connected(?:\s+primary)?\s+(\d+)x(\d+)([+-]\d+)([+-]\d+))");
    std::vector<ScreenInfo> screens;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        std::smatch match;
        if (!std::regex_search(line, match, active)) continue;
        screens.push_back({match[1].str(), std::stoi(match[2].str()),
                           std::stoi(match[3].str()), std::stoi(match[4].str()),
                           std::stoi(match[5].str()),
                           line.find(" connected primary ") != std::string::npos});
    }
    return screens;
}

std::vector<ScreenInfo> XrandrScreenProvider::list_screens() {
    const char* session = std::getenv("XDG_SESSION_TYPE");
    if (session && std::string(session) == "wayland") {
        throw WallpaperError(ErrorCode::unsupported_capability,
                             "xrandr provider requires an X11 session");
    }
    if (!std::getenv("DISPLAY")) {
        throw WallpaperError(ErrorCode::backend_unavailable, "DISPLAY is not set");
    }
    FILE* pipe = popen("xrandr --query", "r");
    if (!pipe) {
        throw WallpaperError(ErrorCode::backend_unavailable,
                             "cannot start xrandr");
    }
    std::string output;
    std::array<char, 4096> buffer{};
    bool too_large = false;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        if (output.size() + std::char_traits<char>::length(buffer.data()) > 1024 * 1024) {
            too_large = true;
            continue; // Sigue drenando para poder recoger el proceso.
        }
        output += buffer.data();
    }
    const bool read_error = std::ferror(pipe) != 0;
    const int status = pclose(pipe);
    if (too_large || read_error || status == -1 || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        throw WallpaperError(ErrorCode::operation_failed,
                             "xrandr --query failed or returned oversized output");
    }
    return parse_xrandr(output);
}
```

La regex lee únicamente líneas de salidas `connected` con `WIDTHxHEIGHT+X+Y`; ignora modos secundarios, desconectados y conectados sin geometría. `pclose` recoge el proceso y comprueba su salida. Si te preocupa el shell que usa `popen`, la sustitución futura es un ejecutor con `posix_spawnp` y captura de stdout; no pases jamás texto variable a este comando fijo.

## Paso 3 — Un comando de inspección pequeño

Crea `src/screen_inspect.cpp`:

```cpp
#include "screen.hpp"
#include <exception>
#include <iostream>

int main() {
    try {
        XrandrScreenProvider provider;
        const auto screens = provider.list_screens();
        for (const auto& screen : screens) {
            std::cout << screen.id << ' ' << screen.width << 'x' << screen.height
                      << (screen.x < 0 ? "" : "+") << screen.x
                      << (screen.y < 0 ? "" : "+") << screen.y
                      << (screen.primary ? " primary" : "") << '\n';
        }
        return screens.empty() ? 2 : 0;
    } catch (const std::exception& error) {
        std::cerr << "screen-inspect: " << error.what() << '\n';
        return 1;
    }
}
```

Un conjunto vacío devuelve `2` para distinguir «la consulta funcionó pero no hay salida activa» de un fallo del proveedor (`1`). El [PRD 06](06-daemon-cli-e-ipc.md) incorporará esta información al CLI `wpd list-screens`.

## Paso 4 — Registrar targets en CMake

Debajo de `add_library(wallpaper-gnome ...)` y su `target_link_libraries`, agrega:

```cmake
add_library(wallpaper-screens STATIC src/screen.cpp)
target_include_directories(wallpaper-screens PUBLIC include)
target_link_libraries(wallpaper-screens PUBLIC wallpaper-core)

add_executable(screen-inspect src/screen_inspect.cpp)
target_link_libraries(screen-inspect PRIVATE wallpaper-screens)
```

Dentro de `if(BUILD_TESTING)`, **debajo** de `add_test(NAME core-tests ...)`, agrega:

```cmake
add_executable(screen-tests tests/screens_test.cpp)
target_link_libraries(screen-tests PRIVATE wallpaper-screens)
add_test(NAME screen-tests COMMAND screen-tests)
```

## Paso 5 — Probar el parser sin X11

Crea `tests/screens_test.cpp`:

```cpp
#include "screen.hpp"
#include <stdexcept>
#include <string>

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    const std::string fixture =
        "Screen 0: minimum 8 x 8, current 3840 x 1080\n"
        "DP-1 connected primary 1920x1080+0+0 (normal left inverted)\n"
        "HDMI-1 connected 1920x1080-1920+0 (normal left inverted)\n"
        "eDP-1 disconnected (normal left inverted)\n"
        "DP-2 connected (normal left inverted)\n";
    const auto screens = parse_xrandr(fixture);
    require(screens.size() == 2, "must find two active screens");
    require(screens[0].id == "DP-1" && screens[0].primary,
            "primary screen must be DP-1");
    require(screens[1].id == "HDMI-1" && screens[1].x == -1920,
            "negative X geometry must be preserved");
    require(parse_xrandr("eDP-1 disconnected\n").empty(),
            "disconnected output must be ignored");
}
```

Es una **prueba de parser**, no de hardware. Ejecuta:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/screen-inspect
```

En X11, el último comando imprime una línea por pantalla activa. En Wayland debe explicar que el proveedor X11 no sirve. En un terminal sin `DISPLAY`, debe decirlo y salir `1`. En un entorno con dos pantallas prueba que las dimensiones, posiciones e identificadores coinciden con `xrandr --query`. **Aceptación:** dos tests CTest aprobados, herramienta de inspección de solo lectura, casos 0/1/2 pantallas, fallo claro sin backend. El siguiente [PRD 03](03-fondos-por-pantalla.md) usa `ScreenInfo` para asignaciones.
