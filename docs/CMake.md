# CMake

Sí. Vamos a introducir **CMake ahora**, antes de seguir creando más módulos. Así no tendremos que modificar nuestros comandos de compilación cada vez que agreguemos un `.cpp`.

La progresión será:

```text
CMakeLists.txt
      ↓
CMake conoce src/
      ↓
CMake encuentra los .cpp
      ↓
CMake genera el sistema de build
      ↓
cmake --build build
      ↓
ejecutable
```

## 1. Crear `CMakeLists.txt`

Debemos colocarlo en la **raíz del proyecto**, no dentro de `src`.

Tu estructura quedará:

```text
wallpaper-daemon-cpp/
├── CMakeLists.txt       ← NUEVO
├── README.md
├── src/
│   ├── config.cpp
│   ├── config.hpp
│   ├── error.cpp
│   ├── main.cpp
│   ├── scanner.cpp
│   ├── scheduler.cpp
│   ├── selector.cpp
│   └── wallpapers.cpp
└── output/
```

Desde la raíz del proyecto ejecuta:

```bash
touch CMakeLists.txt
```

Puedes comprobarlo:

```bash
ls
```

---

# 2. Escribir nuestro primer `CMakeLists.txt`

Abre:

```text
CMakeLists.txt
```

y coloca **todo este contenido**:

```cmake
cmake_minimum_required(VERSION 3.16)

project(wallpaper-daemon-cpp
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(wallpaper-daemon
    src/main.cpp
    src/config.cpp
)
```

Ahora vamos a entenderlo antes de ejecutar nada.

---

## 3. `cmake_minimum_required`

```cmake
cmake_minimum_required(VERSION 3.16)
```

Le estamos diciendo a CMake:

> Este proyecto necesita como mínimo CMake 3.16.

No significa que tengamos que utilizar exactamente esa versión.

Si tienes una versión más nueva, funcionará.

Puedes comprobar la tuya:

```bash
cmake --version
```

---

# 4. `project`

Tenemos:

```cmake
project(wallpaper-daemon-cpp
    VERSION 0.1.0
    LANGUAGES CXX
)
```

Aquí estamos definiendo nuestro proyecto.

```text
Nombre:
wallpaper-daemon-cpp

Versión:
0.1.0

Lenguaje:
C++
```

`CXX` es la forma que utiliza CMake para referirse a C++.

---

# 5. Elegir el estándar de C++

Tenemos:

```cmake
set(CMAKE_CXX_STANDARD 17)
```

Estamos diciendo:

> Quiero compilar utilizando C++17.

Y:

```cmake
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

significa:

> C++17 es obligatorio; no quiero que CMake intente utilizar una versión anterior.

Más adelante podremos pasar a C++20 o C++23 si tiene sentido, pero para este proyecto **C++17 es suficiente**.

---

# 6. La parte más importante: `add_executable`

Tenemos:

```cmake
add_executable(wallpaper-daemon
    src/main.cpp
    src/config.cpp
)
```

Aquí estamos diciendo:

> Quiero crear un ejecutable llamado `wallpaper-daemon` utilizando estos archivos fuente.

Es equivalente conceptualmente a nuestro comando anterior:

```bash
g++ src/main.cpp src/config.cpp -o output/main
```

pero ahora la información queda **guardada en el proyecto**.

Por eso posteriormente podremos agregar:

```text
src/scanner.cpp
```

y modificar CMake, en lugar de tener que recordar todos los archivos en cada compilación.

---

# 7. Crear el directorio de compilación

Ahora viene una práctica importante con CMake.

**No vamos a generar los archivos de compilación dentro de `src/`.**

Vamos a crear:

```text
build/
```

Ejecuta:

```bash
mkdir -p build
```

Tu proyecto tendrá:

```text
wallpaper-daemon-cpp/
├── build/              ← archivos generados por CMake
├── CMakeLists.txt
├── src/
│   ├── config.cpp
│   ├── config.hpp
│   └── main.cpp
└── ...
```

`build/` es una carpeta de **build out-of-source**.

Esto mantiene separado:

```text
código fuente
```

de:

```text
archivos generados por compilación
```

---

# 8. Configurar CMake

Ahora ejecuta desde la raíz:

```bash
cmake -S . -B build
```

Aquí:

```text
-S .
```

significa:

> El código fuente está en el directorio actual.

Y:

```text
-B build
```

significa:

> Coloca los archivos generados por CMake dentro de `build/`.

Deberías ver algo parecido a:

```text
-- The CXX compiler identification is GNU ...
-- Detecting CXX compiler ABI info
-- Configuring done
-- Generating done
-- Build files have been written to: .../wallpaper-daemon-cpp/build
```

---

# 9. Compilar

Ahora viene la parte que queríamos:

```bash
cmake --build build
```

CMake leerá:

```text
CMakeLists.txt
```

y sabrá que debe compilar:

```text
main.cpp
config.cpp
```

No necesitas escribirlos manualmente.

La salida debería terminar aproximadamente en:

```text
[100%] Linking CXX executable wallpaper-daemon
[100%] Built target wallpaper-daemon
```

---

# 10. Ejecutar nuestro programa

Como CMake genera el ejecutable dentro de `build`, primero podemos localizarlo:

```bash
find build -type f -executable
```

Probablemente encontrarás algo como:

```text
build/wallpaper-daemon
```

Entonces:

```bash
./build/wallpaper-daemon
```

Y deberías obtener:

```text
Wallpaper Daemon - C++!
Starting application...
Wallpaper directory: ./assets
Interval: 60 seconds
```

---

# 11. Nuestro flujo ahora cambió

Antes:

```text
Modificar código
      ↓
recordar todos los .cpp
      ↓
g++ src/main.cpp src/config.cpp ...
      ↓
ejecutar
```

Ahora:

```text
Modificar código
      ↓
cmake --build build
      ↓
ejecutar
```

Y cuando agreguemos:

```text
scanner.cpp
selector.cpp
scheduler.cpp
wallpapers.cpp
```

CMake será quien gestione la compilación.

---

## 12. ¿Y qué hacemos con `output/`?

Por ahora **no lo borres todavía**.

Anteriormente estábamos generando:

```text
output/main
```

con `g++`.

Ahora CMake genera su propio ejecutable:

```text
build/wallpaper-daemon
```

Una vez comprobemos que todo funciona correctamente con CMake, podemos eliminar `output/` y dejar que el proyecto utilice exclusivamente:

```text
build/
```

como directorio de compilación.

---

## 13. Los 3 comandos que debes recordar

A partir de ahora:

### Primera configuración

```bash
cmake -S . -B build
```

### Compilar

```bash
cmake --build build
```

### Ejecutar

```bash
./build/wallpaper-daemon
```

En el futuro podemos crear un pequeño `build.sh` para convertir incluso todo eso en:

```bash
./build.sh
```

pero **todavía no lo hagamos**. Primero quiero que entiendas qué está haciendo CMake.

### Haz ahora exactamente esto

Desde:

```text
~/Desktop/wallpapers-daemon-main/wallpaper-daemon-cpp
```

ejecuta, en orden:

```bash
cmake --version
```

```bash
cmake -S . -B build
```

```bash
cmake --build build
```

```bash
./build/wallpaper-daemon
```

Pásame la salida de esos comandos. Si todo funciona, nuestro siguiente paso será **limpiar `output/` y conectar CMake con el próximo módulo: `scanner.cpp` + `std::filesystem`**, que es donde empezaremos a hacer que el programa encuentre las imágenes reales.
