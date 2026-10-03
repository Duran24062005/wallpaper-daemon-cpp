# PRD 03 — Imágenes independientes por pantalla

## Problema, alcance y criterio de verdad

Tenemos `ScreenInfo` y un proveedor X11 de solo lectura del [PRD 02](02-deteccion-de-pantallas.md). Para asignar imágenes diferentes necesitamos seleccionar por identificador y usar un backend que **realmente** permita esa operación. Este PRD añade Xfce sobre X11 y Plasma mediante su API de scripting, conserva GNOME como backend global y rechaza explícitamente `set_image_for_screen` en GNOME. No modifica el núcleo para fingir que una URI global pertenece a un monitor.

**Actores:** usuario con varios monitores y desarrollador de adaptadores. **Datos:** `screen_id` tiene namespace: X11 usa el conector (`HDMI-1`); Plasma usa `plasma:0`, `plasma:1` según el índice de la sesión. Una reconexión puede cambiar índices, así que las asignaciones persistentes futuras deben resolverse de nuevo. **Resultado:** cada operación informa pantalla aplicada o fallo concreto; si una de varias falla, las anteriores pueden quedar aplicadas y el reporte no lo oculta.

## Paso 1 — Reutilizar URI y ejecución de procesos

En el PRD 00, `to_file_uri` está dentro del `namespace` anónimo de `src/wallpapers.cpp`. Crea `include/uri.hpp`:

```cpp
#pragma once
#include <string>
std::string to_file_uri(const std::string& path);
```

Crea `src/uri.cpp` con la misma lógica, ahora compartida:

```cpp
#include "uri.hpp"
#include <filesystem>

std::string to_file_uri(const std::string& path) {
    const std::string absolute = std::filesystem::canonical(path).string();
    const char hex[] = "0123456789ABCDEF";
    std::string uri = "file://";
    for (unsigned char c : absolute) {
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                           (c >= '0' && c <= '9') || c == '/' || c == '-' ||
                           c == '_' || c == '.' || c == '~';
        if (plain) uri += static_cast<char>(c);
        else { uri += '%'; uri += hex[c >> 4]; uri += hex[c & 0x0F]; }
    }
    return uri;
}
```

En `src/wallpapers.cpp` **borra solo** la función `to_file_uri` que comienza en `std::string to_file_uri(` y termina en su llave `}`; conserva `set_key`. Añade `#include "uri.hpp"` debajo de `#include "wallpapers.hpp"`. Para procesos con argumentos variables, crea `include/process.hpp` y `src/process.cpp` copiando los **dos bloques completos** de [Procesos externos](../docs/process-runner.md). Añade `src/uri.cpp` y `src/process.cpp` a `wallpaper-core` en CMake. El helper devuelve stdout y código de salida sin pasar por shell.

## Paso 2 — Asignar sin duplicar imágenes

El backend aplica; el núcleo decide qué imagen corresponde a cada pantalla. Crea `include/assignments.hpp`:

```cpp
#pragma once
#include "screen.hpp"
#include <map>
#include <random>
#include <string>
#include <vector>

std::map<std::string, std::string> choose_assignments(
    const std::vector<ScreenInfo>& screens,
    const std::vector<std::string>& images,
    std::mt19937& random_engine);
```

Crea `src/assignments.cpp`:

```cpp
#include "assignments.hpp"
#include <algorithm>
#include <stdexcept>

std::map<std::string, std::string> choose_assignments(
    const std::vector<ScreenInfo>& screens,
    const std::vector<std::string>& images,
    std::mt19937& random_engine) {
    if (images.empty()) throw std::runtime_error("No wallpapers available");
    std::vector<std::string> shuffled = images;
    std::shuffle(shuffled.begin(), shuffled.end(), random_engine);
    std::map<std::string, std::string> result;
    for (std::size_t i = 0; i < screens.size(); ++i) {
        const auto inserted = result.emplace(screens[i].id,
                                             shuffled[i % shuffled.size()]);
        if (!inserted.second) throw std::runtime_error("duplicate screen id");
    }
    return result;
}
```

Con suficientes imágenes, cada monitor recibe una distinta; con menos imágenes, el módulo reutiliza después de agotar las disponibles. `std::map` da un orden estable para logs y tests, pero la selección sigue dependiendo de la semilla. Una lista de pantallas vacía devuelve un mapa vacío: el llamador lo informa, no declara que se aplicó algo.

## Paso 3 — Backend de Xfce/X11

En `include/xfce.hpp` declara:

