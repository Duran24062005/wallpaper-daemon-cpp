# Ejecutar herramientas externas sin concatenar comandos

El PRD 00 usa `posix_spawnp` para `gsettings` y el PRD 02 usa un `popen` de texto **constante** para `xrandr`. El [PRD 03](../prds/03-fondos-por-pantalla.md) necesita pasar rutas y scripts variables a `qdbus6` y `xfconf-query`: una cadena de shell sería vulnerable y rompería rutas con espacios. Este helper encapsula argumentos, stdout, estado y recogida del hijo. Es código para que **lo escribas en esa etapa**, no una biblioteca ya instalada.

## Paso A — Crear `include/process.hpp`

```cpp
#pragma once
#include <string>
#include <vector>

struct ProcessResult {
    int exit_code;
    std::string output;
};

ProcessResult run_process(const std::vector<std::string>& arguments);
```

## Paso B — Crear `src/process.cpp`

```cpp
#include "process.hpp"

#include <cerrno>
#include <stdexcept>
#include <spawn.h>
#include <system_error>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

ProcessResult run_process(const std::vector<std::string>& arguments) {
    if (arguments.empty()) throw std::invalid_argument("empty process arguments");
    int fds[2];
    if (pipe(fds) == -1) {
        throw std::system_error(errno, std::generic_category(), "pipe");
    }

    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    const bool actions_ready = error == 0;
    if (error == 0) error = posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    if (error == 0) error = posix_spawn_file_actions_addclose(&actions, fds[0]);
    if (error == 0) error = posix_spawn_file_actions_addclose(&actions, fds[1]);
    if (error != 0) {
        if (actions_ready) posix_spawn_file_actions_destroy(&actions);
        close(fds[0]); close(fds[1]);
        throw std::system_error(error, std::generic_category(), "spawn actions");
    }

    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    pid_t child = 0;
    const int spawn_error = posix_spawnp(&child, argv[0], &actions,
                                         nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (spawn_error != 0) {
        close(fds[0]);
        throw std::system_error(spawn_error, std::generic_category(),
                                "cannot start " + arguments[0]);
    }

    std::string output;
    char buffer[4096];
    bool oversized = false;
    int read_error = 0;
    for (;;) {
        const ssize_t count = read(fds[0], buffer, sizeof(buffer));
        if (count > 0) {
            if (output.size() + static_cast<std::size_t>(count) <= 1024 * 1024) {
                output.append(buffer, static_cast<std::size_t>(count));
            } else {
                oversized = true; // Sigue drenando para que el hijo pueda salir.
            }
        } else if (count == 0) {
            break;
        } else if (errno != EINTR) {
            read_error = errno;
            break;
        }
    }
    close(fds[0]);
    int status = 0;
    while (waitpid(child, &status, 0) == -1) {
        if (errno != EINTR) {
            throw std::system_error(errno, std::generic_category(), "waitpid");
        }
    }
    if (read_error != 0) {
        throw std::system_error(read_error, std::generic_category(), "read stdout");
    }
    if (oversized) throw std::runtime_error("process output exceeds 1 MiB");
    if (!WIFEXITED(status)) throw std::runtime_error("process ended by signal");
    return {WEXITSTATUS(status), output};
}
```

`run_process` devuelve un código distinto de cero sin lanzar: cada adaptador puede convertirlo en un mensaje que incluya la operación fallida. stderr se deja en la terminal para diagnóstico; el daemon del PRD 06 debe enviarlo a su journal. El límite de 1 MiB evita capturar accidentalmente una salida ilimitada. Para operaciones de larga duración, como `mpv`, **no** uses esta función bloqueante; el PRD 04 enseña un propietario de proceso independiente.

## Prueba del helper

En un archivo de prueba añade:

```cpp
const auto ok = run_process({"printf", "%s", "ruta con espacios"});
assert(ok.exit_code == 0 && ok.output == "ruta con espacios");
const auto fail = run_process({"false"});
assert(fail.exit_code != 0);
```

No interpoles `arguments` en un texto de shell. `posix_spawnp` recibe `argv` y conserva cada argumento aunque contenga espacios, comillas o signos especiales.
