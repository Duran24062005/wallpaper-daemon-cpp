# PRD 04 — Video global

**Problema y motivo.** Una imagen estática no cubre fondos animados.

**Módulos y secuencia.** Extender configuración y `wallpapers` con adaptador de reproducción: validar, iniciar, supervisar y detener limpiamente.

**C++ frente a Rust.** Usar RAII para procesos, streams y handles; contrastar con ownership y errores tipados.

**Entrada y resultado.** Recurso de video y opciones producen reproducción global o diagnóstico de backend/formato.

**Validación y aceptación.** Probar formato inválido, salida del reproductor y apagado sin procesos huérfanos.

