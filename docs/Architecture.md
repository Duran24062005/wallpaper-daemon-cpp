# Arquitectura: estado actual y evolución

## Estado comprobado en el repositorio

```text
main.cpp
  ├── create_default_config() -> Config { "./assets", 60 }
  ├── scan_wallpapers(directory) -> vector<string>
  └── select_wallpaper(paths, 0) -> string
```

`main` imprime la selección y termina. `scanner` usa `directory_iterator` sin ordenar; acepta `.jpg`, `.jpeg`, `.png` y `.webp` en minúsculas. `selector` exige un índice válido. `scheduler.cpp`, `wallpapers.cpp` y `error.cpp` están vacíos y no participan en CMake. El nombre «daemon» todavía expresa la intención, no el comportamiento. La versión Rust hermana sí tiene un bucle GNOME de imágenes, pero no implementa todos sus PRD futuros.

## Flujo que construirás en los PRD 00 y 01

```text
Config validada ──> scanner ──> selector ──> WallpaperBackend ──> escritorio
     │                 │            │              │
     └─────────────── errores al ciclo principal ──┘
                                 │
                           scheduler
```

Cada ciclo vuelve a escanear, intenta elegir una imagen distinta de la aplicada en este proceso, la aplica y espera. Una carpeta vacía o un fallo transitorio se registra y se vuelve a intentar sin un bucle ocupado. El PRD 01 separa la lógica comprobable del adaptador GNOME para que los tests no cambien el escritorio.

| Responsabilidad | Entrada | Salida | Efectos permitidos |
|---|---|---|---|
| Configuración | valores por defecto y, más tarde, archivo del usuario | carpeta e intervalo válidos | leer configuración |
| Scanner | carpeta | rutas de imágenes ordenadas | leer archivos |
| Selector | candidatos, última imagen y generador aleatorio | imagen elegida o error | ninguno |
| Backend de imagen | ruta absoluta | resultado o error de capacidad | cambiar fondo |
| Scheduler | duración y orden de parada | próximo ciclo | esperar |
| Proceso principal | dependencias anteriores | código de salida y diagnóstico | componer componentes |

## Límite entre núcleo y plataformas

El núcleo conoce rutas, pantallas, reglas y estados; no conoce `gsettings`, `xfconf-query`, Plasma, `mpv` ni sockets. Un `WallpaperBackend` declara si puede aplicar una imagen global o por pantalla. Un `ScreenProvider` entrega identificadores y geometría. `X11VideoBackend` posee cada proceso de reproducción. Cuando falta una capacidad, devuelve `ErrorCode::unsupported_capability` y el CLI la muestra; no finge éxito ni reutiliza silenciosamente un fondo global. El [PRD 01](../prds/01-workspace-y-arquitectura-core.md) fija los primeros tipos y la propiedad de cada objeto.

La secuencia de dependencia es deliberada: [00](../prds/00-mvp-y-robustez.md) enseña el ciclo global funcional; [01](../prds/01-workspace-y-arquitectura-core.md) extrae contratos y pruebas; [02](../prds/02-deteccion-de-pantallas.md) detecta monitores sin cambiar fondos; [03](../prds/03-fondos-por-pantalla.md) aplica imágenes según capacidad; [04–05](../prds/04-video-global.md) añaden vídeo; [06](../prds/06-daemon-cli-e-ipc.md) introduce el servicio local; [07](../prds/07-gui-java.md) es un cliente; [08](../prds/08-empaquetado-y-distribucion.md) distribuye artefactos.

## Por qué separar

La selección se prueba con rutas y un generador determinista. El backend se prueba con un sustituto que guarda llamadas. Las pruebas del adaptador real comprueban procesos, D-Bus o xfconf y requieren la sesión adecuada. Esta separación evita que un fallo de GNOME parezca un fallo del algoritmo. Los recursos nativos (procesos, descriptores, sockets) necesitan un propietario y una liberación definidos; los ejemplos posteriores muestran RAII y parada limpia.
