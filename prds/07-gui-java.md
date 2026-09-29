# PRD 07 — GUI Java

**Problema y motivo.** La operación manual requiere interfaz sin duplicar la lógica del daemon.

**Módulos y secuencia.** Estabilizar IPC; después crear pantallas de configuración, estado y errores como cliente del protocolo.

**C++ frente a Rust.** C++ permanece como servicio y Java como cliente; Rust sigue siendo referencia del dominio.

**Entrada y resultado.** Acciones de usuario producen comandos IPC y estado renderizado.

**Validación y aceptación.** Probar daemon apagado, reconexión y configuración inválida; la GUI no debe inventar reglas.

