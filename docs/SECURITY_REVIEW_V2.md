# StreamCam · Revisión de seguridad para V2

Fecha: 2026-11-07. Código base: `e1a646d`. Revisión del coordinador y tres subagentes de Android, Windows/OBS y protocolo/publicación. Hallazgos abiertos: este trabajo no implementa correcciones de código ni publica una release.

## Alcance y límites

Se revisaron código de autenticación, QR y USB, permisos y componentes Android, claves/almacenamiento, sockets, parsers multimedia, IPC/OBS, procesos externos, instaladores, actualizaciones, CI y auditoría de secretos. Se verificaron firmas de los artefactos locales 0.2.2-preview. El análisis es estático salvo las verificaciones de artefactos, hashes y el auditor de fuente.

No se ejecutaron ataques contra el teléfono o la red, no se subieron APK/binarios a terceros y no se hizo una prueba de penetración independiente, un análisis antivirus, una revisión exhaustiva de CVE/transitivas o un escaneo completo del equipo del usuario. No se puede certificar ausencia de malware ni vulnerabilidades. La ausencia de un exploit demostrado se indica por separado de la evidencia de código.

## Avisos observados al instalar

El usuario informó avisos tanto en Android como Windows y no recuerda su texto. No se puede clasificarlos como simple falta de reputación, bloqueo por política o detección de amenaza concreta.

Comprobaciones locales realizadas:

| Artefacto | Herramienta y resultado |
|---|---|
| `dist/release-0.2.2-preview/StreamCam-0.2.2-preview-windows-x64-setup.exe` | `Get-AuthenticodeSignature`: **NotSigned**. |
| `dist/StreamCam-preview/StreamCam.exe` | `Get-AuthenticodeSignature`: **NotSigned**. |
| `dist/release-0.2.2-preview/StreamCam-0.2.2-preview-android-preview.apk` | `apksigner verify --print-certs`: verificación exitosa; certificado **CN=Android Debug, O=Android, C=US**. |

El manifiesto Android debug generado también contiene `android:debuggable="true"`. La firma debug sigue siendo una firma criptográfica de APK; no debe describirse como APK sin firma. La comprobación identifica el firmante, no acredita seguridad del contenido ni demuestra que el archivo coincida con el instalado por el usuario.

