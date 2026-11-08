# Estado de StreamCam

Actualizado: 2026-11-08. Release público: 0.3.14-preview. Android versionCode 18 incorpora verificación de releases, descarga APK con SHA-256 obligatorio y entrega al instalador del sistema para confirmación; `assembleDebug` y `lintDebug` pasan. El APK conserva el certificado SHA-256 del APK público 0.2.2 para permitir actualización sobre esa instalación. Windows 0.3.14-preview y OBS se compilaron; el paquete pasó `--check-install`. Android sideload no puede instalar en silencio. Continúan pendientes QA física, audio real, pairing, matriz de dispositivos y sesiones prolongadas; preview, APK debug firmado con la clave de continuidad existente e instalador Windows sin Authenticode.

## Implementado

Android: cámara frontal/trasera, perfiles SD/HD/Full HD filtrados por capacidades de la lente y encoder; bitrate configurable; servicio en primer plano con notificación. Tres modos: solo video, solo audio o ambos; permisos solicitados según la opción. Captura de audio por AudioRecord/PCM USB y pista WebRTC/Opus Wi-Fi. Detener medios conserva la vinculación; desconectar es una acción aparte. UI Material en retrato y paisaje.

El selector de audio enumera entradas del sistema por su ID runtime, refresca la lista si se conecta o retira un dispositivo, y muestra las cápsulas/grupos integrados que Android informa. En USB, `AudioRecord.activeMicrophones` añade el número y descripción de las cápsulas que Android reporta activas tras iniciar la captura. Esto incluye teléfonos con varios micrófonos cuando el fabricante publica ese inventario. En Automático, Android/HAL conserva el control de selección y combinación del arreglo interno; estos reportes no permiten escoger por separado cada cápsula. La captura USB usa la entrada preferida seleccionada cuando Android acepta la ruta; WebRTC aplica la entrada preferida reportada por el dispositivo. Compatibilidad real depende del fabricante y falta la matriz física de teléfonos/rutas.

Windows: USB ADB directo sin QR, Wi-Fi mediante QR, preparación de identidad y comandos ADB asíncronos, cancelación, reconexión, revocación, vista previa adaptable y acceso a instrucciones OBS/cámara virtual.

Transporte: Wi-Fi WebRTC H.264/Opus y USB H.264/PCM por TLS sobre ADB reverse. La fuente OBS recibe audio y video por pipes locales restringidos al usuario. La cámara virtual utilizada es OBS Virtual Camera; no se instala un controlador de cámara propio de StreamCam.

Corrección V2 aplicada en esta revisión: el paquete PCM USB usa una cabecera little-endian de 32 bytes en Android y Windows; se liberan AEC/NS explícitamente. El audio recibido ahora usa una cola acotada a 100 paquetes, en orden, y cuenta descartes por presión en el consumidor. La cola evita sobrescribir continuamente el audio si la interfaz se demora; cuando se llena, descarta el paquete más antiguo. La barra de estado de Windows muestra ahora el tamaño de cola recibida (0–100) y pérdidas acumuladas de cola/envío; falta evaluar visualmente el espacio en pantallas pequeñas y añadir una señal fiable de underrun durante sesiones con pista de audio activa.

En el árbol de desarrollo local, los controles Windows incluyen faders de entrada/salida, compresor con umbral y relación configurables, puerta de ruido y un eco creativo opcional con demora (30–500 ms), retroalimentación acotada y mezcla seco/eco. Se aplican antes de OBS por USB y Wi‑Fi. La puerta silencia audio bajo el umbral; no hace supresión espectral. El eco creativo añade repeticiones; no es cancelación acústica. Android puede enumerar entradas disponibles, elegir una fuente de captura (MIC, cámara/video, reconocimiento, comunicación o sin procesar) y configurar AEC, NS y AGC del sistema. AEC reduce el retorno del parlante al micrófono si el dispositivo lo soporta. La lista se actualiza al conectar o retirar entradas; el arreglo integrado queda a cargo de Android/HAL cuando el OEM no expone cada cápsula por separado. Estos cambios siguen sin validarse con micrófonos reales.

