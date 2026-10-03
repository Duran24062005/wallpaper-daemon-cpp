# PRD 06 — Daemon de usuario, CLI e IPC local

## Problema y objetivo

Hasta [PRD 05](05-video-por-pantalla.md), las herramientas son procesos en primer plano y no hay un contrato para controlarlas. Necesitamos un daemon de **usuario** que reciba órdenes locales, responda con estado y despierte sin esperar el intervalo. La solución usa un socket Unix, una petición y una respuesta JSON por conexión, versión `v1`; el [contrato completo](../docs/ipc-protocol.md) también guía a Java. No se expone HTTP ni una interfaz de red.

**Actores:** persona en terminal, GUI del PRD 07 y servicio `systemd --user` del PRD 08. **Persistencia:** carpeta, intervalo y backend en configuración de usuario; el socket es temporal. **Reglas:** un solo daemon por usuario/socket; máximo 4096 bytes por mensaje; errores de versión, capacidad y operación diferenciados. **Fuera de alcance:** acceso remoto, ejecución como root, eventos en streaming y varios clientes simultáneos de larga duración.

## Paso 1 — Hacer persistente `Config`

Reemplaza `include/config.hpp`:

```cpp
#pragma once
#include <filesystem>
#include <string>

struct Config {
    std::string wallpaper_directory;
    int interval_seconds;
    std::string backend = "gnome";
};

Config create_default_config();
void validate_config(const Config& config);
std::filesystem::path config_path();
Config load_config();
void save_config(const Config& config);
```

Reemplaza `src/config.cpp` completo. `folder` puede contener `=` porque se divide por el **primer** signo; se prohíben saltos de línea para que no se creen claves ocultas. El renombrado mantiene el archivo anterior si falla una escritura.

```cpp
#include "config.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>

namespace fs = std::filesystem;

Config create_default_config() { return {"./assets", 60, "gnome"}; }

void validate_config(const Config& config) {
    if (config.wallpaper_directory.empty() ||
        config.wallpaper_directory.find('\n') != std::string::npos) {
        throw std::invalid_argument("invalid wallpaper_directory");
    }
    if (config.interval_seconds <= 0 || config.interval_seconds > 86400) {
        throw std::invalid_argument("interval_seconds must be 1..86400");
    }
    if (config.backend != "gnome" && config.backend != "xfce" &&
        config.backend != "plasma") {
        throw std::invalid_argument("backend must be gnome, xfce or plasma");
    }
}

fs::path config_path() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return fs::path(xdg) / "wallpaper-daemon/config.conf";
    const char* home = std::getenv("HOME");
    if (!home || !*home) throw std::runtime_error("HOME is not set");
    return fs::path(home) / ".config/wallpaper-daemon/config.conf";
}

Config load_config() {
    Config config = create_default_config();
    const fs::path path = config_path();
    if (!fs::exists(path)) return config;
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open config: " + path.string());
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto equal = line.find('=');
        if (equal == std::string::npos) throw std::runtime_error("invalid config line");
        const auto key = line.substr(0, equal);
        const auto value = line.substr(equal + 1);
        if (key == "folder") config.wallpaper_directory = value;
        else if (key == "interval") {
            std::size_t used = 0;
            config.interval_seconds = std::stoi(value, &used);
            if (used != value.size()) throw std::runtime_error("invalid interval");
        } else if (key == "backend") config.backend = value;
        else throw std::runtime_error("unknown config key: " + key);
    }
    if (!file.eof()) throw std::runtime_error("error reading config");
    validate_config(config);
    return config;
}

void save_config(const Config& config) {
    validate_config(config);
    const fs::path path = config_path();
    fs::create_directories(path.parent_path());
    const fs::path temporary = path.string() + ".tmp-" + std::to_string(getpid());
    try {
        std::ofstream file(temporary, std::ios::trunc);
        if (!file) throw std::runtime_error("cannot create config temporary file");
        fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::replace);
        file << "folder=" << config.wallpaper_directory << '\n'
             << "interval=" << config.interval_seconds << '\n'
             << "backend=" << config.backend << '\n';
        file.close();
        if (!file) throw std::runtime_error("cannot write config");
        fs::rename(temporary, path);
    } catch (...) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}
```

