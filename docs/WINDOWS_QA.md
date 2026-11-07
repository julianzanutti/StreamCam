# Validación de Windows

La aplicación ofrece dos recorridos separados: USB conecta mediante depuración ADB sin QR; Wi-Fi/red local genera un QR temporal. La selección de calidad y lente se realiza en Android antes de iniciar la cámara.

## Cambios de experiencia

- USB es el modo inicial. El botón principal y la ayuda cambian con el transporte.
- Las conexiones de red pueden actualizarse sin reiniciar la aplicación.
- Preparar la identidad y ejecutar ADB no bloquea el hilo de la interfaz. Cancelar invalida resultados pendientes y finaliza el proceso ADB activo.
- Durante una conexión se fija el transporte seleccionado; no se cambian rutas debajo de una sesión activa.
- Los errores de conexión aparecen junto a los controles, con una acción concreta para reintentar.
- La vista previa se adapta al espacio disponible y conserva la relación de aspecto. No se estira ni impone un tamaño fijo al resto de la ventana.
- Solo se habilitan cancelación, desconexión y revocación cuando tienen efecto. Las pantallas y acciones usan terminología en español.
- La ayuda de OBS explica la fuente StreamCam, la instalación del plugin y la cámara virtual de OBS como funciones distintas.

## Evidencia automatizada

Comprobado durante la revisión de 2026-11-07:

| Prueba | Resultado | Qué demuestra |
| --- | --- | --- |
| StreamCamPairingProbe | Aprobada, salida 0 | Rechazo de token incorrecto, replay, cancelación e inicialización del receptor |
| StreamCamDeviceProofProbe | Aprobada, salida 0 | Firma, alteración de mensaje/firma, persistencia y revocación |
| StreamCamUsbProbe · 480p/720p/1080p | Aprobadas, salida 0 | 35/35 cuadros H.264 por perfil, decodificación y BGRA con Media Foundation |
| Compilación StreamCamDesktop Release | Aprobada tras los cambios de interfaz | Integración de los controles y procesos asíncronos |

Estas pruebas no equivalen a una validación de cámara real. La transmisión Wi-Fi fue confirmada por el usuario antes de esta revisión. La validación USB, bloqueo del teléfono, FPS sostenidos y la integración visual con OBS deben registrarse con evidencia física.

## Revisión visual ejecutada

Se abrió la compilación de Windows y se comprobó el estado inicial USB, la separación de textos y botón principal al elegir Wi-Fi, la sección de teléfonos vinculados plegada y la ayuda de OBS/cámara virtual. Los controles principales y la ayuda quedan visibles en la ventana inicial de 1120×720. Se abrió y cerró la ayuda sin lanzar OBS y se cerró StreamCam normalmente con Alt+F4. No se aceptaron diálogos de confianza ni se inició la cámara durante esta revisión.

Detener video conserva el enlace con el teléfono y restablece el receptor para permitir iniciar otra captura. Cancelar, revocar y desconectar limpian la salida local de OBS para evitar conservar el último fotograma.

## Recorrido de aceptación

1. Abrir sin teléfono: el modo USB y sus instrucciones deben ser claros. Conectar debe fallar sin congelar la ventana y permitir reintentar.
2. Conectar un único Android autorizado: pulsar Conectar por USB debe abrir StreamCam en Android sin mostrar un QR. Confirmar confianza cuando corresponda.
3. Iniciar la cámara: comprobar resolución/FPS y el indicador USB en Windows. Cambiar el tamaño de la ventana y verificar que el video mantiene su proporción.
4. Bloquear el teléfono durante al menos un minuto; el video debe continuar. Desconectar el cable: la vista debe dejar de mostrar video y permitir reconectar.
5. Elegir Wi-Fi/red local: generar QR, cancelar y generar otro; el anterior debe quedar inválido. Al vencer debe ofrecer generar uno nuevo.
6. En OBS agregar la fuente StreamCam y comprobar el video. Iniciar cámara virtual desde OBS y elegir OBS Virtual Camera en una aplicación de prueba, sin iniciar una emisión ni grabación.
7. Cerrar Windows durante preparación, conexión, recepción y desconexión: no deben quedar diálogos ni procesos ADB propios pendientes.
