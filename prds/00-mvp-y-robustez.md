# PRD 00 — MVP y robustez

**Problema y motivo.** Necesitamos un flujo mínimo confiable antes de agregar pantallas, video o IPC. Una configuración inválida, carpeta inexistente o colección vacía debe producir un diagnóstico determinista.

**Módulos y secuencia.** `main`, `config`, `scanner`, `selector`, `wallpapers` y `error`: validar, escanear, seleccionar, aplicar y devolver código de salida. El scheduler se incorpora después.

**C++ frente a Rust.** Definir contratos, RAII y errores explícitos en C++; contrastar con `Result`, `Option` y `?` en Rust.

**Entrada y resultado.** Ruta, extensiones y reglas producen un fondo aplicado o un error accionable.

**Validación y aceptación.** Probar ruta inexistente, carpeta vacía, extensión no soportada y éxito normal; no ocultar errores ni filtrar recursos.

