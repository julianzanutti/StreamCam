# Estado de StreamCam

Actualizado: 2026-11-07. Versión Windows 0.2.0-preview con instalador por usuario y dependencias incluidas. El audio requiere validación física en OBS antes de considerarse estable.

## Implementado

Android: cámara frontal/trasera, perfiles SD/HD/Full HD filtrados por capacidades de la lente y encoder; bitrate configurable; servicio en primer plano con notificación. Tres modos: solo video, solo audio o ambos; permisos solicitados según la opción. Captura de audio por AudioRecord/PCM USB y pista WebRTC/Opus Wi-Fi. Detener medios conserva la vinculación; desconectar es una acción aparte. UI Material en retrato y paisaje.

Windows: USB ADB directo sin QR, Wi-Fi mediante QR, preparación de identidad y comandos ADB asíncronos, cancelación, reconexión, revocación, vista previa adaptable y acceso a instrucciones OBS/cámara virtual.

Transporte: Wi-Fi WebRTC H.264/Opus y USB H.264/PCM por TLS sobre ADB reverse. La fuente OBS recibe audio y video por pipes locales restringidos al usuario. La cámara virtual utilizada es OBS Virtual Camera; no se instala un controlador de cámara propio de StreamCam.

## Evidencia

| Prueba | Resultado |
| --- | --- |
| Transmisión real Android → Windows por Wi-Fi | Confirmada por el usuario antes de esta ampliación |
| Autenticación Wi-Fi, reconexión firmada y revocación | Aprobada |
| Autenticación USB sin QR, desafío firmado y revocación | Aprobada |
| Video sintético 720p WebRTC → Media Foundation → fuente OBS | 900 cuadros en 30,097 s; aprobada |
| Video sintético 1080p WebRTC → Media Foundation | 900 cuadros en 30,579 s; aprobada |
| Decodificación de unidades H.264 USB en SD/HD/Full HD | Aprobada |
| Compilación APK Android debug | Aprobada |
| Compilación del cliente Windows con Qt/WebRTC | Aprobada |
| Compilación de la fuente OBS con audio/video | Aprobada |
| Script de identidad descargado bajo AllSigned: bloqueo reproducido y corrección verificada | Aprobada |
| Instalación Windows limpia y reinstalación en ruta con espacios | Aprobada |
| Identidad temporal, recarga y arranque ADB desde la instalación | Aprobada |
| Arranque comprobado con PATH mínimo y script marcado como descargado bajo AllSigned | Aprobada |
| Interfaz instalada, ayuda OBS y cierre normal | Aprobada |
| UI Android retrato/paisaje y selección de modo | Revisada en recursos/código; comprobación física pendiente |
| Audio USB y audio Wi-Fi en micrófono real hacia OBS | Pendiente |
| Audio y video simultáneos, fuente y medidor OBS reales | Pendiente |
| USB con cámara real en pantalla bloqueada | Cámara física a 720p confirmada; bloqueo pendiente |
| Cambio de lente/calidad, detener y reiniciar con cámara real | Pendiente |
| Fuente y cámara virtual en la interfaz real de OBS | Pendiente |
| Latencia, desconexiones y sesión prolongada | Pendiente |

## Publicación

Licencia GPL-3.0-or-later, documentación de instalación/uso y guía de errores incluidas. La auditoría busca claves, tokens, rutas personales y artefactos generados sin imprimir valores sensibles. Las notas privadas y diagnósticos de dispositivos quedan fuera. El APK público es debug y no se debe presentar como firma estable de producción; los paquetes locales se identifican como preview.
