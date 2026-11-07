# StreamCam para Android

Aplicación Kotlin con Camera2, MediaCodec H.264, AudioRecord/PCM para USB y WebRTC para Wi-Fi. Android mínimo 10 (API 29), compile/target SDK 35 y JDK 17. La captura empieza únicamente al pulsar Iniciar y pedir permisos según el modo Solo video, Solo audio o Audio + video. No guarda grabaciones.

## Compilar

Instalá Android SDK 35, Build Tools 35 y Gradle 8.11.1. Configurá `JAVA_HOME`, `ANDROID_HOME` y opcionalmente `GRADLE_USER_HOME` en tu entorno; no publiques `local.properties`.

```powershell
gradle -p android --no-daemon assembleDebug lintDebug
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

El APK debug sirve para desarrollo. Una release pública debe firmarse con la clave del mantenedor, almacenada fuera del repositorio. Nunca subir archivos keystore, contraseñas ni claves privadas. Las dependencias tienen verificaciones SHA-256 en `gradle/verification-metadata.xml`.

USB utiliza depuración ADB y un bootstrap de identidad pública desde Windows, sin QR. Wi-Fi usa el QR de Windows. Ambos mantienen TLS y prueba de posesión de la clave del teléfono. La cámara puede permanecer activa con la pantalla bloqueada mediante un servicio de primer plano y una notificación visible.

El lector QR permite cambiar entre las cámaras que Android expone al escáner y recuerda la elegida. Usa enfoque continuo y reconoce códigos normales e invertidos. La aplicación figura como **StreamCam** en el cajón de aplicaciones; si un launcher la oculta, revisá su configuración de aplicaciones ocultas.

La calidad efectiva depende de los formatos que admita cada lente y del encoder. El bitrate de video se adapta por red; el máximo configurado no garantiza una tasa constante por Wi-Fi. El micrófono WebRTC de Wi-Fi requiere que el dispositivo permita captura de audio en primer plano.