```cpp
#pragma once
#include "wallpapers.hpp"

class XfceBackend final : public WallpaperBackend {
public:
    std::string name() const override;
    BackendCapabilities capabilities() const override;
    void set_image_global(const std::string& path) override;
    void set_image_for_screen(const std::string& screen_id,
                              const std::string& path) override;
};
```

Crea `src/xfce.cpp`. Las propiedades de xfdesktop pertenecen al usuario y pueden variar entre versiones; el adaptador **descubre** las rutas existentes y cambia todas las áreas de trabajo de un monitor, sin inventar una propiedad nueva. Primero observa la salida real con `xfconf-query --channel xfce4-desktop --list --verbose`; la guía usa propiedades terminadas en `/last-image` o `/image-path`.

```cpp
#include "xfce.hpp"
#include "error.hpp"
#include "process.hpp"
#include "screen.hpp"
#include <filesystem>
#include <sstream>

std::string XfceBackend::name() const { return "xfce"; }
BackendCapabilities XfceBackend::capabilities() const {
    return {true, true, false, false};
}

void XfceBackend::set_image_for_screen(const std::string& screen_id,
                                       const std::string& path) {
    if (screen_id.empty() || screen_id.find('/') != std::string::npos) {
        throw WallpaperError(ErrorCode::operation_failed, "invalid X11 screen id");
    }
    const auto properties = run_process({"xfconf-query", "--channel", "xfce4-desktop",
                                         "--list"});
    if (properties.exit_code != 0) {
        throw WallpaperError(ErrorCode::backend_unavailable,
                             "cannot list xfce4-desktop properties");
    }
    const std::string marker = "/monitor" + screen_id + "/";
    std::istringstream lines(properties.output);
    std::string property;
    std::size_t changed = 0;
    const std::string absolute = std::filesystem::canonical(path).string();
    while (std::getline(lines, property)) {
        const bool image_key = property.size() >= 11 &&
            (property.compare(property.size() - 11, 11, "/last-image") == 0 ||
             property.compare(property.size() - 11, 11, "/image-path") == 0);
        if (property.find(marker) == std::string::npos || !image_key) continue;
        const auto result = run_process({"xfconf-query", "--channel", "xfce4-desktop",
                                         "--property", property, "--set", absolute});
        if (result.exit_code != 0) {
            throw WallpaperError(ErrorCode::operation_failed,
                                 "xfconf could not set " + property);
        }
        ++changed;
    }
    if (changed == 0) {
        throw WallpaperError(ErrorCode::unsupported_capability,
                             "no xfdesktop image property for " + screen_id);
    }
}

void XfceBackend::set_image_global(const std::string& path) {
    XrandrScreenProvider screens;
    const auto active = screens.list_screens();
    if (active.empty()) {
        throw WallpaperError(ErrorCode::backend_unavailable, "no active X11 screens");
    }
    for (const auto& screen : active) set_image_for_screen(screen.id, path);
}
```

