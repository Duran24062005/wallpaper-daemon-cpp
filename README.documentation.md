# Guía documental del proyecto

Este archivo complementa el README existente con la ruta tutorial de
`wallpaper-daemon-cpp`.

## Ruta de lectura

1. Lee [`docs/Architecture.md`](docs/Architecture.md).
2. Ubica los módulos en [`src/`](src/).
3. Sigue [`prds/README.md`](prds/README.md) en orden.
4. Contrasta cada etapa con [`wallpaper-rs`](../wallpaper-rs/README.md).

## Módulos

`main.cpp` compone el programa; `config.cpp` gestiona configuración;
`scanner.cpp` descubre candidatos; `selector.cpp` elige un fondo;
`scheduler.cpp` coordina el tiempo; `wallpapers.cpp` aplica el resultado y
`error.cpp` concentra errores.

La configuración de build todavía debe formalizarse. No se debe asumir que un
comando funciona hasta que exista una configuración versionada y reproducible.
La paridad funcional con Rust es incremental y está descrita en [`prds/`](prds/README.md).

