# PRD 01 — Núcleo comprobable y adaptador GNOME

## Problema y resultado

Al terminar [PRD 00](00-mvp-y-robustez.md), `main` funciona, pero conoce demasiado sobre escaneo, elección y GNOME. Para añadir escritorios necesitamos una frontera sustituible y tests que no cambien el fondo real. **Resultado de este PRD:** una biblioteca CMake de lógica (`wallpaper-core`), un adaptador GNOME, un binario que conserva el mismo comportamiento y tests ejecutables con CTest. No crees otro repositorio ni un árbol de «workspace» artificial: este C++ sigue siendo una unidad Git independiente.

**Actores:** desarrollador C++ y futuros adaptadores. **Fuera de alcance:** detectar pantallas, persistir configuración, daemon y vídeo. **Contrato:** el núcleo recibe un `WallpaperBackend&`; el backend es dueño de sus recursos; el ciclo solo actualiza `last_applied` tras una aplicación exitosa. El error `unsupported_capability` significa que la operación no existe en ese backend, no que el archivo está mal.

## Paso 1 — Dar nombre a los fallos de capacidad

Crea `include/error.hpp`:

```cpp
#pragma once
#include <stdexcept>
#include <string>

enum class ErrorCode {
    unsupported_capability,
    backend_unavailable,
    operation_failed
};

class WallpaperError : public std::runtime_error {
public:
    WallpaperError(ErrorCode code, const std::string& message);
    ErrorCode code() const noexcept;
private:
    ErrorCode code_;
};
```

Ahora llena `src/error.cpp`, que estaba vacío:

```cpp
#include "error.hpp"

WallpaperError::WallpaperError(ErrorCode code, const std::string& message)
    : std::runtime_error(message), code_(code) {}

ErrorCode WallpaperError::code() const noexcept { return code_; }
```

`std::runtime_error` conserva el texto para un usuario; `ErrorCode` conserva el significado para CLI e IPC posteriores. No conviertas todos los errores del sistema de archivos a una cadena genérica: sus rutas y códigos ayudan a corregirlos.

## Paso 2 — Declarar lo que puede hacer un backend

Reemplaza `include/wallpapers.hpp` del PRD 00 por:

```cpp
#pragma once
#include <string>

struct BackendCapabilities {
    bool global_image = false;
    bool per_screen_image = false;
    bool global_video = false;
    bool per_screen_video = false;
};

class WallpaperBackend {
public:
    virtual ~WallpaperBackend() = default;
    virtual std::string name() const = 0;
    virtual BackendCapabilities capabilities() const = 0;
    virtual void set_image_global(const std::string& path) = 0;
    virtual void set_image_for_screen(const std::string& screen_id,
                                      const std::string& path);
};

class GnomeBackend final : public WallpaperBackend {
public:
    std::string name() const override;
    BackendCapabilities capabilities() const override;
    void set_image_global(const std::string& path) override;
};
```

En `src/wallpapers.cpp`, cambia el primer include a `#include "wallpapers.hpp"`. **Conserva el bloque `namespace { ... }` con `to_file_uri` y `set_key` del PRD 00.** Sustituye solo la función final `void set_wallpaper(...)` por este bloque:

```cpp
std::string GnomeBackend::name() const { return "gnome"; }

BackendCapabilities GnomeBackend::capabilities() const {
    return {true, false, false, false};
}

void GnomeBackend::set_image_global(const std::string& path) {
    const std::string uri = to_file_uri(path);
    set_key("picture-uri", uri);
    set_key("picture-uri-dark", uri);
}
```

La tabla de capacidades hace explícito que este GNOME no aplica por monitor. El método por pantalla lanza antes de tocar GSettings. Después de integrar otros backends, cada uno sobreescribirá solo las operaciones que soporte. **No confundas** capacidad declarada con comprobación de disponibilidad: una sesión GNOME ausente todavía puede fallar al ejecutar `gsettings`; ese caso será `backend_unavailable` al añadir un chequeo de entorno.

