# PRDs y ruta de implementación

Los PRDs describen cómo evolucionar el daemon C++ hasta la cobertura funcional de
`wallpaper-rs`. Deben leerse en orden: cada etapa crea una base que la siguiente
reutiliza.

## Secuencia

1. [MVP y robustez](00-mvp-y-robustez.md)
2. [Workspace y arquitectura core](01-workspace-y-arquitectura-core.md)
3. [Detección de pantallas](02-deteccion-de-pantallas.md)
4. [Fondos por pantalla](03-fondos-por-pantalla.md)
5. [Video global](04-video-global.md)
6. [Video por pantalla](05-video-por-pantalla.md)
7. [Daemon, CLI e IPC](06-daemon-cli-e-ipc.md)
8. [GUI Java](07-gui-java.md)
9. [Empaquetado y distribución](08-empaquetado-y-distribucion.md)

Cada PRD sigue la misma plantilla: problema, motivo, módulos, implementación C++,
contraste Rust, entradas, resultados, validaciones, riesgos y aceptación.

