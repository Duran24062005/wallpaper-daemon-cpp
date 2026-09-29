# PRD 06 — Daemon, CLI e IPC

**Problema y motivo.** El proceso necesita operación sin GUI y control local.

**Módulos y secuencia.** Añadir parser CLI, ciclo daemon, socket IPC, comandos de estado/aplicar/detener y respuestas estructuradas.

**C++ frente a Rust.** Gestionar señales, sockets y threads con RAII; contrastar con mensajes tipados, `Result` y ownership entre tareas.

**Entrada y resultado.** Argumentos o mensajes producen respuesta con estado, error y código consistente.

**Validación y aceptación.** Probar comando inválido, desconexión y concurrencia sin sockets inseguros ni estados ambiguos.