## Paso 3 — Extraer un ciclo sin UI ni reloj

Crea `include/cycle.hpp`:

```cpp
#pragma once
#include "config.hpp"
#include "wallpapers.hpp"
#include <optional>
#include <random>
#include <string>

std::string run_cycle(const Config& config, WallpaperBackend& backend,
                      std::optional<std::string>& last_applied,
                      std::mt19937& random_engine);
```

Crea `src/cycle.cpp`. El método por pantalla predeterminado vive en el núcleo para que los tests enlacen sin el adaptador GNOME:

```cpp
#include "cycle.hpp"
#include "error.hpp"
#include "scanner.hpp"
#include "selector.hpp"

void WallpaperBackend::set_image_for_screen(const std::string& screen_id,
                                             const std::string&) {
    throw WallpaperError(ErrorCode::unsupported_capability,
                         name() + " cannot set an image for screen " + screen_id);
}

std::string run_cycle(const Config& config, WallpaperBackend& backend,
                      std::optional<std::string>& last_applied,
                      std::mt19937& random_engine) {
    const auto images = scan_wallpapers(config.wallpaper_directory);
    const auto selected = select_wallpaper(images, last_applied, random_engine);
    backend.set_image_global(selected);
    last_applied = selected;
    return selected;
}
```

Si `scan`, `select` o el backend lanzan, la asignación final no ocurre. Esa línea es la propiedad más importante del estado del ciclo. `run_cycle` no duerme: una prueba puede llamarlo una vez y observar el resultado inmediatamente.

## Paso 4 — Actualizar `main` y los includes

En `src/main.cpp` del PRD 00, reemplaza las cuatro líneas `#include "../include/..."` de `config`, `scanner`, `selector` y `wallpapers` por:

```cpp
#include "config.hpp"
#include "cycle.hpp"
#include "scheduler.hpp"
#include "wallpapers.hpp"
```

Elimina los includes de `scanner.hpp` y `selector.hpp` en `main`; los usa `cycle.cpp`. En `main`, **después** de `std::optional<std::string> last_applied;`, crea el adaptador:

```cpp
GnomeBackend backend;
```

Dentro del `try` del bucle, sustituye desde `const auto images = ...` hasta `last_applied = selected;` por:

```cpp
const auto selected = run_cycle(config, backend, last_applied, random_engine);
```

Deja la línea `std::cout << "Applied wallpaper: " << selected << std::endl;` justo después. En `src/config.cpp`, `src/scanner.cpp`, `src/selector.cpp` y `src/scheduler.cpp`, cambia solo el primer include relativo `"../include/archivo.hpp"` a `"archivo.hpp"`. CMake dará a cada target la carpeta `include/`; no necesitas rutas `../` en el código.

## Paso 5 — Cambiar CMake en una sola operación

Sustituye el **contenido entero** de `CMakeLists.txt`. La biblioteca core no enlaza `gsettings` ni requiere sesión gráfica. El adaptador GNOME contiene `wallpapers.cpp`; el ejecutable compone ambos y el scheduler.

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
)
target_include_directories(wallpaper-core PUBLIC include)

add_library(wallpaper-gnome STATIC src/wallpapers.cpp)
target_include_directories(wallpaper-gnome PUBLIC include)
target_link_libraries(wallpaper-gnome PUBLIC wallpaper-core)

add_executable(wallpaper-daemon src/main.cpp src/scheduler.cpp)
target_include_directories(wallpaper-daemon PRIVATE include)
target_link_libraries(wallpaper-daemon PRIVATE wallpaper-core wallpaper-gnome)

include(CTest)
if(BUILD_TESTING)
    add_executable(core-tests tests/core_tests.cpp)
    target_link_libraries(core-tests PRIVATE wallpaper-core)
    add_test(NAME core-tests COMMAND core-tests)
