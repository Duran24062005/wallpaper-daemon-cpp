# Documentación de apoyo

El [README principal](../README.md) es el punto de entrada. Las instrucciones para **escribir código** están en [prds/README.md](../prds/README.md); esta carpeta reúne conceptos y comprobaciones que se reutilizan en varias etapas.

- [Arquitectura](Architecture.md): flujo real, contratos propuestos y límites entre lógica y escritorio.
- [CMake](CMake.md): configuración, compilación y cómo agregar módulos sin comandos `g++` manuales.
- [Pruebas](testing.md): qué verificar en cada hito y qué requiere una sesión gráfica.
- [Diagnóstico](troubleshooting.md): errores de compilación, rutas, sesión e IPC.
- [Compatibilidad](compatibility.md): capacidades por backend y qué significa «no disponible».
- [Procesos externos](process-runner.md): ejecutor C++17 sin shell para los adaptadores.
- [Protocolo local](ipc-protocol.md): mensajes `v1` que comparten daemon, CLI y GUI.

Todas las rutas de comandos parten de la raíz de `wallpaper-daemon-cpp`, salvo donde se indique otra cosa. Los fragmentos de un PRD describen **el estado al terminar ese paso**, no el código que ya está implementado.
