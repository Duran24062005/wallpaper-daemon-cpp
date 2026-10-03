# Contrato local IPC `v1`

Este contrato empieza en el [PRD 06](../prds/06-daemon-cli-e-ipc.md). **El binario actual todavía no abre un socket.** La ruta es `$XDG_RUNTIME_DIR/wallpaper-daemon/control.sock`, dentro de un directorio privado del usuario; el servicio no escucha TCP/HTTP. Cada conexión envía **una** línea JSON UTF-8 terminada en `\n`, recibe **una** línea JSON y se cierra. El límite es 4096 bytes por línea y se aplica antes de parsear. El cliente espera hasta dos segundos; el servidor procesa una conexión a la vez en esta primera versión.

## Sobre común

Petición: `{"v":1,"op":"status"}`. Respuesta exitosa: `{"v":1,"ok":true,...}`. Respuesta fallida: `{"v":1,"ok":false,"error":{"code":"bad_request","message":"..."}}`. La propiedad `v` es un entero; versiones distintas de `1` producen `unsupported_version`. `op` es una cadena; operaciones desconocidas producen `unknown_operation`. Campos extra no tienen significado en `v1`; no se usan para decidir acciones.

| `op` | Campos adicionales | Efecto y respuesta |
|---|---|---|
| `status` | ninguno | `backend`, `mode`, `directory`, `interval_seconds`, `last_image`, `last_error` |
| `next` | ninguno | Detiene vídeo activo, aplica la siguiente imagen global y devuelve `image` |
| `stop` | ninguno | Responde y termina el proceso después de cerrar la conexión |
| `list-screens` | ninguno | Array `screens` con `id`, `width`, `height`, `x`, `y`, `primary`; no cambia nada |
| `set-image` | `path`, opcional `screen` | Aplica imagen global o por pantalla; pausa la rotación automática (`mode=manual_image`) |
| `set-video` | `path` | Inicia vídeo global X11; `mode=video` |
| `set-videos` | `items`: array de `{screen,path}` | Inicia una instancia X11 por pantalla; `mode=multi_video` |
| `config` | `field`: `folder`, `interval` o `backend`; `value` | Valida, guarda y actualiza configuración; devuelve `config` |

`path` es un path local. El servidor lo canonicaliza antes de aplicarlo y no ejecuta su texto como shell. `screen` utiliza el namespace del proveedor activo: `plasma:N` en Plasma, nombre de conector X11 en Xfce. Un ID de otro namespace es un error. `set-image` por pantalla no se convierte automáticamente en un cambio global. Los resultados de `set-videos` pueden ser parciales solo si un backend externo falla después de iniciar; el gestor del PRD 05 intenta conservar la configuración anterior hasta iniciar la nueva.

## Errores y códigos de salida

`bad_request` cubre JSON inválido, tipos incorrectos, línea demasiado grande y campos requeridos ausentes. `unsupported_version` y `unknown_operation` son errores de contrato. `unsupported_capability` significa que el backend no sabe hacer esa operación; `backend_unavailable` que la dependencia/sesión requerida no está disponible; `operation_failed` que se intentó y falló. El CLI devuelve `0` cuando `ok=true`, `1` para un error de operación/conexión y `2` para uso inválido del CLI. La GUI muestra el `message`, conserva el `code` para elegir la acción y nunca deduce éxito solo porque el proceso CLI pudo iniciarse.

## Estado y persistencia

La configuración de usuario está en `$XDG_CONFIG_HOME/wallpaper-daemon/config.conf` o, si esa variable falta, `~/.config/wallpaper-daemon/config.conf`. Formato `clave=valor` con claves `folder`, `interval`, `backend`; se guarda mediante archivo temporal y renombrado para no dejar una configuración a medias. El socket, PID e IPC de mpv son **temporales** bajo `XDG_RUNTIME_DIR`; no se guardan en Git ni en la configuración. Las imágenes y vídeos personales nunca se copian ni borran por una petición IPC.

## Prueba de compatibilidad

Un cliente `v2` obtiene `unsupported_version` sin ejecutar la operación. Una línea `{"v":1,"op":"set-image","path":42}` obtiene `bad_request`, no termina el daemon. Una segunda petición usa una nueva conexión. Si en el futuro se añaden eventos o streaming, se define `v2` o un endpoint distinto; no se cambia el significado de una respuesta `v1` existente.