La nueva compilación local de trabajo para V2 incorpora además un código USB aleatorio de seis dígitos por sesión. Windows lo muestra fuera de los extras ADB; Android lo recibe por TLS con certificado fijado y requiere compararlo y marcar una casilla antes de firmar el desafío. La aprobación final con la identidad visible sigue en Windows. No prueba el origen físico del intent exportado y SEC-002 permanece abierto.

Estos cambios no se subieron al release público 0.2.2-preview.

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
| Android assembleDebug + lintDebug después del arreglo de audio USB | Aprobada el 2026-11-07 |
| Android assembleDebug + lintDebug con selección de micrófono y AEC/NS/AGC configurables | Aprobada el 2026-11-07; cambios para el próximo artefacto |
| Compilación del cliente Windows con Qt/WebRTC | Aprobada |
| Compilación Windows tras agregar cola de audio acotada | Aprobada el 2026-11-07; Qt/C++ runtime emitieron advertencias no fatales |
| Compilación Windows con controles de voz y procesamiento antes de OBS | Aprobada el 2026-11-07; aún falta prueba auditiva/calibración real |
| Empaquetado V2 Windows y APK debug local | Paquetes de trabajo conservados bajo `dist/StreamCam-v2-*` y `dist/v2-*`; el más reciente integra workers de audio/video OBS, selector `cameraId`, perfiles por transporte, fallback de encoder y cierre Android asíncrono protegido contra carreras de reconexión. APK debug, hashes y `--check-install`; smoke de instalación/desinstalación Windows bajo G aprobado el 2026-11-07 |
| Probe de voz | Ganancia, compresor, puerta y dos repeticiones de eco atenuadas aprobados el 2026-11-07 con paquetes sintéticos; no evalúa calidad perceptual ni latencia auditiva real |
| Build Windows después de distinguir entradas Android homónimas y quitar límite de una hora en OBS | Aprobado el 2026-11-07 |
| `lintDebug` y APK debug con el ID runtime de entrada para distinguir micrófonos homónimos | Aprobado el 2026-11-07 |
| `assembleDebug lintDebug` con refresco automático de entradas de audio al conectar o retirar dispositivos | Aprobado el 2026-11-07; callbacks se registran en primer plano y se liberan al salir |
| `assembleDebug lintDebug` con inventario de cápsulas/grupos que reporta `AudioManager.microphones` | Aprobado el 2026-11-07; la API es informativa y tolera implementaciones OEM que no reportan el inventario |
| `assembleDebug lintDebug` con reporte de cápsulas activas de `AudioRecord.activeMicrophones` en USB | Aprobado el 2026-11-07; queda por observar su contenido en micrófonos reales/OEM |
| `assembleDebug lintDebug` manteniendo cápsulas activas visibles junto a estadísticas del encoder durante USB A/V | Aprobado el 2026-11-07 |
| Compilación DLL de OBS contra las exportaciones del OBS instalado | Aprobada el 2026-11-07 |
| Probe de reloj OBS: sesión >1 h, duplicado, regresión y discontinuidad | Aprobado el 2026-11-07 |
| Probe de cola audio: límite, orden, overflow y drenado parcial | Aprobado el 2026-11-07 |
| CTest de escritorio y OBS | 2/2 (procesamiento de voz, cola de audio) + 1/1 (reloj OBS) aprobados el 2026-11-07 |
| CTest de escritorio tras separar el control AEC y añadir vencimiento/reintento de ACK | 8/8 en build principal y CMake aislado de CI el 2026-11-07; cubre tracker y probes previos, no pérdida real de ACK en WebSocket |
| Regresión de pairing: callback viejo de A durante B pendiente y cierre del tercer socket al aceptar B | Aprobada el 2026-11-07 contra AuthProbe por WSS loopback con identidades RSA firmadas; cubre los estados del servidor, no el diálogo visual de Qt |
| Regresión WSS del código de verificación USB | Aprobada el 2026-11-07: challenge contiene código de seis dígitos; misma prueba valida consentimiento atrasado y cierre del socket sobrante |
| Build Android después de ajustar aviso del bootstrap USB | `assembleDebug lintDebug` aprobado offline el 2026-11-07 con SDK/JDK/Gradle bajo G; permanecen avisos no fatales de SDK XML y ZXing deprecado |
| Compilación de la fuente OBS con audio/video | Aprobada |
| Paquete e instalador local con el código USB visible | Build Windows y OBS, APK debug + lint, manifiestos SHA256 y `--check-install` aprobados; setup instalado en `G:\programIAs\dist\v2-usb-local-smoke`, comprobación de la app instalada con exit code 0 y desinstalación con exit code 0. Instalador no firmado y APK con certificado Android Debug. |
| Script de identidad descargado bajo AllSigned: bloqueo reproducido y corrección verificada | Aprobada |
| Instalación Windows limpia y reinstalación en ruta con espacios | Aprobada |
| Smoke del instalador 0.3.0-preview: instalación, reinstalación, `--check-install`, PATH mínimo y `AllSigned`; desinstalación y limpieza | Aprobado el 2026-11-07 en `G:\programIAs\.tooling`; sin entrada ni carpeta residual |
| Smoke del setup 0.3.1-preview | Empaquetado, `--check-install`, versión/hash del setup y hash del APK verificados; instalación de prueba no ejecutada porque la clave del mismo AppId ya existe y sus valores no son legibles en el contexto actual |
| Paquete integrado V2 0.3.3-preview | Windows y APK Android (versionCode 7) compilados; `lintDebug`, `--check-install`, hashes SHA-256 y versiones de ambos paquetes aprobados. No repetir el smoke de instalación porque ya existe una clave del mismo AppId no inspeccionable en este contexto. APK debug, setup sin Authenticode y QA física pendiente. |
| Paquete integrado V2 0.3.4-preview | Windows y APK Android (versionCode 8) compilados; `lintDebug`, `--check-install`, hashes SHA-256 y versiones de ambos paquetes aprobados. Incluye mute persistente y medidor pico dBFS hacia OBS; prueba sintética de mute/eco pasa. APK debug, setup sin Authenticode y QA física pendiente. |
| Inspección visual de la ventana Windows 0.3.4 | No completada: ya había una instancia StreamCam activa y el manejador de ventanas perdió la ventana temporal de prueba. No se cerró la sesión activa del usuario. La compilación, auto chequeo empaquetado y probes sí pasan; queda captura visual/manual tras cerrar normalmente la instancia previa. |
| Sincronía A/V | Mapper común OBS probado 2/2; conserva offset sintético de captura y timestamps USB/WebRTC. Falta verificar desfase, deriva y latencia con móviles y OBS reales, incluidas las rutas Android con varios micrófonos y las fuentes de timestamp Camera2 por OEM. |
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