El valor por defecto `./assets` sigue ayudando al desarrollo desde la raíz; **antes de habilitar el servicio** configura una ruta absoluta para que no dependa de su directorio de trabajo. `save_config` escribe solo tres claves documentadas. Prueba archivo inexistente, línea inválida, intervalo `0`, `86401` y una carpeta con espacios. No uses `.env` para guardar preferencias ordinarias.

## Paso 2 — Crear el transporte Unix

Crea `include/ipc.hpp`:

```cpp
#pragma once
#include <string>
#include <unistd.h>

struct SocketHandle {
    int value;
    explicit SocketHandle(int fd) : value(fd) {}
    ~SocketHandle() { if (value >= 0) close(value); }
    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;
};

std::string control_socket_path();
int open_listener();
int connect_to_daemon();
std::string read_line(int fd);
void write_line(int fd, const std::string& text);
```

Crea `src/ipc.cpp`. El directorio es privado; un lock de usuario evita dos instancias y permite retirar un socket obsoleto después de una caída. `read_line` limita tamaño y el tiempo de espera protege al servidor frente a un cliente que conecta y no envía nada.

```cpp
#include "ipc.hpp"
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/un.h>

namespace fs = std::filesystem;

std::string control_socket_path() {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (!runtime || !*runtime) throw std::runtime_error("XDG_RUNTIME_DIR is not set");
    return (fs::path(runtime) / "wallpaper-daemon/control.sock").string();
}

namespace {
sockaddr_un address_for(const std::string& path) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path)) {
        throw std::runtime_error("socket path is too long");
    }
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    return address;
}
}

int open_listener() {
    const auto path = control_socket_path();
    const auto folder = fs::path(path).parent_path();
    fs::create_directories(folder);
    fs::permissions(folder, fs::perms::owner_all, fs::perm_options::replace);
    static SocketHandle lock_fd(-1); // Permanece abierto mientras vive el daemon.
    lock_fd.value = open((folder / "control.lock").c_str(),
                         O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock_fd.value < 0) throw std::system_error(errno, std::generic_category(), "lock file");
    if (flock(lock_fd.value, LOCK_EX | LOCK_NB) != 0) {
        throw std::runtime_error("another wallpaper daemon holds the lock");
    }
    if (fs::exists(path)) {
        SocketHandle probe(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
        if (probe.value < 0) throw std::system_error(errno, std::generic_category(), "probe socket");
        const auto old_address = address_for(path);
        if (connect(probe.value, reinterpret_cast<const sockaddr*>(&old_address),
                    sizeof(old_address)) == 0) {
            throw std::runtime_error("another wallpaper daemon is running");
        }
        if (errno != ECONNREFUSED && errno != ENOENT) {
            throw std::system_error(errno, std::generic_category(), "probe old socket");
        }
        fs::remove(path);
    }
    SocketHandle socket_fd(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (socket_fd.value < 0) throw std::system_error(errno, std::generic_category(), "socket");
    const auto address = address_for(path);
    if (bind(socket_fd.value, reinterpret_cast<const sockaddr*>(&address),
             sizeof(address)) != 0) {
        throw std::system_error(errno, std::generic_category(), "bind");
    }
    if (chmod(path.c_str(), 0600) != 0) {
        const int code = errno;
        fs::remove(path);
        throw std::system_error(code, std::generic_category(), "chmod socket");
    }
    if (listen(socket_fd.value, 8) != 0) {
        fs::remove(path);
        throw std::system_error(errno, std::generic_category(), "listen");
    }
    const int result = socket_fd.value;
    socket_fd.value = -1;
    return result;
}

int connect_to_daemon() {
    const auto path = control_socket_path();
    SocketHandle socket_fd(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (socket_fd.value < 0) throw std::system_error(errno, std::generic_category(), "socket");
    const auto address = address_for(path);
    if (connect(socket_fd.value, reinterpret_cast<const sockaddr*>(&address),
                sizeof(address)) != 0) {
        throw std::system_error(errno, std::generic_category(), "connect");
    }
    const int result = socket_fd.value;
    socket_fd.value = -1;
    return result;
}

std::string read_line(int fd) {
    timeval timeout{2, 0};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        throw std::system_error(errno, std::generic_category(), "socket timeout");
    }
    std::string line;
    char character = 0;
    while (line.size() < 4096) {
        const ssize_t count = recv(fd, &character, 1, 0);
        if (count == 0) throw std::runtime_error("connection closed before newline");
        if (count < 0) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "recv");
        }
        if (character == '\n') return line;
        line += character;
    }
    throw std::runtime_error("IPC line exceeds 4096 bytes");
}

void write_line(int fd, const std::string& text) {
    if (text.size() >= 4096 || text.find('\n') != std::string::npos) {
        throw std::runtime_error("invalid IPC response size or newline");
    }
    const std::string line = text + '\n';
    std::size_t sent = 0;
    while (sent < line.size()) {
        const ssize_t count = send(fd, line.data() + sent, line.size() - sent,
                                   MSG_NOSIGNAL);
        if (count < 0) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "send");
        }
        sent += static_cast<std::size_t>(count);
    }
}
```