**Punto de validación:** en una instalación Xfce real, anota el nombre exacto de monitor que aparece en `xfconf-query --list`; compáralo con `screen-inspect`. Si no coincide, el backend debe informar `unsupported_capability` y debes documentar el mapeo de esa versión antes de añadirlo; no adivines IDs. [xfconf](https://developer.xfce.org/xfconf/) es la interfaz oficial de configuración de Xfce. El bucle puede aplicar algunas propiedades y fallar después: muestra el nombre de la última propiedad que falló y conserva el registro de las anteriores.

## Paso 4 — Backend de Plasma

Plasma ofrece un escritorio por pantalla y métodos `desktopForScreen`, `screenCount` y `screenGeometry` en su [API de scripting](https://develop.kde.org/docs/plasma/scripting/api/). La [interfaz D-Bus de PlasmaShell](https://github.com/KDE/plasma-workspace/blob/master/shell/dbus/org.kde.PlasmaShell.xml) declara `evaluateScript` con un argumento `script` y una salida de texto. La herramienta de esta guía es `qdbus6` de la misma sesión Plasma; si falta, el adaptador informa disponibilidad, no prueba otra interfaz arbitraria.

Crea `include/plasma.hpp`:

```cpp
#pragma once
#include "screen.hpp"
#include "wallpapers.hpp"

class PlasmaScreenProvider final : public ScreenProvider {
public:
    std::vector<ScreenInfo> list_screens() override;
};

class PlasmaBackend final : public WallpaperBackend {
public:
    std::string name() const override;
    BackendCapabilities capabilities() const override;
    void set_image_global(const std::string& path) override;
    void set_image_for_screen(const std::string& screen_id,
                              const std::string& path) override;
};
```

Crea `src/plasma.cpp`:

```cpp
#include "plasma.hpp"
#include "error.hpp"
#include "process.hpp"
#include "uri.hpp"
#include <charconv>
#include <sstream>
#include <stdexcept>

namespace {
std::string eval(const std::string& script) {
    const auto result = run_process({"qdbus6", "org.kde.plasmashell", "/PlasmaShell",
                                     "org.kde.PlasmaShell.evaluateScript", script});
    if (result.exit_code != 0) {
        throw WallpaperError(ErrorCode::backend_unavailable,
                             "PlasmaShell scripting call failed");
    }
    return result.output;
}

int plasma_index(const std::string& id) {
    if (id.rfind("plasma:", 0) != 0) {
        throw WallpaperError(ErrorCode::operation_failed, "invalid Plasma screen id");
    }
    const std::string number = id.substr(7);
    int index = -1;
    const auto parsed = std::from_chars(number.data(), number.data() + number.size(), index);
    if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() || index < 0) {
        throw WallpaperError(ErrorCode::operation_failed, "invalid Plasma screen index");
    }
    return index;
}
}

std::vector<ScreenInfo> PlasmaScreenProvider::list_screens() {
    const std::string script =
        "var rows=[]; for(var i=0;i<screenCount;i++){"
        "var g=screenGeometry(i); rows.push([i,g.width,g.height,g.x,g.y].join(','));"
        "} print(rows.join(';'));";
    std::vector<ScreenInfo> screens;
    std::istringstream rows(eval(script));
    std::string row;
    while (std::getline(rows, row, ';')) {
        if (row.empty()) continue;
        std::istringstream values(row);
        std::string cell;
        int numbers[5];
        for (int& number : numbers) {
            if (!std::getline(values, cell, ',')) {
                throw WallpaperError(ErrorCode::operation_failed,
                                     "malformed Plasma screen row");
            }
            number = std::stoi(cell);
        }
        screens.push_back({"plasma:" + std::to_string(numbers[0]), numbers[1],
                           numbers[2], numbers[3], numbers[4], false});
    }
    return screens;
}

std::string PlasmaBackend::name() const { return "plasma"; }
BackendCapabilities PlasmaBackend::capabilities() const {
    return {true, true, false, false};
}

void PlasmaBackend::set_image_for_screen(const std::string& screen_id,
                                         const std::string& path) {
    const int index = plasma_index(screen_id);
    const auto screens = PlasmaScreenProvider{}.list_screens();
    if (static_cast<std::size_t>(index) >= screens.size()) {
        throw WallpaperError(ErrorCode::operation_failed, "Plasma screen is disconnected");
    }
    const std::string uri = to_file_uri(path); // Solo ASCII seguro para literal JS.
    const std::string script =
        "var d=desktopForScreen(" + std::to_string(index) + ");"
        "if(!d){print('NO_SCREEN')}else{"
        "d.wallpaperPlugin='org.kde.image';"
        "d.currentConfigGroup=['Wallpaper','org.kde.image','General'];"
        "d.writeConfig('Image','" + uri + "');d.reloadConfig();print('OK')}";
    if (eval(script).find("OK") == std::string::npos) {
        throw WallpaperError(ErrorCode::operation_failed,
                             "Plasma did not confirm wallpaper update");
    }
}

void PlasmaBackend::set_image_global(const std::string& path) {
    const auto screens = PlasmaScreenProvider{}.list_screens();
    if (screens.empty()) {
        throw WallpaperError(ErrorCode::backend_unavailable, "no Plasma screens");
    }
    for (const auto& screen : screens) set_image_for_screen(screen.id, path);
}
```

`to_file_uri` convierte comillas y caracteres especiales en escapes `%HH`, de modo que `uri` no puede cerrar el literal JS. El script comprueba si existe el contenedor antes de escribir. El índice de Plasma **no es** el nombre físico de salida X11; usa `PlasmaScreenProvider` y evita mezclar namespaces. El `print` de resultado es una comprobación de llamada; confirma visualmente la imagen y documenta la versión exacta probada. Si `qdbus6` devuelve un formato distinto en tu versión, conserva una muestra en un fixture y ajusta solo el parser de ese adaptador.

## Paso 5 — Registrar y probar por capas

Primero crea `src/set_image_inspect.cpp` para una prueba manual que reciba backend, ID y ruta. El contenido completo es:

```cpp
#include "plasma.hpp"
#include "xfce.hpp"
#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: set-image-inspect xfce|plasma SCREEN_ID IMAGE\n";
        return 2;
    }
    try {
        if (std::string(argv[1]) == "xfce") {
            XfceBackend{}.set_image_for_screen(argv[2], argv[3]);
        } else if (std::string(argv[1]) == "plasma") {
            PlasmaBackend{}.set_image_for_screen(argv[2], argv[3]);
        } else {
            std::cerr << "unknown backend\n";
            return 2;
        }
        std::cout << "applied to " << argv[2] << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

En CMake añade `add_executable(set-image-inspect src/set_image_inspect.cpp)` y `target_link_libraries(set-image-inspect PRIVATE wallpaper-xfce wallpaper-plasma)`. Compila y corre CTest; después inspecciona pantallas y aplica una imagen en el backend real. Ejemplos (ajusta ID a la **salida observada**):

Ahora sustituye **todo** `CMakeLists.txt`. `screen.cpp` pasa a `wallpaper-core`, así que **borra** las líneas que creaban `wallpaper-screens`; no muevas archivos físicos. `screen-inspect` y `screen-tests` siguen existiendo, pero enlazan core. No hay enlace circular.

```cmake
cmake_minimum_required(VERSION 3.16)
project(wallpaper-daemon-cpp VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(wallpaper-core STATIC
    src/config.cpp
    src/scanner.cpp
    src/selector.cpp
    src/error.cpp
    src/cycle.cpp
    src/screen.cpp
    src/uri.cpp
    src/process.cpp
    src/assignments.cpp
)
target_include_directories(wallpaper-core PUBLIC include)

add_library(wallpaper-gnome STATIC src/wallpapers.cpp)
target_include_directories(wallpaper-gnome PUBLIC include)
target_link_libraries(wallpaper-gnome PUBLIC wallpaper-core)

add_library(wallpaper-xfce STATIC src/xfce.cpp)
target_include_directories(wallpaper-xfce PUBLIC include)
target_link_libraries(wallpaper-xfce PUBLIC wallpaper-core)

add_library(wallpaper-plasma STATIC src/plasma.cpp)
target_include_directories(wallpaper-plasma PUBLIC include)
target_link_libraries(wallpaper-plasma PUBLIC wallpaper-core)

add_executable(wallpaper-daemon src/main.cpp src/scheduler.cpp)
target_link_libraries(wallpaper-daemon PRIVATE wallpaper-core wallpaper-gnome)
add_executable(screen-inspect src/screen_inspect.cpp)
target_link_libraries(screen-inspect PRIVATE wallpaper-core)
add_executable(set-image-inspect src/set_image_inspect.cpp)
target_link_libraries(set-image-inspect PRIVATE wallpaper-xfce wallpaper-plasma)

include(CTest)
if(BUILD_TESTING)
    add_executable(core-tests tests/core_tests.cpp)
    target_link_libraries(core-tests PRIVATE wallpaper-core)
    add_test(NAME core-tests COMMAND core-tests)
    add_executable(screen-tests tests/screens_test.cpp)
    target_link_libraries(screen-tests PRIVATE wallpaper-core)
    add_test(NAME screen-tests COMMAND screen-tests)
endif()
```

En `tests/core_tests.cpp`, añade `#include "assignments.hpp"` entre los includes del proyecto. **Debajo de** `assert(first != second);`, agrega:

```cpp
        std::vector<ScreenInfo> two_screens = {
            {"HDMI-1", 1920, 1080, 0, 0, true},
            {"DP-1", 1920, 1080, 1920, 0, false}
        };
        const auto assignments = choose_assignments(two_screens, images, rng);
        require(assignments.size() == 2, "two screens need two assignments");
        require(assignments.at("HDMI-1") != assignments.at("DP-1"),
                "two images should not repeat across two screens");
```

Compila y corre CTest; después inspecciona pantallas y aplica una imagen en el backend real. Ejemplos (ajusta ID a la **salida observada**; Plasma usa IDs de `PlasmaScreenProvider`, no los de `screen-inspect` X11):

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/screen-inspect
./build/set-image-inspect xfce HDMI-1 assets/1.jpeg
./build/set-image-inspect plasma plasma:0 assets/1.jpeg
```

**Aceptación:** con dos imágenes hay asignaciones distintas; Xfce y Plasma solo se marcan soportados tras prueba visual en su sesión; GNOME devuelve `unsupported_capability` para pantalla individual; una pantalla desconectada y un servicio ausente dan errores distintos. **Riesgos:** índices de Plasma cambian, propiedades de Xfce dependen de versión y varias escrituras no son transaccionales. No persistas un `plasma:0` como identidad física. Continúa con [PRD 04](04-video-global.md).
