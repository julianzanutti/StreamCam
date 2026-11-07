# Validación Android

## Cambios de interfaz y ciclo de vida

- USB se inicia desde Windows y no utiliza QR. El lector se presenta como **Conectar por Wi-Fi**.
- Los ajustes de lente, resolución y bitrate se conservan. Los formatos se filtran por las capacidades de la cámara y del encoder hardware antes de iniciar.
- El perfil de ahorro utiliza 640×480 (4:3); HD y Full HD utilizan 16:9, a 30 fps.
- **Detener cámara** permite cambiar ajustes y reiniciar sin volver a vincular. **Desconectar PC** termina la conexión.
- El diagnóstico y el olvido de la PC quedan deshabilitados durante la captura para evitar competencia por la cámara.
- La pantalla se adapta a orientación horizontal con dos tarjetas y conserva la sesión durante la recreación de la actividad.
- Android 13 o posterior solicita el permiso de notificaciones al iniciar por primera vez, para poder mostrar la acción de detener. Denegarlo no impide iniciar el servicio de cámara.

## Verificaciones realizadas

- XML de retrato y paisaje válido; ambos tienen los mismos 12 identificadores, sin duplicados.
- Estilos y fondo de campos XML válidos.
- Revisión de los callbacks Camera2/MediaCodec: fallos de configuración o captura se envían al controlador para detener y liberar la sesión; se evitan excepciones sin capturar en createCaptureSession.
- Selección del encoder limitada a hardware H.264, entrada Surface y soporte de tamaño, FPS y bitrate. CBR se usa solamente si está anunciado; VBR es la alternativa.

## Prueba física de aceptación pendiente

1. Abrir Android sin enlace: no debe activar cámara; USB y Wi-Fi deben quedar diferenciados.
2. Elegir USB en Windows: abrir Android y confirmar solo el primer vínculo, sin QR.
3. Comprobar lente trasera y frontal a 720p, detener y cambiar a 1080p o 480p según lo anunciado.
4. Confirmar que la cámara deje de enviar al detener y que reinicie sin vincular de nuevo.
5. Rotar el teléfono, abrir la app desde la notificación y volver atrás: la captura activa debe continuar.
6. Bloquear durante al menos un minuto y comprobar recepción y FPS en Windows; detener desde la notificación.
7. Desconectar el cable, reconectar y cancelar el vínculo: no deben quedar cámara, encoder ni servicio activos.
8. Repetir por Wi-Fi y comprobar la lente frontal, bitrate real y errores de negociación sin cierre del proceso.

La compilación integrada y los resultados de las pruebas físicas deben registrarse después de instalar la versión que incorpora todos estos cambios. Este documento no declara aprobadas pruebas que todavía no se realizaron.
