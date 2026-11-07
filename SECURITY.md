# Seguridad y privacidad

No publiques vulnerabilidades con tokens, certificados privados, datos de dispositivos o capturas de QR activos. Antes de abrir un informe eliminá IPs personales, números de serie, nombres de usuario y contenido de cámara. Nunca adjuntes almacenes de claves ni los archivos de identidad o dispositivos confiables.

StreamCam genera identidades por instalación. El QR Wi-Fi contiene una invitación de corta duración y la huella pública de la PC. USB usa el canal ADB autorizado para entregar solo la identidad pública, y luego verifica TLS y un desafío firmado del teléfono. La primera conexión requiere confirmación del usuario. Los dispositivos pueden revocarse desde Windows.

La depuración USB permite acceso amplio al teléfono: autorizá únicamente tu computadora y revocá esa autorización desde Android cuando ya no la necesites.

No se emplean servicios externos para transportar el video, no se usa micrófono y no se guardan grabaciones. OBS y las aplicaciones que reciban su cámara virtual tienen sus propias opciones de grabación y transmisión.

Este proyecto está en desarrollo; no hay una versión pública con auditoría independiente de seguridad.
