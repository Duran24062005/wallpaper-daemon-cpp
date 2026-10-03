# PRD 08 — Instalación, servicio y paquetes Linux

## Problema y resultado

Un binario en `build/` depende del árbol de desarrollo, mientras que una instalación debe arrancar desde cualquier directorio y conservar las preferencias del usuario. Este PRD enseña a instalar `wallpaper-daemon`, `wpd` y una unidad `systemd --user`, a construir paquetes `.deb` y `.rpm` con CPack, y a distribuir la GUI como aplicación separada. **No habilites ni arranques el servicio automáticamente al instalar**: necesita la sesión gráfica y una carpeta absoluta configurada por la persona usuaria.

**Actores:** usuario que instala, empaquetador y mantenedor. **Datos:** `~/.config/wallpaper-daemon/config.conf` pertenece al usuario y no al paquete; imágenes personales nunca se incluyen ni se borran. **Riesgos:** herramientas de escritorio opcionales (`gsettings`, `qdbus6`, `xfconf-query`, `mpv`, `xwinwrap`), variables de sesión ausentes, diferencias de directorios por distribución y socket obsoleto tras una caída. La [matriz de compatibilidad](../docs/compatibility.md) define qué se promete por entorno.

## Paso 1 — Crear la unidad de usuario

Crea `packaging/wallpaper-daemon.service`:

```ini
[Unit]
Description=Wallpaper Daemon C++ (sesión de usuario)
After=graphical-session.target
PartOf=graphical-session.target

[Service]
Type=simple
ExecStart=/usr/bin/wallpaper-daemon
Restart=on-failure
RestartSec=5

[Install]
WantedBy=graphical-session.target
```

No uses `User=`: la unidad ya vive en el administrador **del usuario**. No uses `sudo systemctl start` para manejar su wallpaper. `ExecStart=/usr/bin/...` corresponde al prefijo `/usr` usado por los paquetes de este PRD; para una instalación local en otro prefijo hay que generar la unidad con esa ruta, no copiar esta unidad literalmente. `RestartSec` evita una tormenta de reinicios ante una configuración inválida. En escritorios que no activan `graphical-session.target`, `systemctl --user start wallpaper-daemon.service` sigue siendo la prueba explícita tras iniciar sesión.

## Paso 2 — Agregar instalación y CPack a CMake

Al **final** de `CMakeLists.txt` del PRD 06, después de los targets y tests, añade:

```cmake
include(GNUInstallDirs)
install(TARGETS wallpaper-daemon wpd
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/wallpaper-daemon.service
        DESTINATION lib/systemd/user)

set(CPACK_PACKAGE_NAME "wallpaper-daemon-cpp")
set(CPACK_PACKAGE_VENDOR "Proyecto de aprendizaje")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "maintainer@example.invalid")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cambiador local de fondos para Linux")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
include(CPack)
```

Sustituye `maintainer@example.invalid` por un contacto real **antes de publicar** el paquete. CPack empaqueta los artefactos instalados por esas reglas, no `assets/` ni la configuración del usuario. La dependencia C++ de JSON es de compilación; el ejecutable final usa sus cabeceras incluidas en el binario. Las herramientas gráficas externas siguen siendo capacidades opcionales y se diagnostican en ejecución. Si una distribución exige declarar `gsettings` como dependencia dura para el perfil GNOME, añádelo en un perfil de paquete específico y prueba el resultado.

## Paso 3 — Construir y examinar paquetes

