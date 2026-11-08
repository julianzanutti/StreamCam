# Seguridad y privacidad

No publiques vulnerabilidades con tokens, certificados privados, datos de dispositivos o capturas de QR activos. Antes de abrir un informe eliminá IPs personales, números de serie, nombres de usuario y contenido de cámara. Nunca adjuntes almacenes de claves ni los archivos de identidad o dispositivos confiables.

StreamCam genera identidades por instalación. El QR Wi-Fi contiene una invitación de corta duración y la huella pública de la PC. USB usa el canal ADB autorizado para entregar solo la identidad pública, y luego verifica TLS y un desafío firmado del teléfono. La primera conexión requiere confirmación del usuario. Los dispositivos pueden revocarse desde Windows.

La depuración USB permite acceso amplio al teléfono: autorizá únicamente tu computadora y revocá esa autorización desde Android cuando ya no la necesites.

No se emplean servicios externos para transportar audio o video y StreamCam no guarda grabaciones. El micrófono se utiliza únicamente en los modos Solo audio y Audio + video, con permiso del usuario; en Solo video está desactivado. OBS y las aplicaciones receptoras tienen sus propias opciones de grabación y transmisión.

Este proyecto está en desarrollo; no hay una versión pública con auditoría independiente de seguridad.

La preview distribuye un APK de depuración; no representa una firma Android de producción. Los artefactos Windows locales 0.2.2-preview revisados no tienen firma Authenticode. Los avisos del sistema deben distinguirse de una detección concreta de amenaza; no desactives Play Protect ni Defender para resolverlos. Una firma identifica al editor y protege la integridad, pero no certifica que el programa esté libre de vulnerabilidades.

Consulta la [revisión de seguridad para V2](docs/SECURITY_REVIEW_V2.md) para conocer alcance, hallazgos abiertos y controles requeridos antes de una publicación estable.
