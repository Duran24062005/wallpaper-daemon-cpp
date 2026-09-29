# PRD 01 — Workspace y arquitectura core

**Problema y motivo.** Los módulos deben compilarse y probarse como una unidad mantenible.

**Módulos y secuencia.** Formalizar headers, tipos de dominio, build, logging y tests; `main` ensambla y los servicios no dependen de él.

**C++ frente a Rust.** Usar headers, namespaces, const-correctness y RAII; contrastar con módulos, visibilidad, structs/enums y Cargo.

**Entrada y resultado.** Fuentes y dependencias producen un build reproducible, interfaces documentadas y pruebas aisladas.

**Validación y aceptación.** Compilar desde un directorio limpio y ejecutar tests sin divergencias entre headers e implementación.

