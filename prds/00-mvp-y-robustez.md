# PRD 00 — De selector a cambiador global de imágenes

## Problema, meta y punto de partida

Tenemos `main.cpp`, `config`, `scanner` y `selector`; hoy la aplicación imprime una ruta y termina. Para **cambiar el fondo de GNOME repetidamente** necesitamos: validar opciones, obtener rutas comparables, elegir sin repetir cuando hay alternativas, llamar a GSettings, esperar y volver a escanear. Este PRD enseña un ciclo en primer plano que se detiene con `Ctrl+C`; el servicio y el CLI llegan en el [PRD 06](06-daemon-cli-e-ipc.md).

**Actores:** quien ejecuta el programa en una sesión GNOME y quien aprende C++ modificando este repositorio. **Fuera de alcance:** monitores individuales, vídeo, configuración persistente y ejecución en segundo plano. **Entrada:** `./assets` e intervalo de 60 segundos. **Salida:** URI aplicada a las claves globales de GNOME o un diagnóstico; tras un error recuperable, el siguiente ciclo vuelve a intentar.

Trabaja desde la raíz del repositorio. Al terminar **cada paso**, ejecuta `cmake -S . -B build` y `cmake --build build` cuando el conjunto de declaraciones y definiciones ya esté completo. Sustituye **el contenido entero** de los archivos indicados; así no quedan firmas viejas mezcladas con las nuevas. Esta es una receta de aprendizaje: el código siguiente **aún no está** en `src/`.

## Paso 1 — Validar la configuración que ya existe

Tenemos `Config { wallpaper_directory, interval_seconds }`; si el intervalo es cero, el bucle no esperará. En `include/config.hpp`, reemplaza el archivo completo por:

```cpp
#pragma once
#include <string>

struct Config {
    std::string wallpaper_directory;
    int interval_seconds;
};

Config create_default_config();
void validate_config(const Config& config);
```

En `src/config.cpp`, conserva la creación de valores y agrega la definición de validación **debajo** de `create_default_config`:

```cpp
#include "../include/config.hpp"
#include <stdexcept>

Config create_default_config() {
    return {"./assets", 60};
}

void validate_config(const Config& config) {
    if (config.wallpaper_directory.empty()) {
        throw std::invalid_argument("wallpaper_directory must not be empty");
    }
    if (config.interval_seconds <= 0) {
        throw std::invalid_argument("interval_seconds must be positive");
    }
}
```

La función no comprueba aún que la carpeta exista: una carpeta puede desaparecer y volver durante la ejecución. El scanner informará ese problema en cada ciclo. `const Config&` evita una copia y expresa que validar no modifica la configuración.

## Paso 2 — Normalizar y ordenar candidatos

El header `include/scanner.hpp` **no cambia**. Reemplaza `src/scanner.cpp` completo:

```cpp
#include "../include/scanner.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

std::vector<std::string> scan_wallpapers(const std::string& directory) {
    namespace fs = std::filesystem;
    std::vector<std::string> wallpapers;

    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".jpg" || extension == ".jpeg" ||
            extension == ".png" || extension == ".webp") {
            wallpapers.push_back(fs::absolute(entry.path()).lexically_normal().string());
        }
    }
    std::sort(wallpapers.begin(), wallpapers.end());
    return wallpapers;
}
```

`directory_iterator` visita solo la carpeta indicada, no subcarpetas; lanzar una `filesystem_error` por carpeta inexistente es útil para el diagnóstico de `main`. Convertimos la extensión a minúsculas antes de filtrar. Las rutas absolutas permiten comparar la elección anterior incluso si se ejecuta desde otro directorio, y el orden facilita tests reproducibles. `lexically_normal` no comprueba el archivo; `set_wallpaper` lo resolverá de nuevo antes de aplicarlo.

## Paso 3 — Seleccionar sin repetir cuando sea posible

El selector actual exige un índice fijo (`0` en `main`). Sustituye `include/selector.hpp`:

```cpp
#pragma once
#include <optional>
#include <random>
#include <string>
#include <vector>

std::string select_wallpaper(
    const std::vector<std::string>& wallpapers,
    const std::optional<std::string>& previous,
    std::mt19937& random_engine
);
```

Sustituye `src/selector.cpp`:

