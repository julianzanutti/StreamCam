# Solución de problemas

## USB no encuentra el teléfono

- Usa un cable de datos; algunos cables solo cargan. Prueba otro puerto USB y desbloquea el teléfono.
- Asegúrate de que **Depuración USB** está activada y acepta el diálogo de autorización. Desconecta y conecta de nuevo después de revocar autorizaciones ADB.
- Cierra Android Studio u otra app que esté usando ADB y vuelve a abrir StreamCam Windows. Conecta un solo teléfono durante el emparejamiento.
- Si Windows dice *unauthorized*, mira la pantalla Android y acepta el diálogo RSA. Si dice *device not found*, revisa cable, puerto y controladores del fabricante.
- Desconecta y vuelve a conectar desde Windows para crear una ruta ADB nueva. No es necesario escanear un QR por USB.

## El QR no conecta o venció

- Genera un QR nuevo: vence a los dos minutos y no se puede reutilizar después de aceptar.
- Confirma que PC y teléfono estén en la misma red y no conectados a una red de invitados que aísle dispositivos.
- Permite StreamCam en el Firewall de Windows para redes privadas. Evita redes públicas compartidas.
- Comprueba que la fecha y hora de ambos equipos sean correctas. Si falla la identidad TLS, cancela e inicia una invitación nueva desde la PC.
- Desactiva temporalmente VPNs que impidan la comunicación local y elige en Windows la interfaz Wi‑Fi que comparte la red con el teléfono.

## La cámara no muestra imagen

- Confirma que el modo incluya video y que concediste el permiso de cámara a StreamCam.
- Cierra otras apps que usen la cámara. Detén y vuelve a iniciar la sesión.
- Cambia entre cámara frontal y trasera o baja la resolución a HD/SD; no todas las lentes admiten todos los formatos.
- Mantén StreamCam abierto unos segundos y revisa el mensaje de estado en Android. Comprueba que Windows muestra la conexión y los FPS.
- Si al bloquear la pantalla deja de capturar en un teléfono específico, prueba con la pantalla encendida y desactiva la optimización de batería para StreamCam.

## No se escucha el micrófono

- Selecciona **Solo audio** o **Audio + video**; **Solo video** nunca captura el micrófono.
- Acepta el permiso de micrófono. En Android, ve a **Ajustes → Apps → StreamCam → Permisos → Micrófono** y permite su uso mientras se usa la app.
- Cierra grabadoras, llamadas, asistentes de voz u otras aplicaciones que estén usando el micrófono; luego detén e inicia la transmisión.
- En OBS comprueba el mezclador: que la fuente **StreamCam** no esté silenciada, que el volumen no esté en cero y que no tenga filtros que eliminen audio. En propiedades avanzadas confirma el monitoreo/ruteo requerido por tu configuración.
- En OBS Virtual Camera, la imagen sale como cámara; usá el micrófono habitual de la PC en la videollamada. StreamCam entrega audio a OBS, pero no instala un micrófono virtual de Windows.
- USB usa PCM mono de 48 kHz; Wi‑Fi usa audio WebRTC/Opus. Si USB se entrecorta, cierra otras apps de audio y vuelve a conectar el cable.

## No aparece la fuente StreamCam en OBS

- Cierra OBS, ejecuta el instalador incluido y vuelve a abrir OBS. El plugin requiere OBS Studio de 64 bits compatible con la versión para la que se preparó el paquete.
- Comprueba en OBS **Ayuda → Archivos de registro → Ver registro actual** si `streamcam-source` se cargó. Cierra OBS antes de reinstalar.
- Asegúrate de haber extraído el ZIP completo y que el archivo del plugin no haya sido movido ni bloqueado por Windows.
- No confundas la fuente **StreamCam** con **OBS Virtual Camera**: primero agrega la fuente, después inicia la cámara virtual.

## La app no abre o Windows la bloquea

- Extrae el ZIP en una carpeta normal y vuelve a ejecutar `StreamCam.exe`; no lo ejecutes dentro del ZIP.
- Si Windows SmartScreen bloquea el programa, revisa que se haya descargado desde la release oficial y su hash publicado. No desactives el antivirus.
- Si falta una DLL o el plugin no carga, reextrae el paquete completo. No descargues DLLs sueltas de sitios externos.

## Windows no puede preparar la identidad de esta PC

- Usa el ZIP completo y extrae todos sus archivos a una carpeta normal; no ejecutes solo `StreamCam.exe` ni lo abras desde la vista previa del ZIP.
- La identidad se crea al conectar y se guarda en los datos locales de tu usuario de Windows. No copies una identidad desde otra PC ni borres esos archivos mientras haya teléfonos vinculados.
- Si el mensaje menciona PowerShell o acceso a la carpeta, reinicia StreamCam y verifica que tu usuario pueda escribir en `%LOCALAPPDATA%\StreamCam\StreamCam`. Si sigue fallando, anota el detalle que aparece en la app y compártelo sin enviar archivos de identidad.

## Reinicio seguro

Pulsa **Detener** en Android, cierra StreamCam Windows y vuelve a abrirla. Si USB quedó trabado, desconecta el cable, termina la app Windows desde el Administrador de tareas, conecta de nuevo y repite el emparejamiento. Si el problema sigue, abre una incidencia con versiones de Android, Windows, OBS y StreamCam, modo, transporte y texto exacto del error. No compartas QR vigentes, claves, seriales, IPs públicas, capturas con imágenes privadas ni archivos de identidad.
