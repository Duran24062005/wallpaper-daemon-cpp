# Arquitectura de `wallpaper-daemon-cpp`

## Propósito

El daemon selecciona y aplica fondos de pantalla a partir de una configuración
y de los archivos disponibles. Esta versión C++ sirve también como contraparte
para estudiar ownership, errores, composición de módulos y compilación frente a
la versión de Rust.

## Flujo conceptual

```text
config.cpp ──> configuración válida
                    │
                    v
scanner.cpp ──> fondos encontrados ──> selector.cpp ──> fondo elegido
                                                        │
                                                        v
                       scheduler.cpp ──> wallpapers.cpp ──> escritorio
                              ^
                              │
                         main.cpp
```

`error.cpp` cruza el flujo para representar y comunicar fallos sin ocultarlos.

## Responsabilidades

| Módulo | Responsabilidad | Equivalente en Rust |
|---|---|---|
| `main.cpp` | Punto de entrada y composición de dependencias | `src/main.rs` |
| `config.cpp` | Leer y validar opciones/configuración | `src/config.rs` |
| `scanner.cpp` | Descubrir archivos candidatos | `src/scanner.rs` |
| `selector.cpp` | Elegir un fondo según las reglas | `src/selector.rs` |
| `scheduler.cpp` | Coordinar cambios a lo largo del tiempo | `src/scheduler.rs` |
| `wallpapers.cpp` | Aplicar el fondo al sistema | `src/wallpapers.rs` |
| `error.cpp` | Centralizar errores del dominio | `src/error.rs` |

## Flujo de datos

1. `main` inicia el proceso y construye las dependencias.
2. `config` entrega opciones normalizadas; una configuración inválida detiene el flujo con un error claro.
3. `scanner` transforma una ubicación en una colección de candidatos.
4. `selector` transforma candidatos y reglas en una elección.
5. `scheduler` decide cuándo repetir la operación.
6. `wallpapers` convierte la elección en un efecto observable del escritorio.
7. Los errores vuelven al punto de entrada para logging y código de salida.

## Decisiones y límites

- La selección permanece separada de la integración con el escritorio para poder probarla sin cambiar el wallpaper real.
- La configuración se valida antes de escanear para fallar temprano.
- La paridad con `wallpaper-rs` se alcanza por etapas.
- Video, video por pantalla, CLI/IPC, GUI y empaquetado son objetivos de los PRDs, no capacidades declaradas como terminadas.

## Comparación C++/Rust

| Concepto | C++ | Rust |
|---|---|---|
| Organización | archivos `.cpp` y contratos/build por formalizar | módulos `mod` y `Cargo.toml` |
| Errores | tipos y convenciones del proyecto | `Result` y `?` |
| Ownership | RAII, referencias y responsabilidad del programador | ownership y borrowing comprobados |
| Dependencias | gestor/build por formalizar | Cargo |
| Pruebas | framework y build por configurar | `tests/` y `cargo test` |

