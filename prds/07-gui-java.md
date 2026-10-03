# PRD 07 — Cliente gráfico Java para el CLI

## Propósito y decisión

El [PRD 06](06-daemon-cli-e-ipc.md) deja un contrato JSON local y un ejecutable `wpd`. La GUI debe pedir acciones y mostrar resultados **sin** leer la configuración interna, ejecutar `gsettings`, ni seleccionar imágenes por su cuenta. Para que el ejercicio principal siga siendo C++ y puedas ejecutar el ejemplo sin dependencias Java externas, esta primera GUI usa **Swing del JDK 17** en lugar de JavaFX. Es una aplicación separada bajo `gui/`; no se compila dentro de CMake ni modifica el daemon.

**Actores:** persona que prefiere botones y campos al terminal. **Entrada:** ruta del binario `wpd` y acciones del usuario. **Salida:** estado o error recibido del CLI. **Reglas:** nunca bloquear el hilo de interfaz, pasar cada argumento por separado, mostrar código de salida y salida del proceso, no inferir éxito por el simple hecho de que `wpd` arrancó. **Fuera de alcance:** eventos en tiempo real, administración remota y gestión directa de procesos de vídeo.

## Paso 1 — Crear el módulo separado

Cuando llegues a esta etapa, crea `gui/src/` y `gui/README.md`. En el README de **ese módulo** anota: requiere JDK 17 para compilar, `wpd` del mismo protocolo `v1`, y ejecución con una ruta absoluta a `wpd`. No coloques clases Java en `src/` de C++; el propietario de esa aplicación es `gui/`.

## Paso 2 — Escribir una ventana completa

Crea `gui/src/App.java` con todo este contenido:

```java
import javax.swing.*;
import java.awt.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.TimeUnit;

public final class App extends JFrame {
    private final String wpd;
    private final JTextArea output = new JTextArea(18, 72);
    private final JTextField folder = new JTextField(36);
    private final JTextField interval = new JTextField("60", 6);
    private final JTextField image = new JTextField(36);
    private final JTextField screen = new JTextField(12);
    private boolean busy = false;

    private record Result(int exitCode, String text) {}

    private App(String wpd) {
        super("Wallpaper Daemon — control local");
        this.wpd = wpd;
        setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        setLayout(new BorderLayout(8, 8));

        JPanel controls = new JPanel(new GridLayout(0, 1, 4, 4));
        JPanel actions = new JPanel(new FlowLayout(FlowLayout.LEFT));
        addButton(actions, "Estado", () -> run(wpd, "status"));
        addButton(actions, "Siguiente", () -> run(wpd, "next"));
        addButton(actions, "Pantallas", () -> run(wpd, "list-screens"));
        addButton(actions, "Detener daemon", () -> run(wpd, "stop"));
        addButton(actions, "Iniciar servicio", () ->
                run("systemctl", "--user", "start", "wallpaper-daemon.service"));
        controls.add(actions);

        JPanel paths = new JPanel(new FlowLayout(FlowLayout.LEFT));
        paths.add(new JLabel("Carpeta:")); paths.add(folder);
        addButton(paths, "Guardar carpeta", () -> {
            if (!folder.getText().isBlank()) run(wpd, "config", "folder", folder.getText());
        });
        paths.add(new JLabel("Intervalo (s):")); paths.add(interval);
        addButton(paths, "Guardar intervalo", () ->
                run(wpd, "config", "interval", interval.getText()));
        controls.add(paths);

        JPanel images = new JPanel(new FlowLayout(FlowLayout.LEFT));
        images.add(new JLabel("Imagen/vídeo:")); images.add(image);
        images.add(new JLabel("Pantalla opcional:")); images.add(screen);
        addButton(images, "Aplicar imagen", () -> {
            if (image.getText().isBlank()) return;
            if (screen.getText().isBlank()) run(wpd, "set-image", image.getText());
            else run(wpd, "set-image", image.getText(), screen.getText());
        });
        addButton(images, "Vídeo X11", () -> {
            if (!image.getText().isBlank()) run(wpd, "set-video", image.getText());
        });
        controls.add(images);
        add(controls, BorderLayout.NORTH);

        output.setEditable(false);
        output.setFont(new Font(Font.MONOSPACED, Font.PLAIN, 13));
        add(new JScrollPane(output), BorderLayout.CENTER);
        pack();
        setLocationRelativeTo(null);
    }

    private void addButton(JPanel panel, String label, Runnable action) {
        JButton button = new JButton(label);
        button.addActionListener(event -> action.run());
        panel.add(button);
    }

    private void run(String... args) {
        if (busy) return;
        busy = true;
        output.setText("Ejecutando: " + String.join(" ", args) + "\n");
        new SwingWorker<Result, Void>() {
            @Override protected Result doInBackground() throws Exception {
                List<String> command = new ArrayList<>(Arrays.asList(args));
                Process process = new ProcessBuilder(command).redirectErrorStream(true).start();
                if (!process.waitFor(5, TimeUnit.SECONDS)) {
                    process.destroyForcibly();
                    process.waitFor();
                    return new Result(1, "Tiempo de espera agotado; consulta Estado antes de reintentar");
                }
                String text = new String(process.getInputStream().readAllBytes(),
                                         StandardCharsets.UTF_8);
                return new Result(process.exitValue(), text);
            }
            @Override protected void done() {
                busy = false;
                try {
                    Result result = get();
                    output.setText("Código de salida: " + result.exitCode() + "\n" + result.text());
                } catch (Exception error) {
                    output.setText("No se pudo ejecutar: " + error.getMessage());
                }
            }
        }.execute();
    }

    public static void main(String[] args) {
        if (args.length != 1) {
            System.err.println("usage: java -jar wallpaper-gui.jar /ruta/absoluta/a/wpd");
            System.exit(2);
        }
        String cli = Path.of(args[0]).toAbsolutePath().toString();
        SwingUtilities.invokeLater(() -> new App(cli).setVisible(true));
    }
}
```