```cpp
#include "../include/selector.hpp"
#include <stdexcept>

std::string select_wallpaper(
    const std::vector<std::string>& wallpapers,
    const std::optional<std::string>& previous,
    std::mt19937& random_engine
) {
    if (wallpapers.empty()) {
        throw std::runtime_error("No wallpapers available");
    }
    std::vector<std::size_t> candidates;
    for (std::size_t index = 0; index < wallpapers.size(); ++index) {
        if (!previous || wallpapers[index] != *previous) {
            candidates.push_back(index);
        }
    }
    if (candidates.empty()) {
        // Una sola imagen: repetirla es mejor que detener el programa.
        candidates.push_back(0);
    }
    std::uniform_int_distribution<std::size_t> distribution(0, candidates.size() - 1);
    return wallpapers[candidates[distribution(random_engine)]];
}
```

El generador se crea **una vez** en `main` y se presta por referencia; una semilla fija en tests produce resultados reproducibles. Con dos o más imágenes se excluye la anterior. Con una sola imagen se permite repetir. Si se retiraron imágenes entre ciclos y `previous` ya no aparece, todas las actuales son candidatas.

## Paso 4 — Crear el límite con GNOME

Necesitamos invocar `gsettings` con argumentos separados para que espacios o caracteres de shell en la ruta no se interpreten como comandos. No uses `std::system("gsettings ..." + path)`. En Linux, `posix_spawnp` crea el proceso y `waitpid` recoge su estado. El escritorio recibe una **URI** `file://`, no una ruta con espacios sin codificar. Crea `include/wallpapers.hpp`:

```cpp
#pragma once
#include <string>

void set_wallpaper(const std::string& path);
```

Ahora coloca **todo** esto en el archivo vacío `src/wallpapers.cpp`:

```cpp
#include "../include/wallpapers.hpp"

#include <cerrno>
#include <filesystem>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/wait.h>
#include <vector>

extern char** environ;

namespace {
std::string to_file_uri(const std::string& path) {
    namespace fs = std::filesystem;
    const std::string absolute = fs::canonical(path).string();
    const char hex[] = "0123456789ABCDEF";
    std::string uri = "file://";
    for (unsigned char c : absolute) {
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                           (c >= '0' && c <= '9') || c == '/' || c == '-' ||
                           c == '_' || c == '.' || c == '~';
        if (plain) {
            uri += static_cast<char>(c);
        } else {
            uri += '%';
            uri += hex[c >> 4];
            uri += hex[c & 0x0F];
        }
    }
    return uri;
}

void set_key(const std::string& key, const std::string& uri) {
    std::vector<std::string> arguments = {
        "gsettings", "set", "org.gnome.desktop.background", key, uri
    };
    std::vector<char*> argv;
    for (auto& argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    pid_t child = 0;
    const int spawn_error = posix_spawnp(&child, "gsettings", nullptr,
                                         nullptr, argv.data(), environ);
    if (spawn_error != 0) {
        throw std::system_error(spawn_error, std::generic_category(),
                                "Cannot start gsettings");
    }
    int status = 0;
    while (waitpid(child, &status, 0) == -1) {
        if (errno != EINTR) {
            throw std::system_error(errno, std::generic_category(),
                                    "Cannot wait for gsettings");
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw std::runtime_error("gsettings failed for " + key +
                                 "; inspect its stderr and the GNOME session");
    }
}
}  // namespace

void set_wallpaper(const std::string& path) {
    const std::string uri = to_file_uri(path);
    set_key("picture-uri", uri);
    set_key("picture-uri-dark", uri);
}
```