El `SOCK_CLOEXEC` evita que hijos como `gsettings`, `qdbus6` o `xwinwrap` hereden el socket. El ejemplo comprueba `chmod` y `setsockopt`, restringe el directorio y toma un lock antes de decidir si un socket previo quedó obsoleto.

## Paso 3 — Dependencia JSON y daemon

Instala el paquete de desarrollo `nlohmann-json3-dev` de tu distribución. Su [documentación oficial de CMake](https://json.nlohmann.me/integration/cmake/) define `nlohmann_json::nlohmann_json`. Esta dependencia aparece **solo al llegar a IPC**; los PRD 00–05 no la necesitan. Reemplaza `src/main.cpp` del bucle antiguo por el programa siguiente. El estado vive en un hilo; `poll` espera el socket o el próximo ciclo, sin busy wait.

```cpp
#include "config.hpp"
#include "cycle.hpp"
#include "error.hpp"
#include "ipc.hpp"
#include "multi_video.hpp"
#include "plasma.hpp"
#include "screen.hpp"
#include "video.hpp"
#include "wallpapers.hpp"
#include "xfce.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <poll.h>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/socket.h>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;
namespace {
volatile std::sig_atomic_t stop_signal = 0;
void on_signal(int) { stop_signal = 1; }

struct State {
    Config config = load_config();
    std::unique_ptr<WallpaperBackend> backend;
    X11VideoBackend global_video;
    MultiVideoManager screen_videos;
    std::optional<std::string> last_image;
    std::mt19937 random_engine{std::random_device{}()};
    std::string mode = "automatic";
    std::string last_error;
    bool running = true;
    bool reset_timer = false;
};

std::unique_ptr<WallpaperBackend> make_backend(const std::string& name) {
    if (name == "gnome") return std::make_unique<GnomeBackend>();
    if (name == "xfce") return std::make_unique<XfceBackend>();
    if (name == "plasma") return std::make_unique<PlasmaBackend>();
    throw std::invalid_argument("unknown backend");
}

std::string error_code(ErrorCode code) {
    switch (code) {
    case ErrorCode::unsupported_capability: return "unsupported_capability";
    case ErrorCode::backend_unavailable: return "backend_unavailable";
    case ErrorCode::operation_failed: return "operation_failed";
    }
    return "operation_failed";
}

json failure(const std::string& code, const std::string& message) {
    return {{"v", 1}, {"ok", false},
            {"error", {{"code", code}, {"message", message}}}};
}

std::vector<ScreenInfo> current_screens(const State& state) {
    if (state.config.backend == "plasma") return PlasmaScreenProvider{}.list_screens();
    return XrandrScreenProvider{}.list_screens();
}

json handle(State& state, const json& request) {
    if (!request.is_object()) return failure("bad_request", "expected JSON object");
    if (!request.contains("v") || !request["v"].is_number_integer() ||
        request["v"].get<int>() != 1) {
        return failure("unsupported_version", "expected IPC version 1");
    }
    if (!request.contains("op") || !request["op"].is_string()) {
        return failure("bad_request", "missing string op");
    }
    const std::string op = request["op"].get<std::string>();
    if (op == "status") {
        json video_status = json::array();
        if (state.mode == "multi_video") {
            for (const auto& item : state.screen_videos.status()) {
                video_status.push_back({{"screen", item.screen_id},
                                        {"healthy", item.healthy}});
            }
        } else if (state.mode == "video") {
            video_status.push_back({{"screen", "global"},
                                    {"healthy", state.global_video.healthy()}});
        }
        return {{"v", 1}, {"ok", true}, {"backend", state.config.backend},
                {"mode", state.mode}, {"directory", state.config.wallpaper_directory},
                {"interval_seconds", state.config.interval_seconds},
                {"last_image", state.last_image.value_or("")},
                {"last_error", state.last_error}, {"videos", video_status}};
    }
    if (op == "stop") { state.running = false; return {{"v", 1}, {"ok", true}}; }
    if (op == "list-screens") {
        json screens = json::array();
        for (const auto& screen : current_screens(state)) {
            screens.push_back({{"id", screen.id}, {"width", screen.width},
                               {"height", screen.height}, {"x", screen.x},
                               {"y", screen.y}, {"primary", screen.primary}});
        }
        return {{"v", 1}, {"ok", true}, {"screens", screens}};
    }
    if (op == "next") {
        state.global_video.stop(); state.screen_videos.stop_all();
        state.mode = "automatic";
        state.reset_timer = true;
        const auto image = run_cycle(state.config, *state.backend,
                                     state.last_image, state.random_engine);
        state.last_error.clear();
        return {{"v", 1}, {"ok", true}, {"image", image}};
    }
    if (op == "set-image") {
        const auto path = request.at("path").get<std::string>();
        if (request.contains("screen")) {
            state.backend->set_image_for_screen(
                request.at("screen").get<std::string>(), path);
        } else {
            state.backend->set_image_global(path);
            state.last_image = fs::canonical(path).string();
        }
        state.global_video.stop(); state.screen_videos.stop_all();
        state.mode = "manual_image"; state.last_error.clear();
        return {{"v", 1}, {"ok", true}};
    }
    if (op == "set-video") {
        const auto path = request.at("path").get<std::string>();
        state.global_video.stop();
        state.global_video.start(path);
        state.screen_videos.stop_all();
        state.mode = "video"; state.last_error.clear();
        return {{"v", 1}, {"ok", true}};
    }
    if (op == "set-videos") {
        const auto screens = XrandrScreenProvider{}.list_screens();
        std::vector<ScreenVideo> requested;
        for (const auto& item : request.at("items")) {
            const auto id = item.at("screen").get<std::string>();
            const auto path = item.at("path").get<std::string>();
            const auto found = std::find_if(screens.begin(), screens.end(),
                [&](const ScreenInfo& screen) { return screen.id == id; });
            if (found == screens.end()) {
                throw WallpaperError(ErrorCode::operation_failed,
                                     "unknown active X11 screen: " + id);
            }
            requested.push_back({*found, path});
        }
        state.screen_videos.apply(requested);
        state.global_video.stop();
        state.mode = "multi_video"; state.last_error.clear();
        return {{"v", 1}, {"ok", true}};
    }
    if (op == "config") {
        Config next = state.config;
        const auto field = request.at("field").get<std::string>();
        if (field == "folder") {
            const auto folder = request.at("value").get<std::string>();
            if (!fs::is_directory(folder)) throw std::invalid_argument("folder is not a directory");
            next.wallpaper_directory = fs::canonical(folder).string();
        } else if (field == "interval") {
            next.interval_seconds = request.at("value").get<int>();
        } else if (field == "backend") {
            next.backend = request.at("value").get<std::string>();
        } else return failure("bad_request", "unknown config field");
        validate_config(next);
        auto backend = make_backend(next.backend);
        save_config(next);
        state.config = next;
        state.backend = std::move(backend);
        state.global_video.stop(); state.screen_videos.stop_all();
        state.mode = "automatic";
        state.reset_timer = true;
        return {{"v", 1}, {"ok", true},
                {"config", {{"folder", next.wallpaper_directory},
                            {"interval", next.interval_seconds},
                            {"backend", next.backend}}}};
    }
    return failure("unknown_operation", "unknown op: " + op);
}
}

int main() {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    try {
        State state;
        state.backend = make_backend(state.config.backend);
        SocketHandle listener(open_listener());
        const std::string path = control_socket_path();
        struct SocketPathGuard {
            std::string path;
            ~SocketPathGuard() {
                std::error_code ignored;
                fs::remove(path, ignored);
            }
        } socket_guard{path};
        auto due = std::chrono::steady_clock::now();
        while (state.running && !stop_signal) {
            int timeout = -1;
            if (state.mode == "automatic") {
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    due - std::chrono::steady_clock::now()).count();
                timeout = static_cast<int>(std::min<long long>(
                    std::max<long long>(0, ms), std::numeric_limits<int>::max()));
            }
            pollfd event{listener.value, POLLIN, 0};
            const int ready = poll(&event, 1, timeout);
            if (ready < 0) {
                if (errno == EINTR) continue;
                throw std::system_error(errno, std::generic_category(), "poll");
            }
            if (ready > 0 && (event.revents & POLLIN)) {
                SocketHandle peer(accept4(listener.value, nullptr, nullptr, SOCK_CLOEXEC));
                if (peer.value >= 0) {
                    json response;
                    try {
                        response = handle(state, json::parse(read_line(peer.value)));
                    } catch (const WallpaperError& error) {
                        response = failure(error_code(error.code()), error.what());
                    } catch (const std::exception& error) {
                        response = failure("bad_request", error.what());
                    }
                    write_line(peer.value, response.dump());
                    if (state.mode == "automatic" && state.reset_timer) {
                        due = std::chrono::steady_clock::now() +
                              std::chrono::seconds(state.config.interval_seconds);
                        state.reset_timer = false;
                    }
                }
            }
            if (state.mode == "automatic" && std::chrono::steady_clock::now() >= due) {
                try {
                    state.last_image = run_cycle(state.config, *state.backend,
                                                  state.last_image, state.random_engine);
                    state.last_error.clear();
                } catch (const std::exception& error) {
                    state.last_error = error.what();
                    std::cerr << "cycle: " << error.what() << '\n';
                }
                due = std::chrono::steady_clock::now() +
                      std::chrono::seconds(state.config.interval_seconds);
            }
        }
        state.global_video.stop(); state.screen_videos.stop_all();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "daemon: " << error.what() << '\n';
        return 1;
    }
}
```

El guard de la ruta elimina el socket también si una excepción sale del bucle. `status` no retrasa el siguiente cambio automático; `next` y `config` reinician el temporizador. **Antes de distribución** comprueba que el directorio de runtime pertenece al usuario. Un backend que se bloquee todavía puede retrasar respuestas de otros clientes: añade timeouts a los procesos externos antes de convertir este ejemplo en un servicio permanente.

## Paso 4 — CLI que usa el mismo contrato

Crea `src/wpd.cpp` completo. El CLI construye JSON tipado; no concatena texto para producirlo. `set-videos` recibe pares de ID y ruta. `start` y `restart` se harán con `systemctl --user` una vez instalado el servicio del PRD 08.

```cpp
#include "ipc.hpp"
#include <nlohmann/json.hpp>
#include <charconv>
#include <exception>
#include <iostream>
#include <string>

using json = nlohmann::json;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: wpd status|next|stop|list-screens|set-image|set-video|set-videos|config\n";
        return 2;
    }
    json request = {{"v", 1}};
    const std::string command = argv[1];
    if ((command == "status" || command == "next" || command == "stop" ||
         command == "list-screens") && argc == 2) {
        request["op"] = command;
    } else if (command == "set-image" && (argc == 3 || argc == 4)) {
        request["op"] = command; request["path"] = argv[2];
        if (argc == 4) request["screen"] = argv[3];
    } else if (command == "set-video" && argc == 3) {
        request["op"] = command; request["path"] = argv[2];
    } else if (command == "set-videos" && argc >= 4 && (argc - 2) % 2 == 0) {
        request["op"] = command; request["items"] = json::array();
        for (int i = 2; i < argc; i += 2) {
            request["items"].push_back({{"screen", argv[i]}, {"path", argv[i + 1]}});
        }
    } else if (command == "config" && argc == 4) {
        request["op"] = command; request["field"] = argv[2];
        if (std::string(argv[2]) == "interval") {
            int value = 0;
            const std::string text = argv[3];
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
                std::cerr << "interval must be an integer\n"; return 2;
            }
            request["value"] = value;
        } else request["value"] = argv[3];
    } else {
        std::cerr << "invalid arguments\n";
        return 2;
    }
    try {
        SocketHandle socket(connect_to_daemon());
        write_line(socket.value, request.dump());
        const json response = json::parse(read_line(socket.value));
        std::cout << response.dump(2) << '\n';
        return response.value("ok", false) ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "wpd: " << error.what() << '\n';
        return 1;
    }
}
```

## Paso 5 — CMake, uso y criterios

En `CMakeLists.txt`, **debajo** de `find_package`/targets del PRD 05, agrega la dependencia y el transporte; modifica el target del daemon para enlazar adaptadores y JSON:

```cmake
find_package(nlohmann_json 3.11 CONFIG REQUIRED)
add_library(wallpaper-ipc STATIC src/ipc.cpp)
target_include_directories(wallpaper-ipc PUBLIC include)
target_link_libraries(wallpaper-daemon PRIVATE
    wallpaper-core wallpaper-gnome wallpaper-xfce wallpaper-plasma
    wallpaper-video wallpaper-ipc nlohmann_json::nlohmann_json)
add_executable(wpd src/wpd.cpp)
target_link_libraries(wpd PRIVATE wallpaper-ipc nlohmann_json::nlohmann_json)
```

Las líneas de enlace anteriores **se añaden** a las existentes; si `wallpaper-daemon` aún compila `src/scheduler.cpp`, puedes dejarlo sin usar durante esta transición y quitarlo en un cambio separado. El proceso permanece en primer plano; en otra terminal del mismo usuario/sesión ejecuta:

```bash
cmake -S . -B build
cmake --build build
./build/wallpaper-daemon
./build/wpd status
./build/wpd next
./build/wpd list-screens
./build/wpd config folder /ruta/absoluta/a/imagenes
./build/wpd config interval 30
./build/wpd stop
```

La respuesta de `status` debe incluir `"v": 1`, `"ok": true`, modo y backend; `stop` debe hacer que desaparezca el socket. La guía de [pruebas](../docs/testing.md) enumera mensajes inválidos, timeout y permisos. Verifica que dos instancias no comparten socket, que un JSON `v:2` recibe `unsupported_version`, que un fallo de `set-image` no indica `ok=true` y que `next` no espera 30 segundos. **Riesgo abierto:** el ejemplo de servidor procesa peticiones en serie; un backend bloqueado retrasa otras respuestas. Registra ese límite y añade timeouts de procesos antes de distribuir. Sigue [PRD 07](07-gui-java.md) y [08](08-empaquetado-y-distribucion.md).
