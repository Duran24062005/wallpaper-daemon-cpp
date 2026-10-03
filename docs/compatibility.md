# Compatibilidad y capacidades

**Todas las filas son metas de las guías, no funcionalidades del binario actual.** La compilación local no demuestra una operación gráfica. Registra para cada prueba la distribución, versión del escritorio, `XDG_SESSION_TYPE`, `XDG_CURRENT_DESKTOP`, número de monitores y resultado observado. Si una dependencia o una API no está disponible, el comando debe devolver un error `unsupported_capability` o `backend_unavailable` con el nombre del backend.

| Entorno de referencia | Imagen global | Imagen por monitor | Vídeo de fondo | Cómo se verifica |
|---|---|---|---|---|
| GNOME, X11 o Wayland | PRD 00: `gsettings` sobre `picture-uri` y `picture-uri-dark` | No se promete con GSettings global | No se promete en esta guía | Leer ambas claves y comprobar el escritorio visualmente |
| KDE Plasma 6, X11 o Wayland | PRD 03: [scripting de Plasma](https://develop.kde.org/docs/plasma/scripting/api/) | PRD 03: un *desktop containment* por pantalla/actividad | No se promete con el backend de imagen | Comprobar cada pantalla y actividad activa |
| Xfce 4.20 sobre X11 | PRD 03: [xfconf](https://developer.xfce.org/xfconf/) | PRD 03: propiedades de `xfce4-desktop` descubiertas en la sesión | PRD 04–05: **experimental**, solo si `xwinwrap` queda visible detrás de las ventanas | Comprobar propiedades y resultado visual, con uno y dos monitores |
| Compositor Wayland con `wlr-layer-shell` | No incluido en el primer adaptador | No incluido en el primer adaptador | Se documenta el protocolo; sin backend de vídeo validado se informa no disponible | Detectar el protocolo anunciado antes de intentar una superficie |
| Cinnamon u otro escritorio | Sin adaptador en esta ruta | Sin adaptador | Sin adaptador | `backend_unavailable`, nunca éxito aparente |

Plasma expone `desktopForScreen` y la configuración de wallpaper de cada contenedor en su [API oficial](https://develop.kde.org/docs/plasma/scripting/api/). Xfce expone canales de configuración mediante [xfconf](https://developer.xfce.org/xfconf/). GNOME documenta su clave [global `picture-uri`](https://help.gnome.org/system-admin-guide/desktop-background.html). El protocolo [wlr-layer-shell](https://github.com/swaywm/wlr-protocols/blob/master/unstable/wlr-layer-shell-unstable-v1.xml) define una capa `background` cuando el compositor lo implementa; eso no significa que todas las sesiones Wayland lo implementen. [mpv](https://mpv.io/manual/stable/) tiene IPC local y admite incrustación de ventana en X11; [xwinwrap](https://github.com/mmhobi7/xwinwrap) es una utilidad separada para colocar esa ventana en el fondo. Comprueba versiones y opciones en tu sistema antes de seguir PRD 04.

## Regla de adaptación

No deduzcas capacidades solo de `XDG_CURRENT_DESKTOP`: puede contener varios nombres y no garantiza que el servicio o herramienta esté ejecutándose. El adaptador comprueba su dependencia y operación concreta. `list-screens` no aplica fondos. `set-image --screen` no cambia silenciosamente toda la sesión si el backend no soporta pantalla individual. El vídeo nunca arranca como ventana normal fingiendo ser wallpaper. El usuario debe poder distinguir `supported`, `unsupported_capability`, `backend_unavailable` y un fallo operativo.

## Matriz de pruebas manuales

1. Imagen global: anota URI anterior, aplica imagen, comprueba la clave y la superficie visible; restaura la URI anterior al acabar.
2. Dos pantallas: guarda las asignaciones anteriores, aplica dos imágenes distintas, comprueba cada monitor y reconecta uno.
3. Vídeo: verifica que permanece detrás de ventanas normales, no toma foco y se detiene sin hijos huérfanos. Si no se cumple, marca el backend como no soportado en esa combinación.
4. Sesión sin backend: el mismo comando debe fallar con diagnóstico y código distinto de cero, sin modificar otro escritorio.
