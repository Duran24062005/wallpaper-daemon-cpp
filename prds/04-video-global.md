# PRD 04 — Vídeo global en un backend X11 verificable

## Meta, límites y dependencias

Una imagen estática no cubre fondos animados. Para aprender a poseer y detener procesos en C++, este PRD usa **`xwinwrap` + `mpv` en una sesión X11**. [xwinwrap documenta](https://github.com/mmhobi7/xwinwrap) cómo crear una ventana de fondo y pasar su `WID` a `mpv`; [mpv documenta](https://mpv.io/manual/stable/) `--wid` y su socket IPC. Son programas externos, no código ya incluido en este repositorio. El backend se considera soportado **solo en una combinación de escritorio y sesión donde la prueba visual confirme** que el vídeo queda detrás de ventanas y no toma foco. En GNOME Wayland, Plasma Wayland u otro compositor sin este mecanismo se informa `unsupported_capability`; un proceso `mpv` abierto como ventana normal no cuenta como fondo.

**Actores:** usuario de X11 y desarrollador que aprende RAII/procesos. **Entrada:** vídeo local existente (`.mp4`, `.mkv` o `.webm`) y sesión X11. **Salida:** proceso de vídeo supervisado o error. **Fuera de alcance:** múltiples vídeos, mezcla de audio, descarga remota y soporte Wayland. **Riesgos:** consumo de CPU/GPU, códecs, backend X11 del escritorio y procesos huérfanos.

## Paso 1 — Comprobar el entorno sin tocar código

En una terminal de la **sesión gráfica**, ejecuta:

```bash
printf '%s\n' "$XDG_SESSION_TYPE" "$DISPLAY" "$XDG_RUNTIME_DIR"
command -v xwinwrap
command -v mpv
mpv --version
```

La primera línea debe ser `x11`; `DISPLAY` y `XDG_RUNTIME_DIR` deben estar presentes. Usa `mpv --no-config --no-audio --loop-file=inf archivo.mp4` para comprobar el archivo como vídeo normal antes de depurar la ventana de fondo; ciérralo antes del siguiente paso. Si falta `xwinwrap`, usa las instrucciones de instalación de su repositorio y registra versión/commit. No arranques un servicio de usuario desde `sudo` para cambiar el escritorio de otra sesión.

## Paso 2 — Declarar un propietario del proceso

Crea `include/video.hpp`:

```cpp
#pragma once
#include "screen.hpp"
#include <optional>
#include <string>
#include <sys/types.h>

class X11VideoBackend {
public:
    X11VideoBackend() = default;
    ~X11VideoBackend();
    X11VideoBackend(const X11VideoBackend&) = delete;
    X11VideoBackend& operator=(const X11VideoBackend&) = delete;

    void start(const std::string& path,
               const std::optional<ScreenInfo>& screen = std::nullopt);
    bool healthy();
    void stop() noexcept;
    const std::string& socket_path() const noexcept { return socket_path_; }
private:
    pid_t child_ = -1;
    std::string socket_path_;
};
```

El objeto posee **una** instancia. El destructor garantiza el intento de parada incluso si sale una excepción en otro módulo. No se copia: dos objetos con el mismo PID podrían detener el proceso equivocado. PRD 05 guardará un objeto por monitor.

## Paso 3 — Implementar inicio, salud y parada

Crea `src/video.cpp` completo. Se crea un grupo de procesos propio para detener `xwinwrap` y su reproductor. No se usa `-d` (daemonizar) de `xwinwrap`, porque el C++ perdería la propiedad del hijo. La ruta del socket IPC de mpv vive bajo `XDG_RUNTIME_DIR`, con carpeta privada.

```cpp
#include "video.hpp"
#include "error.hpp"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
namespace fs = std::filesystem;

namespace {
std::string position(int value) {
    return (value < 0 ? "" : "+") + std::to_string(value);
}
}

X11VideoBackend::~X11VideoBackend() { stop(); }

void X11VideoBackend::start(const std::string& path,
                            const std::optional<ScreenInfo>& screen) {
    if (child_ != -1) throw std::logic_error("video already started");
    const char* session = std::getenv("XDG_SESSION_TYPE");
    if (!session || std::string(session) != "x11" || !std::getenv("DISPLAY")) {
        throw WallpaperError(ErrorCode::unsupported_capability,
                             "xwinwrap video requires an X11 graphical session");
    }
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (!runtime) {
        throw WallpaperError(ErrorCode::backend_unavailable,
                             "XDG_RUNTIME_DIR is not set");
    }
    const fs::path video = fs::canonical(path);
    if (!fs::is_regular_file(video)) throw std::runtime_error("video is not a file");
    const std::string extension = video.extension().string();
    if (extension != ".mp4" && extension != ".mkv" && extension != ".webm") {
        throw std::invalid_argument("expected .mp4, .mkv or .webm video");
    }
    const fs::path runtime_dir = fs::path(runtime) / "wallpaper-daemon";
    fs::create_directories(runtime_dir);
    fs::permissions(runtime_dir, fs::perms::owner_all,
                    fs::perm_options::replace);
    socket_path_ = (runtime_dir /
                    ("mpv-" + std::to_string(getpid()) + "-" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".sock")).string();

    std::vector<std::string> args = {
        "xwinwrap", "-b", "-s", "-st", "-sp", "-nf", "-ni", "-ov", "-fdt"
    };
    if (screen) {
        args.push_back("-g");
        args.push_back(std::to_string(screen->width) + "x" +
                       std::to_string(screen->height) + position(screen->x) +
                       position(screen->y));
    } else {
        args.push_back("-fs");
    }
    args.insert(args.end(), {"--", "mpv", "-wid", "WID", "--no-config",
                             "--no-audio", "--no-osc", "--no-input-default-bindings",
                             "--loop-file=inf", "--input-ipc-server=" + socket_path_,
                             video.string()});
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);

    posix_spawnattr_t attributes;
    int error = posix_spawnattr_init(&attributes);
    if (error != 0) throw std::system_error(error, std::generic_category(), "spawnattr");
    error = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    if (error == 0) error = posix_spawnattr_setpgroup(&attributes, 0);
    if (error != 0) {
        posix_spawnattr_destroy(&attributes);
        throw std::system_error(error, std::generic_category(), "process group");
    }
    error = posix_spawnp(&child_, "xwinwrap", nullptr, &attributes,
                         argv.data(), environ);
    posix_spawnattr_destroy(&attributes);
    if (error != 0) {
        child_ = -1;
        throw std::system_error(error, std::generic_category(), "cannot start xwinwrap");
    }
}

bool X11VideoBackend::healthy() {
    if (child_ == -1) return false;
    int status = 0;
    const pid_t result = waitpid(child_, &status, WNOHANG);
    if (result == 0) return true;
    if (result == child_) {
        // El grupo puede conservar un mpv hijo: intenta pararlo antes de olvidar el PID.
        kill(-child_, SIGTERM);
        child_ = -1;
        return false;
    }
    if (errno == EINTR) return true;
    throw std::system_error(errno, std::generic_category(), "video waitpid");
}

void X11VideoBackend::stop() noexcept {
    if (child_ != -1) {
        kill(-child_, SIGTERM);
        int status = 0;
        for (int attempt = 0; attempt < 20; ++attempt) {
            const pid_t result = waitpid(child_, &status, WNOHANG);
            if (result == child_ || (result == -1 && errno == ECHILD)) {
                child_ = -1;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (child_ != -1) {
            kill(-child_, SIGKILL);
            while (waitpid(child_, &status, 0) == -1 && errno == EINTR) {}
            child_ = -1;
        }
    }
    if (!socket_path_.empty()) {
        std::error_code ignored;
        fs::remove(socket_path_, ignored);
        socket_path_.clear();
    }
}
```

El socket contiene PID y dirección del objeto para evitar colisiones entre instancias del mismo proceso; el directorio se limita al usuario. `healthy()` comprueba si el proceso principal sigue vivo, pero **no** certifica que la ventana esté detrás de otras ni que el decodificador produzca fotogramas. Esa parte requiere observación visual. `stop()` intenta terminar el grupo, espera hasta dos segundos y fuerza la parada si hace falta.

## Paso 4 — Herramienta de prueba manual

Crea `src/video_inspect.cpp`:

```cpp
#include "video.hpp"
#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: video-inspect VIDEO_FILE\n";
        return 2;
    }
    try {
        X11VideoBackend backend;
        backend.start(argv[1]);
        std::cout << "video started; press Enter to stop\n";
        std::cin.get();
        if (!backend.healthy()) {
            std::cerr << "video process exited early\n";
            return 1;
        }
        backend.stop();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "video: " << error.what() << '\n';
        return 1;
    }
}
```

Después de los targets del PRD 03 en `CMakeLists.txt`, agrega:

```cmake
add_library(wallpaper-video STATIC src/video.cpp)
target_include_directories(wallpaper-video PUBLIC include)
target_link_libraries(wallpaper-video PUBLIC wallpaper-core)
add_executable(video-inspect src/video_inspect.cpp)
target_link_libraries(video-inspect PRIVATE wallpaper-video)
```

Configura, compila y ejecuta `./build/video-inspect /ruta/a/video.mp4` desde X11. Abre otras ventanas, comprueba foco y visibilidad, pulsa Enter, y verifica que no queda `xwinwrap` ni el `mpv` hijo de **esa ejecución**. Si `xwinwrap` sale pronto, `healthy()` debe devolver falso. Repite con archivo inexistente, extensión inválida, `XDG_SESSION_TYPE=wayland` y dependencia `xwinwrap` ausente. Usa [la matriz de compatibilidad](../docs/compatibility.md) para registrar escritorio y versión.

## Aceptación y transición

El backend X11 es válido en una sesión concreta solo cuando inicia, muestra vídeo detrás de las ventanas, no roba foco y se detiene sin hijos. Cualquier otra sesión recibe error de capacidad o disponibilidad. No se promete soporte Wayland genérico: [wlr-layer-shell](https://github.com/swaywm/wlr-protocols/blob/master/unstable/wlr-layer-shell-unstable-v1.xml) define superficies de fondo en compositores que lo implementan, pero no convierte el `WID` X11 de `mpv` en una superficie Wayland. El [PRD 05](05-video-por-pantalla.md) reutiliza este propietario por monitor.