El arreglo del contrato/cola no equivale a probar un micrófono real. No había dispositivo conectado al consultar ADB en esta revisión; el APK nuevo no se instaló en un teléfono. El escritorio de desarrollo está pendiente de probar el flujo visual de comparación y consentimiento con un Android real y Windows.

## Publicación

Licencia GPL-3.0-or-later, documentación de instalación/uso y guía de errores incluidas. La auditoría busca claves, tokens, rutas personales y artefactos generados sin imprimir valores sensibles. Las notas privadas y diagnósticos de dispositivos quedan fuera. El APK preview es debug y no se debe presentar como firma estable de producción; los paquetes preview locales no están firmados con identidad de editor.

| Paquete integrado V2 0.3.5-preview | Windows y APK Android (versionCode 9) compilados; Android `assembleDebug lintDebug testDebugUnitTest` aprobado (4/4), CTest escritorio 8/8, OBS 2/2 y probes aislados 9/9 incluido WSS loopback. `--check-install` y manifiestos SHA-256 del paquete (705 archivos) y setup/APK aprobados. APK debug y setup sin Authenticode; no instalados en teléfono por falta de dispositivo conectado; QA física sigue pendiente. |
| Iteración V2 0.3.6-preview | Se agrupan lecturas parciales de AudioRecord en paquetes USB PCM16 exactos de 20 ms, incluso si el límite de lectura cae entre bytes; remuestreo conserva bloques regulares. Android build/lint y 6 pruebas unitarias pasan. Nuevo APK versionCode 10. Prueba física sigue pendiente. |
| Paquete integrado V2 0.3.6-preview | Windows y APK Android (versionCode 10) empaquetados y versionados; Android `assembleDebug lintDebug testDebugUnitTest` 6/6, escritorio 8/8, OBS 2/2 y probes aislados 9/9 (incluido WSS loopback). `--check-install` y los manifiestos SHA-256 del paquete (705 archivos) y setup/APK pasan. Setup sin Authenticode, APK debug; no se publica ni se probó con hardware físico. |
| Paquete integrado V2 0.3.7-preview | Windows y APK Android (versionCode 11) compilados; Android build/lint y 6/6 unitarias, escritorio 8/8, OBS 2/2 y probes aislados 9/9. La barra de estado Windows muestra backlog (0–100) y pérdidas de audio. `--check-install` y hashes revisados. APK debug y setup sin Authenticode; QA física pendiente. |
| Paquete integrado V2 0.3.8-preview | Windows y APK Android (versionCode 12); escritorio 9/9 incluyendo señal de underrun (sin pista, timeout, recuperación y reset); plugin OBS 2/2; Android build/lint y 6/6 unitarias. Smoke Inno aislado pasó instalación, reinstalación, `--check-install`, ruta con espacios y PATH mínimo bajo `AllSigned`; se usó AppId propio y luego se desinstaló/verificó la limpieza. `--check-install` y SHA-256 aprobados para 705 archivos y cuatro artefactos de release. DLL OBS del paquete coincide por SHA-256 con el build. APK debug, setup sin Authenticode y QA física pendiente. |
| Paquete integrado V2 0.3.9-preview | Windows y APK Android (versionCode 13). Android build/lint y 13/13 unitarias; escritorio 9/9, OBS 2/2, `--check-install`, 705 hashes del paquete y cuatro hashes release. Siete pruebas cubren control-v2 iniciado por ambos lados, inicio simultáneo consistente/conflictivo, capabilities, duplicados y reset. APK debug, setup sin Authenticode, no publicado; falta QA física. |

| Iteración local V2 0.3.10-preview | Windows y Android versionCode 14; handshake control-v2 bilateral/simultáneo en Android y Windows con probe nativo dedicado. Android build/lint y 13 pruebas, escritorio 10/10, CMake aislado 11/11, OBS 2/2. Falta empaquetado/smoke de esta versión, dispositivo Android conectado y QA física de transporte/micrófonos. APK debug; no publicado. |

| Paquete integrado V2 0.3.11-preview | Windows y APK Android (versionCode 15) empaquetados; Android `assembleDebug lintDebug testDebugUnitTest` 19/19, CTest escritorio 10/10, probes aislados 11/11 y OBS 2/2. Manifiestos SHA-256 verificados (705 archivos del paquete y cuatro artefactos). Smoke elevado pasó instalación, reinstalación, `--check-install`, PATH mínimo/AllSigned y desinstalación con limpieza. El APK instalado desde cero abre en AVD Android 35 y `PairingActivity` queda al frente sin excepción. APK debug, setup sin Authenticode, no publicado; falta pairing real y QA física de audio, transportes y OBS. |
