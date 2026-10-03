# Pruebas por hito

## Antes de escribir código

Comprueba el estado base desde la raíz:

```bash
cmake -S . -B build
cmake --build build
./build/wallpaper-daemon
git status --short
```

La última línea permite distinguir tus archivos de los artefactos de `build/`. Hoy no hay `enable_testing()` ni `tests/`, así que `ctest` no es una prueba válida de la aplicación. Conserva una copia de la salida base para comparar después.

## PRD 00: primer fondo funcional

Sigue el código del PRD y verifica en este orden:

1. Compila después de cada bloque de archivos. Un fallo de enlazado suele indicar un `.cpp` ausente en CMake.
2. Crea una carpeta temporal con `mktemp -d`, coloca imágenes con extensiones `.jpg`, `.JPEG`, `.png` y un `.txt`; el scanner debe devolver solo las tres imágenes, ordenadas.
3. Ejecuta desde la raíz con `assets/`; el proceso debe registrar `Applied wallpaper` y continuar hasta `Ctrl+C`. Comprueba `gsettings get org.gnome.desktop.background picture-uri` y `picture-uri-dark` **en una sesión GNOME**. Restaura los valores previos si haces una prueba manual sobre tu escritorio.
4. Renombra temporalmente la carpeta de prueba o usa una configuración de prueba con ruta inválida; el mensaje debe identificar la ruta y el proceso no debe girar consumiendo CPU. No cambies tus fondos personales para probar un error.

La prueba con un `gsettings` simulado en `PATH` verifica argumentos y códigos de salida, pero **no** demuestra que GNOME cambió el fondo. El PRD 01 aporta pruebas automatizadas de scanner y selector sin sesión gráfica.

## PRD 01 y posteriores

Una vez incluido el CMake de tests:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Para un test determinista, usa una carpeta temporal creada por la prueba y un `std::mt19937` con semilla fija. No asumas el orden del sistema de archivos. El test de un backend sustituido comprueba qué operación se pidió; la prueba manual de integración comprueba que el escritorio la aceptó. No mezcles ambos resultados en un único «funciona».

| PRD | Casos mínimos antes de pasar al siguiente |
|---|---|
| 00 | carpeta vacía/inexistente, extensiones, una imagen, fallo de `gsettings`, `Ctrl+C` |
| 01 | selección sin repetición con dos imágenes, CMake limpio, backend fake, config inválida |
| 02 | cero/una/dos pantallas, salida desconectada, proveedor ausente, solo lectura |
| 03 | aplicación por pantalla soportada; GNOME rechaza la operación; fallo parcial identificable |
| 04 | dependencia ausente, archivo inválido, proceso que muere, parada y ausencia de hijos |
| 05 | una instancia fallida no detiene las demás; reconexión; parada simultánea |
| 06 | versión IPC inválida, comando desconocido, `next` interrumpe espera, socket privado |
| 07 | daemon apagado, respuesta de error, JSON inválido, ruta con espacios |
| 08 | instalación limpia, actualización que conserva configuración, desinstalación sin borrar imágenes |

## Registro de evidencia

En cada PRD anota: comando ejecutado, resultado esperado, resultado real y entorno. Para integración gráfica agrega `XDG_SESSION_TYPE` y `XDG_CURRENT_DESKTOP`. Si no tienes el entorno requerido, marca «no ejecutado» y conserva la prueba simulada; no conviertas esa ausencia en un aprobado.
