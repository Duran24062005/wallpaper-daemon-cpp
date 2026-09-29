# PRD 02 — Detección de pantallas

**Problema y motivo.** Un fondo global no representa escritorios con varios monitores.

**Módulos y secuencia.** Añadir modelo `Screen` y adaptador del sistema: consultar, normalizar y entregar una colección estable.

**C++ frente a Rust.** Encapsular handles con RAII; contrastar con structs, ownership y el scanner Rust.

**Entrada y resultado.** Backend gráfico produce pantallas identificables con geometría y errores distinguibles.

**Validación y aceptación.** Probar una, varias, desconectadas y backend ausente; liberar siempre los recursos.

