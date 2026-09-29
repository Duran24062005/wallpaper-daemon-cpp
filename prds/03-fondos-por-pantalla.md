# PRD 03 — Fondos por pantalla

**Problema y motivo.** Cada monitor puede necesitar una selección distinta.

**Módulos y secuencia.** Extender `config`, `scanner`, `selector` y `wallpapers`: detectar, resolver reglas, seleccionar y aplicar independientemente.

**C++ frente a Rust.** Controlar copias y referencias; contrastar con structs, iteradores y `Result`.

**Entrada y resultado.** Pantallas, reglas y candidatos producen un fondo por pantalla y reportes individuales.

**Validación y aceptación.** Probar regla sin coincidencia, pantalla nueva y fallo aislado sin corromper asignaciones restantes.

