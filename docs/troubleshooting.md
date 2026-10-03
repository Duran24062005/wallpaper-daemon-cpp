# Diagnóstico de errores

Empieza por **el comando exacto que falló** y por el PRD que estabas siguiendo. No cambies varios módulos a la vez para perseguir un mensaje.

| Síntoma | Causa probable | Qué comprobar y corregir |
|---|---|---|
| `fatal error: ...hpp: No such file` | Header en `include/` o ruta de include incorrecta | En PRD 00 usa las rutas relativas indicadas; en PRD 01 añade `target_include_directories(... PUBLIC include)` y cambia a `#include "..."`. |
| `undefined reference to ...` | Declaración sin definición enlazada o firma distinta | Revisa el `.cpp` correspondiente en `add_executable`/`add_library` y compara firma, `const`, namespace y parámetros. |
| `filesystem` no compila | C++17 no activo o toolchain antigua | Revisa `CMAKE_CXX_STANDARD 17`, borra solo `build/` si la caché apunta a otro compilador y vuelve a configurar. |
| `filesystem_error` al iniciar | `./assets` se resuelve desde otro directorio, carpeta sin permisos o archivo desaparecido | Ejecuta `pwd`, `ls assets` y usa una ruta absoluta en la configuración de prueba. El mensaje debe incluir la ruta. |
| `No wallpapers available` | Carpeta vacía o extensiones distintas | Comprueba que hay archivos regulares `.jpg/.jpeg/.png/.webp`; PRD 00 acepta mayúsculas. |
| `gsettings` falla o no cambia el escritorio | Sesión GNOME ausente, esquema no disponible o proceso fuera de la sesión de usuario | Comprueba `XDG_CURRENT_DESKTOP`, `gsettings list-schemas`, ambas claves `picture-uri*` y el código de salida. No ejecutes el servicio como root para cambiar el fondo del usuario. |
| Imagen con espacios no aparece | Ruta pasada como texto de shell o URI mal codificada | El PRD 00 pasa argumentos separados al proceso y codifica el path como URI `file://`; no uses `std::system` con una ruta interpolada. |
| `Cannot open display` en X11 | `DISPLAY` o sesión X11 ausente | Confirma `XDG_SESSION_TYPE=x11` y `DISPLAY`; en Wayland usa un proveedor del compositor, no `xrandr` como prueba universal. |
| Plasma o Xfce devuelve «no disponible» | Servicio o dependencia del adaptador ausente | Comprueba que la sesión corresponde al backend y consulta la [matriz](compatibility.md). No sustituyas una asignación por pantalla por un cambio global. |
| Vídeo se abre como ventana normal | El backend no creó una superficie de fondo válida | Detén el proceso, revisa `xwinwrap`, X11 y el escritorio probado; informa no soportado si la ventana no queda detrás. |
| CLI no conecta al socket | Daemon apagado, ruta de runtime distinta, permisos o versión incompatible | Compara `$XDG_RUNTIME_DIR`, dueño y modo del socket; consulta el código de error y la versión `v1`. |
| `ctest` indica «No tests were found» | Aún no llegaste al PRD 01 o faltó `enable_testing()` | No lo reportes como éxito; añade el target y `add_test` del PRD 01. |

Para aislar una falla de compilación, ejecuta `cmake --build build --verbose` y lee la **primera** línea de error del compilador. Para aislar una falla de backend, ejecuta la comprobación de capacidad antes de aplicar y conserva stderr del comando externo. Consulta las [pruebas por hito](testing.md) para saber qué demuestra cada resultado.
