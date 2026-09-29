# PRD 05 — Video por pantalla

**Problema y motivo.** El video global no permite experiencias distintas por monitor.

**Módulos y secuencia.** Resolver recursos por pantalla, crear supervisores independientes y coordinar su parada desde `scheduler`.

**C++ frente a Rust.** Definir ownership y sincronización de cada reproductor; contrastar con tipos de estado y ownership Rust.

**Entrada y resultado.** Mapa pantalla-recurso produce reproductores independientes y estado observable.

**Validación y aceptación.** Probar pantalla fallida, reconexión y parada simultánea con limpieza determinista.

