# PRD 05 — Una reproducción por pantalla X11

## Problema y resultado

El [PRD 04](04-video-global.md) posee un proceso de fondo. Para vídeos diferentes en dos monitores necesitamos **un propietario y una geometría por salida**, más una operación de reemplazo que limpie instancias viejas. Esta etapa usa el mismo backend X11 validado visualmente; no afirma vídeo por monitor en GNOME/Plasma Wayland. `xwinwrap -g` recibe `ancho x alto + x + y` y el PRD 04 ya construye esa cadena a partir de `ScreenInfo`.

**Actores:** usuario de X11 con varios monitores. **Entrada:** pares pantalla–archivo existentes. **Salida:** una instancia supervisada por pantalla, con estado individual. **Reglas:** rechazar IDs duplicados, pantallas sin geometría positiva y más de cuatro reproducciones; si falla el inicio de una nueva configuración, conservar la configuración anterior. **Fuera de alcance:** pausas por bloqueo/suspensión (requieren una integración de sesión que se añadirá al servicio), vídeo en compositor Wayland sin adaptador específico.

## Paso 1 — Definir un gestor que sea dueño de todo

Crea `include/multi_video.hpp`:

```cpp
#pragma once
#include "video.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

struct ScreenVideo {
    ScreenInfo screen;
    std::string path;
};

struct VideoStatus {
    std::string screen_id;
    bool healthy;
};

class MultiVideoManager {
public:
    void apply(const std::vector<ScreenVideo>& requested);
    std::vector<VideoStatus> status();
    void stop_all() noexcept;
private:
    struct Entry {
        std::string path;
        std::unique_ptr<X11VideoBackend> process;
    };
    std::map<std::string, Entry> active_;
};
```

`unique_ptr` impide copiar un proceso. `active_` es el único dueño de las instancias y su destructor hace que cada `X11VideoBackend` detenga su grupo. `status()` no promete que el vídeo sea visible; solo observa el proceso principal.

## Paso 2 — Implementar un reemplazo controlado

Crea `src/multi_video.cpp`:

```cpp
#include "multi_video.hpp"
#include "error.hpp"
#include <set>
#include <stdexcept>

void MultiVideoManager::apply(const std::vector<ScreenVideo>& requested) {
    if (requested.empty()) {
        throw WallpaperError(ErrorCode::operation_failed, "no screen videos requested");
    }
    if (requested.size() > 4) {
        throw WallpaperError(ErrorCode::unsupported_capability,
                             "maximum four video screens in this backend");
    }
    std::set<std::string> ids;
    for (const auto& item : requested) {
        if (item.screen.id.empty() || item.screen.width <= 0 || item.screen.height <= 0) {
            throw WallpaperError(ErrorCode::operation_failed, "invalid screen geometry");
        }
        if (!ids.insert(item.screen.id).second) {
            throw WallpaperError(ErrorCode::operation_failed,
                                 "duplicate video screen: " + item.screen.id);
        }
    }

    std::map<std::string, Entry> next;
    for (const auto& item : requested) {
        auto process = std::make_unique<X11VideoBackend>();
        process->start(item.path, item.screen);
        next.emplace(item.screen.id, Entry{item.path, std::move(process)});
    }
    // Si alguno lanzó, next se destruye y active_ conserva la configuración previa.
    active_.swap(next);
    // Ahora next posee los procesos anteriores y los detiene al salir del método.
}

std::vector<VideoStatus> MultiVideoManager::status() {
    std::vector<VideoStatus> result;
    for (auto& [id, entry] : active_) {
        result.push_back({id, entry.process->healthy()});
    }
    return result;
}

void MultiVideoManager::stop_all() noexcept {
    active_.clear();
}
```

Se valida **todo** antes de iniciar nada. El mapa temporal posee nuevas instancias; si un `start` lanza, sus destructores detienen las que ya arrancaron y el mapa anterior permanece. El intercambio se hace solo después de iniciar todas. Hay un instante en que los vídeos nuevos y viejos pueden coexistir; cuando termine el método, los viejos se detienen. `apply` se usa al cambiar configuración o monitores, no en cada fotograma ni en cada ciclo de 60 segundos.

## Paso 3 — Una herramienta manual para dos pantallas

Crea `src/multi_video_inspect.cpp` completo:

```cpp
#include "multi_video.hpp"
#include "screen.hpp"
#include <exception>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3 || (argc - 1) % 2 != 0) {
        std::cerr << "usage: multi-video-inspect SCREEN_ID VIDEO [SCREEN_ID VIDEO...]\n";
        return 2;
    }
    try {
        XrandrScreenProvider provider;
        const auto screens = provider.list_screens();
        std::vector<ScreenVideo> requested;
        for (int i = 1; i < argc; i += 2) {
            const std::string id = argv[i];
            bool found = false;
            for (const auto& screen : screens) {
                if (screen.id == id) {
                    requested.push_back({screen, argv[i + 1]});
                    found = true;
                    break;
                }
            }
            if (!found) throw std::runtime_error("unknown active screen: " + id);
        }
        MultiVideoManager manager;
        manager.apply(requested);
        for (const auto& item : manager.status()) {
            std::cout << item.screen_id << ": "
                      << (item.healthy ? "running" : "exited") << '\n';
        }
        std::cout << "press Enter to stop all videos\n";
        std::cin.get();
        manager.stop_all();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "multi-video: " << error.what() << '\n';
        return 1;
    }
}
```

Debajo de `add_executable(video-inspect ...)` en `CMakeLists.txt` añade:

```cmake
target_sources(wallpaper-video PRIVATE src/multi_video.cpp)
add_executable(multi-video-inspect src/multi_video_inspect.cpp)
target_link_libraries(multi-video-inspect PRIVATE wallpaper-video)
```

## Paso 4 — Observar y tratar hotplug

Compila y consulta las salidas; **copia IDs reales** del diagnóstico. Ejemplo:

```bash
cmake -S . -B build
cmake --build build
./build/screen-inspect
./build/multi-video-inspect HDMI-1 /ruta/uno.mp4 DP-1 /ruta/dos.webm
```

Comprueba que ambos vídeos quedan detrás de ventanas, cada uno dentro de su geometría, y que al pulsar Enter no quedan hijos de esta ejecución. Desconecta una pantalla durante la prueba: el gestor **aún no recibe eventos**; el proceso de servicio del PRD 06 deberá volver a consultar `ScreenProvider`, comparar la lista con su último estado y llamar a `apply` con las salidas activas. Hasta entonces detén manualmente y vuelve a iniciar. El identificador `HDMI-1` se resuelve en cada arranque; no guardes coordenadas permanentes como identidad del monitor.

## Pruebas y aceptación

- ID duplicado, pantalla no activa, geometría inválida y quinta pantalla fallan **antes** de iniciar un proceso.
- Un archivo de vídeo inválido en una nueva configuración deja la anterior activa; los procesos nuevos que alcanzaron a arrancar se limpian.
- `status()` distingue una instancia que salió de las demás; `stop_all()` termina todas.
- La prueba gráfica de dos pantallas y de reconexión se registra con versión de escritorio, X11, geometría, procesos y observación visual. Si el escritorio coloca una ventana encima de iconos o roba foco, esa combinación sigue sin soporte.

La supervisión, pausa por bloqueo y recuperación con backoff pertenecen al ciclo de vida del [PRD 06](06-daemon-cli-e-ipc.md). No presentes `healthy()` como una prueba de decodificación ni de visibilidad.