`fs::canonical` rechaza una imagen borrada entre escaneo y aplicación. El bucle codifica bytes no seguros en la URI, incluidos espacios y `#`; no transforma un nombre UTF-8 en otro nombre. `posix_spawnp` evita un intérprete de comandos. `waitpid` maneja la interrupción `EINTR`. Escribimos ambas claves para los temas claro y oscuro; [GNOME documenta `picture-uri`](https://help.gnome.org/system-admin-guide/desktop-background.html). **Límite:** si la primera escritura funciona y la segunda falla, el escritorio queda parcialmente actualizado; el error lo indica y el siguiente ciclo reintenta. No prometas una operación atómica entre claves.

## Paso 5 — Añadir una espera cancelable sencilla

Crea `include/scheduler.hpp`:

```cpp
#pragma once
#include <csignal>

void wait_interval(int seconds, volatile std::sig_atomic_t& stop_requested);
```

Pon en `src/scheduler.cpp`:

```cpp
#include "../include/scheduler.hpp"
#include <chrono>
#include <thread>

void wait_interval(int seconds, volatile std::sig_atomic_t& stop_requested) {
    for (int elapsed = 0; elapsed < seconds && !stop_requested; ++elapsed) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
```

La espera en tramos de un segundo permite responder a `Ctrl+C` sin esperar los 60 segundos completos. El PRD 06 reemplazará esta solución por una espera despertable por `next` y `stop`; no introduzcas hilos o sockets todavía.

## Paso 6 — Componer el ciclo en `main`

Reemplaza `src/main.cpp` completo. Las excepciones de un ciclo se registran y se vuelve a intentar **después** de esperar; la configuración inválida sí termina el proceso con código `1`.

```cpp
#include <csignal>
#include <exception>
#include <iostream>
#include <optional>
#include <random>
#include <string>

#include "../include/config.hpp"
#include "../include/scanner.hpp"
#include "../include/scheduler.hpp"
#include "../include/selector.hpp"
#include "../include/wallpapers.hpp"

namespace {
volatile std::sig_atomic_t stop_requested = 0;
void request_stop(int) { stop_requested = 1; }
}

int main() {
    std::signal(SIGINT, request_stop);
    std::signal(SIGTERM, request_stop);
    try {
        const Config config = create_default_config();
        validate_config(config);
        std::mt19937 random_engine(std::random_device{}());
        std::optional<std::string> last_applied;
        std::cout << "Wallpaper Daemon - C++\n";

        while (!stop_requested) {
            try {
                const auto images = scan_wallpapers(config.wallpaper_directory);
                const auto selected = select_wallpaper(images, last_applied,
                                                        random_engine);
                set_wallpaper(selected);
                last_applied = selected;  // Solo después de aplicar con éxito.
                std::cout << "Applied wallpaper: " << selected << std::endl;
            } catch (const std::exception& error) {
                std::cerr << "Wallpaper cycle failed: " << error.what() << '\n';
            }
            if (!stop_requested) {
                wait_interval(config.interval_seconds, stop_requested);
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Startup failed: " << error.what() << '\n';
        return 1;
    }
}
```

La variable del handler es `sig_atomic_t`: el handler solo asigna una señal de parada; imprimir o reservar memoria dentro de él sería inapropiado. `last_applied` no intenta leer una selección de un proceso anterior. Si quieres conservar estado entre ejecuciones, eso requiere configuración/estado persistente posterior.

## Paso 7 — Enlazar lo nuevo

Abre `CMakeLists.txt`. Dentro del `add_executable(wallpaper-daemon ...)` existente, **debajo de `src/selector.cpp` y antes de `)`**, añade:

```cmake
    src/wallpapers.cpp
    src/scheduler.cpp
```

`src/error.cpp` sigue vacío; no lo agregues para fingir una capa de errores. El PRD 01 introduce contratos de error. Configura y compila:

```bash
cmake -S . -B build
cmake --build build
./build/wallpaper-daemon
```

Salida esperada en una sesión GNOME con imágenes:

```text
Wallpaper Daemon - C++
Applied wallpaper: /ruta/absoluta/.../assets/3.jpeg
```

El proceso queda activo; otra imagen aparecerá después de 60 segundos. Detén con `Ctrl+C`. El archivo elegido es aleatorio. En una terminal aparte consulta las claves:

```bash
gsettings get org.gnome.desktop.background picture-uri
gsettings get org.gnome.desktop.background picture-uri-dark
```

## Validaciones, fallos y aceptación

- Con `assets/` y GNOME, ambas claves contienen una URI `file://` y se observa el fondo. Guarda y restaura los valores anteriores si pruebas sobre tu escritorio real.
- Con dos imágenes, los ciclos consecutivos no eligen la misma; con una, la repetición es permitida. Al agregar una imagen, el siguiente escaneo la ve.
- Una carpeta vacía produce `No wallpapers available`; una ruta inexistente produce `filesystem_error`; ambos casos esperan antes de reintentar.
- Sin `gsettings` en `PATH`, o sin sesión compatible, stderr explica el fallo. El programa no afirma que aplicó la imagen.
- Una ruta con espacios llega como **un solo argumento** y como URI codificada; no se ejecuta texto de la ruta como shell.
- `Ctrl+C` termina en aproximadamente un segundo durante la espera. No hay procesos hijo huérfanos después de cada `waitpid`.

El comando y los errores de esta etapa se prueban según [docs/testing.md](../docs/testing.md). Si sale `undefined reference`, revisa las dos líneas de CMake; si `assets` no aparece, revisa `pwd`. **Riesgos abiertos:** el acceso a GSettings es específico de GNOME y las dos claves no forman una transacción. El [PRD 01](01-workspace-y-arquitectura-core.md) extrae el backend para poder probar y sustituirlo.
