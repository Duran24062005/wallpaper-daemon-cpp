# wallpaper-daemon-cpp

Proyecto práctico para aprender C++17 construyendo, por etapas, un cambiador de fondos de pantalla para Linux. **Estado del código hoy:** el ejecutable lee una configuración fija, enumera imágenes de `assets/` y muestra una ruta seleccionada. Todavía no cambia el fondo, no espera entre ciclos y no es un daemon. Los archivos vacíos `src/wallpapers.cpp`, `src/scheduler.cpp` y `src/error.cpp` son trabajo pendiente; los PRD explican cómo completarlo.

## Ejecutar exactamente lo que existe

Desde la raíz de **este repositorio**:

```bash
cmake -S . -B build
cmake --build build
./build/wallpaper-daemon
```

Salida de referencia con los siete archivos de `assets/`:

```text
Wallpaper Daemon - C++
Starting application...
wallpaper directory: ./assets
interval seconds: 60
Wallpapers found: 7
Selected wallpaper: ./assets/...
```

El nombre concreto de la imagen puede variar porque `std::filesystem::directory_iterator` no promete un orden. Ejecuta desde la raíz: `./assets` es una ruta relativa al directorio de trabajo, no al binario. El comando antiguo `g++ src/main.cpp -o output/main` **no enlaza** los otros módulos y no sirve para el estado actual.

## Ruta para construirlo tú

1. [Lee el mapa del código actual](docs/Architecture.md) y [la guía de CMake](docs/CMake.md).
2. Sigue [los nueve PRD en orden](prds/README.md). **Cada PRD distingue código existente de código que escribirás**.
3. Usa [pruebas](docs/testing.md), [diagnóstico de errores](docs/troubleshooting.md) y [compatibilidad](docs/compatibility.md) en cada hito.

La primera meta es un cambiador global de imágenes para GNOME; las demás fases añaden separación de módulos, monitores, vídeo, control local, interfaz y distribución. La implementación de referencia en [wallpaper-rs](../wallpaper-rs/README.md) ayuda a comparar ideas, pero sus PRD tampoco son funcionalidades ya terminadas. Cada repositorio se construye y mantiene por separado.

## Mapa rápido

| Archivo actual | Qué hace hoy | Primer PRD que lo amplía |
|---|---|---|
| `src/main.cpp` | Imprime configuración, cuenta imágenes y selecciona índice `0` | [00](prds/00-mvp-y-robustez.md) |
| `src/config.cpp`, `include/config.hpp` | Crean carpeta `./assets` e intervalo `60` | [00](prds/00-mvp-y-robustez.md) |
| `src/scanner.cpp`, `include/scanner.hpp` | Filtran cuatro extensiones, con mayúsculas aún sin tratar | [00](prds/00-mvp-y-robustez.md) |
| `src/selector.cpp`, `include/selector.hpp` | Devuelven el elemento de un índice o lanzan error | [00](prds/00-mvp-y-robustez.md) |
| `src/wallpapers.cpp`, `src/scheduler.cpp`, `src/error.cpp` | Vacíos | [00](prds/00-mvp-y-robustez.md) |
| `CMakeLists.txt` | Compila `main` y tres módulos `.cpp` | [00](prds/00-mvp-y-robustez.md) |

## Alcance de las guías

Los ejemplos usan C++17, CMake y APIs locales de Linux. Las operaciones de escritorio dependen de la sesión y del backend: consulta la [matriz de capacidades](docs/compatibility.md) antes de probar una fase. Un test con backend simulado comprueba la lógica, pero no prueba que un compositor acepte una operación gráfica.

- [ChatGPT Chat](https://chatgpt.com/c/6abc2b99-7c58-83ea-b939-5ac5f161e3b5)
