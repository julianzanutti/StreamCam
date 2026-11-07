# Probar StreamCam

## USB con depuración (sin QR)

Conectar un teléfono, autorizar la depuración en Android y abrir StreamCam en ambos equipos. En Windows seleccionar USB y pulsar Conectar por USB. Android debe abrirse directamente. Confirmar confianza si se solicita; no escanear ningún QR.

En Android elegir modo Solo video, Solo audio o Audio + video, lente, resolución y bitrate cuando corresponda; pulsar Iniciar. Windows debe mostrar video, resolución y FPS cuando el modo incluye video. En modos con audio, OBS debe recibir la señal de StreamCam en el mezclador. Detener, cambiar modo/calidad y volver a iniciar: debe recuperarse sin repetir la vinculación.

Bloquear el teléfono durante un minuto. El video debe continuar y la notificación debe ofrecer Detener. Desconectar el cable y verificar que Windows deja de mostrar imagen; reconectar y repetir.

## Wi-Fi

Elegir en Windows la interfaz de red compartida con Android y generar el QR. En Android pulsar Conectar por Wi-Fi, escanear y confirmar en ambos equipos. Elegir la calidad e iniciar. Un QR vencido o cancelado debe rechazarse; generar uno nuevo para reintentar.

Observar bitrate efectivo y FPS en Android. Una tasa máxima configurada no implica que WebRTC envíe esa cantidad constantemente: el contenido de la imagen y las condiciones de red influyen. Comparar escenas con movimiento y buena iluminación.

## OBS y cámara virtual

Con OBS cerrado, instalar el plugin con el script incluido. Abrir OBS y agregar la fuente StreamCam. La imagen debe coincidir con la vista previa de Windows. Si no aparece la fuente, comprobar la versión de OBS, reiniciar después de instalar y consultar el registro de carga del plugin, eliminando datos personales antes de compartirlo.

Para otras aplicaciones, pulsar Iniciar cámara virtual en OBS y seleccionar OBS Virtual Camera como cámara de entrada. No hace falta iniciar emisión ni grabación.

Para una llamada, seleccioná OBS Virtual Camera como cámara y el micrófono habitual de tu PC como entrada de audio. StreamCam entrega audio a la fuente de OBS, pero no instala un micrófono virtual de Windows.

## Registrar el resultado

Anotar versiones de las apps, Android, Windows y OBS, lente, resolución, transporte, FPS y duración. No publicar IPs, números de serie, identidades, claves, QR activos ni imágenes personales. Distinguir pruebas sintéticas de pruebas con cámara real.
