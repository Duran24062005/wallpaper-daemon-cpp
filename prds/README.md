# Ruta de construcción guiada

Esta lista transforma **este** repositorio C++ desde el binario actual hasta un servicio de usuario controlable. Lee en orden: cada PRD nombra archivos que existen al terminar el anterior. **Ninguno de los cambios de código de los PRD se ha aplicado en `src/` por esta documentación**; los escribirás tú. Los bloques `cpp`, `cmake`, `java` e `ini` son material de implementación, no prueba de que esa función exista hoy.

| Orden | Documento | Resultado observable al terminar |
|---|---|---|
| 00 | [MVP y robustez](00-mvp-y-robustez.md) | GNOME cambia una imagen global, espera, reescanea y se detiene con `Ctrl+C` |
| 01 | [Núcleo y arquitectura](01-workspace-y-arquitectura-core.md) | Biblioteca comprobable, adaptador GNOME y primer CTest |
| 02 | [Detección de pantallas](02-deteccion-de-pantallas.md) | `screen-inspect` enumera salidas X11 activas sin cambiarlas |
| 03 | [Imágenes por pantalla](03-fondos-por-pantalla.md) | Asignaciones y adaptadores Xfce/Plasma; GNOME rechaza la capacidad que no ofrece |
| 04 | [Vídeo global](04-video-global.md) | Reproducción de fondo en una sesión X11 compatible y parada sin hijo huérfano |
| 05 | [Vídeo por pantalla](05-video-por-pantalla.md) | Un proceso por salida X11, con estado y limpieza independientes |
| 06 | [Daemon, CLI e IPC](06-daemon-cli-e-ipc.md) | Socket local `v1`, `wpd`, configuración de usuario y control sin esperar el ciclo |
| 07 | [GUI Java](07-gui-java.md) | Aplicación cliente independiente que invoca `wpd` sin duplicar reglas |
| 08 | [Paquetes](08-empaquetado-y-distribucion.md) | Unidad `systemd --user`, instalación y paquetes inspeccionables |

## Cómo estudiar cada paso

1. Lee el problema y señala qué archivo **ya tienes** y cuál vas a crear. Si te saltaste un PRD, no copies el ejemplo siguiente sobre código de otra etapa.
2. Escribe los bloques en el orden indicado. Si dice «sustituye el archivo entero», no dejes funciones antiguas. Si dice «debajo de», conserva el bloque anterior.
3. Compila después de cada conjunto coherente de headers y `.cpp`, corrige el primer error del compilador y registra qué aprendiste.
4. Ejecuta las pruebas del hito y compara el comportamiento esperado. Usa [pruebas](../docs/testing.md) y [diagnóstico](../docs/troubleshooting.md); las pruebas de escritorio requieren el backend real.
5. Anota en el repositorio el resultado manual que dependa de tu distribución o compositor. La [matriz de compatibilidad](../docs/compatibility.md) impide confundir «compila» con «funciona en mi sesión».

## Regla para varias plataformas

GNOME, Plasma, Xfce, X11 y Wayland no comparten una API única de fondos por monitor. Cada PRD explica qué adaptador se escribe y qué devuelve cuando la capacidad no existe. El soporte de vídeo se valida visualmente en el escritorio concreto. La referencia [wallpaper-rs](../../wallpaper-rs/README.md) sirve para comparar conceptos, pero su roadmap no garantiza funciones implementadas en C++ ni en Rust.

El [README raíz](../README.md) describe **solo el código presente hoy**. Actualízalo cuando completes un PRD: anota nuevo comportamiento, comandos reales, dependencias y limitaciones. No dejes el estado del proyecto únicamente en la conversación.
