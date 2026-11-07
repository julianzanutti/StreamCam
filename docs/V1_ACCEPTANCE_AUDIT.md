# Auditoría de cierre v1 · 2026-11-07

Resultado: **incompleta**. No se etiqueta v1 funcional hasta verificar equipos reales.

| Requisito | Evidencia actual | Pendiente |
|---|---|---|
| App Windows usable | Compilada y ventana responde; paquete/hash verificados | QA visual y recorrido de usuario |
| APK Android | Compilado e instalado Success en Moto G32 | Escáner/Keystore/cámara en pairing físico |
| QR | Render Nayuki decodificado por zxing-cpp independiente | Apuntar Moto a QR real |
| Pairing TLS e identidad | Cliente independiente validó pin, firma y aceptación | Mismo handshake desde APK |
| Reconexión/revocación | Desafío firmado y rechazo revocado pasan; store persiste | Flujo físico y UI |
| H264720p30 hasta OBS | Emisor sintético→WebRTC→MF/D3D11 RTX3060→pipe→libobs:900 cuadros en30,046s y píxeles correctos | Moto real, OBS UI, latencia y duración prolongada |
| Desconexión segura | IO cancelable y pruebas de cierre; limpieza de cuadros implementada | Cable/red/cámara reales |
| Wi-Fi | PC y Moto observados en192.168.1.0/24 | Tráfico físico y reconexión |
| USB tethering | Código selecciona interfaz y transporte común | Teléfono reporta sys.usb.config=adb; rndis0 no existe. Activación visible del usuario y prueba de ruta necesaria |
| Instalación | Paquete24 archivos con SHA256; script plugin, APK instalado | Validación manual de la distribución |

La intervención física está solicitada en el chat: escanear QR, confirmar ambos equipos e iniciar video. La aplicación Windows del paquete permanece abierta. No hay un job de compilación pendiente que sustituya esa acción. Las pruebas sintéticas no prueban la cámara real, ni su ruta USB o seguridad de la red doméstica.
