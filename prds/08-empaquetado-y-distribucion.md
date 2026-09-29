# PRD 08 — Empaquetado y distribución

**Problema y motivo.** Un build local no basta para instalar y actualizar el daemon.

**Módulos y secuencia.** Definir build release, instalación, configuración, servicio, logs, desinstalación y CI.

**C++ frente a Rust.** Fijar toolchain, ABI y dependencias nativas; contrastar con toolchain y `Cargo.lock`.

**Entrada y resultado.** Código, toolchain y assets producen un paquete instalable, documentación y rollback.

**Validación y aceptación.** Instalar, actualizar, desinstalar y ejecutar en entorno limpio; verificar versionado, checksums y ausencia de secretos.