endif()
```

El `PUBLIC include` permite que quien enlaza `wallpaper-core` encuentre sus headers. `include(CTest)` crea la opción `BUILD_TESTING` y activa CTest. Si configuras antes de crear `tests/core_tests.cpp`, CMake avisará que falta el archivo; crea el test en el siguiente paso y **entonces** reconfigura.

## Paso 6 — Crear una prueba real, sin tocar el escritorio

Crea la carpeta `tests/` y el archivo `tests/core_tests.cpp` completo:

```cpp
#include "config.hpp"
#include "cycle.hpp"
#include "scanner.hpp"
#include "selector.hpp"
#include "wallpapers.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

class FakeBackend final : public WallpaperBackend {
public:
    bool fail = false;
    std::vector<std::string> applied;
    std::string name() const override { return "fake"; }
    BackendCapabilities capabilities() const override { return {true, false, false, false}; }
    void set_image_global(const std::string& path) override {
        if (fail) throw std::runtime_error("simulated backend failure");
        applied.push_back(path);
    }
};

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path folder = fs::temp_directory_path() /
                            ("wallpaper-core-test-" + std::to_string(stamp));
    fs::create_directory(folder);
    try {
        std::ofstream(folder / "b.JPEG").put('b');
        std::ofstream(folder / "a.png").put('a');
        std::ofstream(folder / "ignore.txt").put('x');
        const auto images = scan_wallpapers(folder.string());
        require(images.size() == 2, "scanner must find two images");
        require(images[0] < images[1], "scanner must sort paths");

        std::mt19937 rng(123);
        const auto first = select_wallpaper(images, std::nullopt, rng);
        const auto second = select_wallpaper(images, first, rng);
        require(first != second, "selector must avoid repetition");

        Config config{folder.string(), 1};
        validate_config(config);
        FakeBackend backend;
        std::optional<std::string> previous;
        const auto selected = run_cycle(config, backend, previous, rng);
        require(previous == selected, "cycle must update previous");
        require(backend.applied.size() == 1, "backend must receive one image");
        backend.fail = true;
        bool backend_failed = false;
        try {
            run_cycle(config, backend, previous, rng);
        } catch (const std::runtime_error&) {
            backend_failed = true;
        }
        require(backend_failed, "backend failure must propagate");
        require(previous == selected, "failed cycle must preserve previous");

        bool missing_failed = false;
        try {
            scan_wallpapers((folder / "missing").string());
        } catch (const fs::filesystem_error&) { missing_failed = true; }
        require(missing_failed, "missing folder must fail");
    } catch (...) {
        fs::remove_all(folder);
        throw;
    }
    fs::remove_all(folder);
}
```

La carpeta temporal aísla los archivos de prueba de `assets/`. `FakeBackend` registra la operación y permite verificar que un fallo no modifica `previous`. `require` lanza también en Release; así CTest sigue comprobando reglas en el paquete final.

## Compilar, observar y aceptar

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

La salida esperada de CTest contiene `100% tests passed, 0 tests failed out of 1`. Después ejecuta `./build/wallpaper-daemon` en GNOME y comprueba el mismo comportamiento del PRD 00. Si el linker no encuentra `WallpaperBackend::set_image_for_screen`, verifica que `wallpaper-gnome` enlaza `wallpaper-core` y que `src/wallpapers.cpp` contiene la definición. Si no encuentra headers, comprueba `target_include_directories` y las rutas de `#include`.

**Aceptación:** build limpio; un test registrado y aprobado; selección y fallo probados sin GNOME; aplicación manual de GNOME con sesión real; código de escritorio fuera de `wallpaper-core`. **Riesgo:** la disponibilidad del backend se comprobará explícitamente al introducir detección y CLI; el nombre de escritorio por sí solo no certifica una sesión funcional. Continúa con [PRD 02](02-deteccion-de-pantallas.md).