`SwingWorker` deja libre el hilo de interfaz mientras `wpd` o `systemctl` responden. `ProcessBuilder` mantiene la carpeta con espacios como **un argumento**. El área de salida muestra el JSON `v1` formateado por `wpd` y su código; la GUI no necesita un segundo parser que pueda divergir del contrato. Un timeout no significa que la acción no ocurrió: por eso el mensaje pide consultar `Estado` antes de repetir.

## Paso 3 — Compilar y ejecutar sin ocultar dependencias

Desde la raíz del repositorio C++:

```bash
javac --release 17 -d gui/out gui/src/App.java
jar --create --file gui/wallpaper-gui.jar --main-class App -C gui/out .
java -jar gui/wallpaper-gui.jar ./build/wpd
```

La carpeta `gui/out/` y el `.jar` son artefactos generados: añádelos al `.gitignore` **cuando implementes la GUI**, junto con el README del módulo. El servicio debe estar corriendo para las acciones de `wpd`; «Iniciar servicio» requiere la unidad del PRD 08 y `systemd --user`. Si aún ejecutas el daemon a mano, abre otra terminal con `./build/wallpaper-daemon` y luego esta ventana.

## Pruebas y aceptación

1. Con el daemon apagado, «Estado» enseña un error de conexión y la ventana sigue respondiendo. «Iniciar servicio» muestra el fallo real si la unidad aún no existe.
2. Con el daemon activo, «Estado», «Siguiente» y «Pantallas» muestran la respuesta `v1`. Una carpeta con espacios se guarda y aparece en `status`.
3. Imagen inválida, monitor no soportado y vídeo fuera de X11 muestran un código no cero y el diagnóstico del daemon.
4. Dos clics rápidos no crean operaciones superpuestas. Cierra la ventana mientras se ejecuta un comando y comprueba que no deja un proceso de reproducción nuevo fuera del daemon.

La GUI es cliente del CLI; para cambiar el formato de `status` o un código de error, actualiza primero [docs/ipc-protocol.md](../docs/ipc-protocol.md) y sus pruebas. El [PRD 08](08-empaquetado-y-distribucion.md) la puede distribuir como aplicación Java separada con runtime incluido.