Desde la raíz del repositorio, en un árbol limpio y **sin** cambiar `build/` de desarrollo:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-release
ctest --test-dir build-release --output-on-failure
cd build-release
cpack -G DEB
cpack -G RPM
```

`cpack -G RPM` necesita `rpmbuild`; si falta, registra «no probado» en lugar de declarar éxito. Para comprobar el contenido **sin instalar**:

```bash
dpkg-deb --contents wallpaper-daemon-cpp*.deb
rpm -qpl wallpaper-daemon-cpp*.rpm
```

Debes ver `/usr/bin/wallpaper-daemon`, `/usr/bin/wpd` y `/usr/lib/systemd/user/wallpaper-daemon.service`; no debe aparecer una carpeta `assets/`, `.env`, token, socket, PID ni `config.conf`. Si `GNUInstallDirs` o el generador de una distribución escogen otra ruta de unidad, inspecciona el paquete y actualiza la ruta de instalación antes de publicarlo. Añade `build-release/` y artefactos `*.deb`, `*.rpm` al `.gitignore` cuando implementes esta fase.

## Paso 4 — Instalar y configurar como usuario

Después de instalar el paquete con el gestor de la distribución, inicia sesión gráfica y configura una ruta **absoluta**. Para el primer arranque, el daemon puede iniciar con `./assets` y registrar un error de ciclo hasta recibir la carpeta; el canal IPC sigue operativo. Comprueba primero que `$XDG_RUNTIME_DIR` existe en el entorno del servicio.

```bash
systemctl --user daemon-reload
systemctl --user start wallpaper-daemon.service
wpd status
wpd config folder "$HOME/Pictures/Wallpapers"
wpd config interval 60
wpd config backend gnome
wpd next
journalctl --user -u wallpaper-daemon.service -n 50 --no-pager
```

Usa `xfce` o `plasma` en `config backend` solo en su sesión y después de instalar sus herramientas. Para habilitar inicio con la sesión, verifica primero `systemctl --user status graphical-session.target` y luego `systemctl --user enable wallpaper-daemon.service`. La unidad no debe ejecutarse como servicio de sistema. Si `gsettings` falla desde systemd pero funciona en terminal, compara variables de sesión importadas (`DISPLAY`, `WAYLAND_DISPLAY`, `DBUS_SESSION_BUS_ADDRESS`, `XDG_CURRENT_DESKTOP`) y el usuario; no copies secretos a la unidad.

## Paso 5 — Actualización, desinstalación y GUI

Antes de actualizar, anota versión (`wpd status` y versión del paquete), guarda una copia de la configuración y prueba que el paquete nuevo entiende el mismo IPC `v1`. Detén el servicio, actualiza con el gestor de paquetes, recarga unidades y arranca de nuevo. La actualización **no** reescribe `~/.config/wallpaper-daemon/config.conf`. Si cambia el esquema, crea una migración explícita en un PRD futuro en vez de reinterpretar silenciosamente claves antiguas.

Para desinstalar, ejecuta `systemctl --user disable --now wallpaper-daemon.service`, retira el paquete y comprueba que el proceso y socket ya no existen. La configuración del usuario y sus imágenes permanecen. Si quiere borrar su configuración, debe hacerlo de forma explícita; el paquete no borra carpetas personales.

La GUI del [PRD 07](07-gui-java.md) es otro artefacto. Puedes generar una imagen de aplicación con un runtime Java incluido usando `jpackage` después de compilar su `.jar`:

```bash
jpackage --type app-image --name wallpaper-gui \
  --input gui --main-jar wallpaper-gui.jar \
  --runtime-image /ruta/a/runtime-java-17 --dest gui/dist
```

El runtime se crea por separado con `jlink --add-modules java.base,java.desktop --output /ruta/a/runtime-java-17`. La GUI sigue necesitando `wpd` instalado y una sesión gráfica; su paquete no incluye ni modifica el daemon. Documenta en `gui/README.md` cómo localizar `wpd` en la instalación final.

## Criterios de aceptación

- Build Release y CTest aprobados; `.deb` y `.rpm` inspeccionados y, cuando el entorno esté disponible, instalados en sistemas limpios.
- `wpd status`, `next` y `stop` funcionan desde la misma sesión del servicio; no hay HTTP ni procesos huérfanos.
- Actualizar conserva configuración; desinstalar no borra imágenes ni preferencias del usuario.
- Fallar por dependencia, escritorio o sesión produce un mensaje que identifica qué falta. Cada backend se declara probado **solo** tras el checklist gráfico del [documento de compatibilidad](../docs/compatibility.md).
- Los paquetes tienen versión, contacto real, lista de archivos y dependencias inspeccionables; no contienen secretos ni artefactos de desarrollo.