Microsoft explica que SmartScreen evalúa reputación del archivo/editor y que incluso una firma válida puede mostrar avisos mientras no haya reputación suficiente: [documentación oficial](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation). Google distingue recomendaciones de escaneo de apps desconocidas, bloqueos y detecciones de apps dañinas: [guía oficial de avisos Play Protect](https://developers.google.com/android/play-protect/warning-dev-guidance).

Acción de producto: producir builds release no depurables, identidad de editor estable, firma Android de producción, Authenticode para instalador y binarios propios Windows, timestamp y verificación en CI. Mantener claves fuera del repositorio. Ensayar migración desde APK debug: cambiar de firmante puede impedir actualización directa. No resolver avisos desactivando protecciones ni prometer que la firma elimina todos los avisos. Si vuelve a aparecer un nombre concreto de amenaza, registrar mensaje y hash, detener esa distribución y analizar esa muestra antes de clasificarla como falso positivo.

## Modelo de amenazas

Activos: cámara/micrófono en vivo, consentimiento del usuario, identidad privada de PC/teléfono, confianza de dispositivos, integridad de medios entregados a OBS y paquetes de instalación.

Fronteras: app local Android → activity exportada; dispositivo LAN → listener de pairing; teléfono autorizado → parsers/control/decoder; proceso local Windows → named pipes; entorno de build → paquetes publicados; repositorio de releases → navegador/descarga.

Adversarios considerados: equipo LAN sin confianza; teléfono autorizado malicioso; app Android local instalada; proceso local Windows del mismo usuario; alteración accidental o maliciosa del entorno de compilación. Un administrador/root o malware con control completo del usuario supera varias protecciones del sistema: no se presenta DPAPI ni ACL por usuario como aislamiento frente a todo proceso de esa misma cuenta.

## Hallazgos priorizados

Severidad contextual, sin puntuación CVSS calculada. P1 bloquea publicación estable hasta resolver o justificar con evidencia; P2 requiere endurecimiento y validación. Las brechas de firma/CI son controles de publicación, no detecciones de malware.

### SEC-001 · P1 / alta: consentimiento no vinculado al intento

**Evidencia original:** `desktop/app/PairingServer.cpp` eliminaba `pending_` al desconectarse A. `desktop/app/main.cpp` conservaba el diálogo y respondía mediante `confirm(bool)` global, que aplicaba la respuesta al socket pendiente en ese momento.

**Escenario:** A llega a confirmación, se desconecta y B completa su handshake antes de que se resuelva el diálogo A. Aceptar el diálogo antiguo puede autorizar B. B necesita acceso al listener y una prueba válida de su propia identidad. En Wi-Fi un dispositivo previamente confiable puede usar reconexión; una identidad nueva normalmente necesita invitación válida. No se demostró bypass de TLS, firma o captura silenciosa.

**Corrección local implementada:** `confirm(attemptId, accepted)` rechaza identificadores vencidos y sockets desconectados; disconnect, stop/cancelación y expiración invalidan el intento y notifican a la UI para cerrar el diálogo correspondiente. El consentimiento muestra la huella de identidad disponible y si es vinculación o reconexión. Un evento de UI atrasado queda sin efecto sobre intentos posteriores.

**Validación:** `desktop/check-stale-consent.py` pasó contra el AuthProbe local por WSS en loopback. A completó la prueba firmada y se desconectó; B quedó pendiente; la respuesta atrasada de A se ejecutó mientras B seguía conectado y fue rechazada; luego se aceptó B mediante su intento actual. Esto verifica la carrera principal. La UI Qt real, expiración y revocación todavía requieren regresiones separadas.

### SEC-002 · P1 / media: bootstrap USB local sin procedencia autenticada

**Evidencia:** `android/app/src/main/AndroidManifest.xml:27` exporta PairingActivity. `PairingActivity.kt:137-149` acepta intent explícito USB_CONNECT con pin/id suministrados por el emisor, y describe al interlocutor como PC conectada por cable. El endpoint es localhost fijo.

**Escenario:** otra app instalada con INTERNET puede ofrecer un servidor WSS local, lanzar ese intent con su pin y pedir confianza bajo un texto que afirma origen PC/USB no verificado. Requiere puerto disponible, interacción del usuario para vincular y luego iniciar medios, y permisos de captura. No es ataque remoto ni grabación silenciosa. Exportar el launcher no es por sí mismo la vulnerabilidad; el problema es confiar en la procedencia de extras no autenticados.

**Mitigación implementada:** Android advierte que el intent exportado no prueba quién lo inició. Windows genera un código aleatorio de seis dígitos por sesión y no lo incluye en los extras ADB; Android lo recibe por TLS con el certificado de la PC fijado y obliga a escribir el código visible en Windows. Sólo la coincidencia exacta habilita el envío de la prueba firmada. El server mantiene vivo ese challenge 60 segundos, elimina su estado al recibir la prueba y cierra la conexión si vence; las conexiones sin challenge conservan el timeout corto de 10 segundos. Windows mantiene la aprobación final con identidad visible. El intent todavía puede ser lanzado por otra app y mostrar una advertencia, pero los extras por sí solos ya no permiten vincular un servidor distinto sin que la persona compare el código fuera de banda.

**Validación local:** Android 35 AVD ejecutó `scripts/check-usb-verification-ui.ps1`, que instala el APK debug en un serial `emulator-*`, lanza el intent USB externo, comprueba la advertencia de emisor no verificable, introduce un código incorrecto (botón deshabilitado y sin pairing) y después el exacto (prueba firmada recibida y aceptada por AuthProbe). El script elimina el `adb reverse`, el identity temporal y el pairing de prueba, conservando preferencias de audio/video. Pasaron `assembleDebug`, `lintDebug` y las pruebas Kotlin. La prueba usa un AVD y un peer de loopback controlado; no sustituye ensayo físico en más de un OEM ni prueba una app adversarial instalada. SEC-002 queda mitigado en el flujo local y conserva QA física antes de anunciar compatibilidad amplia.

### SEC-003 · P2 / media: sockets sobrantes conservan estado de pairing

**Evidencia original:** `PairingServer.cpp` cerraba el listener y detenía expiración, pero solo retiraba el socket elegido; los demás conservaban handlers.

**Impacto/precondición:** conexión previa concurrente y handshake válido; puede aparecer otro intento tras aceptar uno. Amplía el escenario de consentimiento obsoleto, sin demostrar autorización automática por sí solo.

**Corrección y validación:** al aceptar un socket se desconectan handlers y se cierran/eliminan los demás. La misma prueba abre un tercer socket firmado pero inactivo mientras B está pendiente; al aceptar B, el cliente observa el cierre de ese socket. La sonda pasó. Quedan otros escenarios de carga/rate limit fuera de esta regresión.

### SEC-004 · P2 / media, defensa local: productor IPC sin autenticación

**Evidencia:** OBS conecta a nombres constantes en `obs-plugin/streamcam-source.cpp:54,77` sin comprobar identidad del productor.

**Escenario:** un proceso local que registre primero esos nombres puede entregar medios falsos a OBS. El caso entre usuarios depende del namespace/ACL y requiere prueba; no se afirma elevación de privilegios ni ejecución de comandos. La ACL del pipe legítimo protege clientes permitidos, pero no acredita que el servidor al que conecta OBS sea StreamCam.

**Corrección/prueba:** canal ligado a instancia/sesión, verificación de proceso/identidad y handshake autenticado con secreto protegido. No basta aleatorizar un nombre. Probar servidor impostor, colisión, usuarios diferentes y comportamiento remoto SMB. Mantener SECURITY_IDENTIFICATION, que limita impersonación del cliente.

### SEC-005 · P2 / baja: límites incompletos de disponibilidad

**Evidencia:** `desktop/ipc/FramePublisher.h:17-18` y `AudioPublisher.h:15-16` admiten ocho clientes, no consumen/rechazan entrada ni fijan read buffer. `PairingServer.cpp:19,26-28` permite cuatro clientes con 10 s de timeout pero sin presupuesto de intentos por origen/global.

**Escenario:** proceso local permitido ocupa slots/envía entrada inesperada; equipo LAN ocupa repetidamente pairing mientras está abierto. El crecimiento efectivo de memoria y coste criptográfico no se midieron.

**Corrección/prueba:** límites de buffer y entrada, handshake/timeout, desconexión de clientes inválidos, rate limit con presupuesto global y por origen, métricas redactadas; cargas controladas deben dejar UI/cancelación operativas.

### SEC-006 · P1 / control de distribución: firma no preparada para estable

Artefactos comprobados arriba, `desktop/package.ps1:10`, Gradle sin firma release y `desktop/installer/StreamCam.iss` sin SignTool. Preparar firma/custodia/renovación, migración debug, comprobación del editor esperado y build release no depurable. No confundir integridad SHA-256 con autenticidad del editor. Verificar firma también después de generar instalador.

### SEC-007 · P1 / cadena de suministro: procedencia nativa no exigida

**Evidencia:** `desktop/package.ps1:17,30` selecciona PowerShell de PATH y copia recursivamente su instalación; ADB procede del SDK local. `desktop/media/CMakeLists.txt:1-4` etiqueta WebRTC como pinned, pero solo exige que exista `webrtc.lib`.

**Precondición:** entorno de build alterado o herramienta actualizada inesperadamente; no se encontró compromiso. Los hashes generados después de empaquetar no prueban procedencia.

**Corrección/prueba:** inventario bloqueado de versiones, origen y SHA/firma upstream; allowlist de runtime; staging limpio; SBOM con transitivas y binarios; análisis de vulnerabilidades reproducible; abortar ante componente distinto o archivo extra. Conservar verificación SHA-256 de Gradle ya existente.

### SEC-008 · P2 / control de auditoría: scanner limitado

`scripts/audit-public.py` revisa árbol Git visible y tres familias de patrones; no historial ni contenido ignorado del paquete. La alternativa PowerShell omite archivos cuya lectura falla. Un PASS solo cubre esos patrones/archivos.

Añadir scanner mantenido para diff e historial, fixtures sintéticos, escaneo de staging y fallo explícito ante lecturas imposibles. Revisar binarios y dependencias por separado. No imprimir coincidencias secretas ni subir muestras privadas automáticamente.

### SEC-009 · P2 / baja: endurecimiento de identidad y procesos

`PcIdentity.h` y `main.cpp` ahora permiten fallback a PowerShell/ADB del `PATH` solo en builds Debug mediante `STREAMCAM_DEV_SYSTEM_TOOLS`, definido por CMake únicamente para esa configuración. Release exige los runtimes incluidos junto al ejecutable y falla con un mensaje claro si fueron quitados; ya no busca PowerShell en `Program Files` ni ADB en `PATH`. El build Release recompiló localmente. Esto reduce sustitución accidental en distribución, pero no acredita procedencia/autenticidad de esos binarios ni protege frente a modificación local de todo el paquete. Sigue pendiente reducir las copias temporales de clave en JSON/PEM que `PcIdentity.h` crea antes de limpiar buffers; eso puede exponer material en memoria/dumps bajo control local, no es una vía remota demostrada.

En distribución exigir rutas conocidas/procedencia; reservar fallback para desarrollo explícito. Minimizar copias sensibles, excluirlas de diagnósticos y evaluar claves no exportables en almacén Windows. Mantener DPAPI CurrentUser en disco y no pedir elevación administrativa para la app.

## Controles existentes favorables

- TLS 1.2+ en PC; Android verifica pin SHA-256 de clave pública en contexto dedicado, sin redirecciones. `VerifyNone` del servidor no elimina la autenticación de cliente realizada por desafíos firmados en la capa de aplicación.
- QR de un uso y 120 s, aleatoriedad del sistema, comparación constante del token, límites de tamaño y direcciones numéricas privadas.
- Desafíos de reconexión/USB por socket y consumidos; pruebas RSA acotan tamaños; identidades revocadas se rechazan antes de recordar/aceptar.
- Clave Android en Keystore; PC en DPAPI por usuario. Android deshabilita backup y tráfico cleartext; servicio/scanner/diagnóstico no exportados; captura explícita y permisos según modo.
- IPC usa UserAccessOption; OBS limita dimensiones/tamaños antes de reservar y conecta con SECURITY_IDENTIFICATION. No se identificó overflow/RCE demostrado en los parsers revisados.
- QProcess utiliza argumentos separados, sin shell interpolado; instalador por usuario con privilegio mínimo.
- Actualizaciones consultadas por HTTPS con timeout/límite de respuesta y URL restringida al repositorio esperado; abre navegador, no instala código automáticamente.
- Gradle tiene hashes de dependencias. Ausencia de firma PGP por sí sola no invalida esa verificación.

Estos controles son observaciones del código, no prueba de que todos los caminos se comporten correctamente en producción. La semántica por usuario de QLocalServer está documentada por [Qt](https://doc.qt.io/qt-6/qlocalserver.html); el alcance de componentes exportados se explica en [Android Developers](https://developer.android.com/privacy-and-security/risks/android-exported).

## Plan de resolución y puerta de publicación

1. **Sprint inmediato, responsable protocolo/Windows + QA:** SEC-001/003; regresiones de consentimiento obsoleto, cancelación, socket concurrente y revocación. No ampliar controles remotos antes de cerrar estos estados.
2. **Android + UX + protocolo:** SEC-002; ejecutar la prueba adversarial del intent exportado y evaluar autenticación del emisor sin romper el bootstrap ADB legítimo.
3. **Release + seguridad:** SEC-006/007/008; firma, migración, SBOM/procedencia, scanner de paquetes y CI. No comprar ni generar nuevas credenciales en esta revisión.
4. **OBS/IPC + QA:** SEC-004/005; productor autenticado, límites de entrada/slots y pruebas ACL/SMB; separar claramente protección por usuario de confianza en proceso.
5. **Identidad + plataforma:** SEC-009; reducir material privado en memoria y cerrar fallbacks de distribución.

Puerta estable: sin SEC-001/002/003 pendientes; firmas y build no depurable verificados; checks de replay/pin/expiración/revocación y consentimiento en CI; revisión de dependencias con versiones exactas; pruebas físicas y presión controlada; informe de riesgos restantes y artefactos identificados por hash. Las optimizaciones y correcciones multimedia del plan V2 siguen siendo necesarias, pero no sustituyen estas verificaciones de seguridad.

## Verificaciones ejecutadas

- `scripts/audit-public.ps1`: PASS sobre 118 archivos visibles para Git, sin coincidencias de los patrones limitados de claves/tokens/rutas/artefactos del auditor. Se usó la alternativa PowerShell disponible; Python no estaba accesible.
- Recompilación Windows Release posterior al cambio SEC-009: PASS. El paquete `dist/StreamCam-v2-0.3.11-preview-hardened-runtime` pasó `--check-install` y smoke elevado de instalación, reinstalación, identidad/USB, PATH mínimo/AllSigned y desinstalación aisladas en G:. Se verifican 705 hashes del paquete y cuatro del release. SHA-256 del setup: `c4e2afbdcc04a5b3a4d10e1dd631768117271e50431e0dec8a54cff2c13cdeec`. Este smoke confirma el runtime incluido y el instalador; no prueba que una build Release ignore binarios maliciosos puestos en PATH cuando el runtime requerido está ausente, ni la procedencia upstream.
- Tres archivos del manifiesto local `dist/release-0.2.2-preview/SHA256SUMS.txt`: hashes coincidentes. El manifiesto es local y no firmado; esto no prueba procedencia ni inocuidad.
- Authenticode y apksigner: resultados de la tabla de firmas anterior.
- `git diff --check`: sin errores de espacio en los cambios rastreados.

No se atribuyen los avisos del usuario a estos hallazgos: no se conoce el mensaje original ni se analizó el binario instalado en su teléfono/PC.
