# Plugin de OBS

Fuente nativa StreamCam implementada en `streamcam-source.cpp`: recibe BGRA por named pipe restringido al usuario, valida dimensiones/tamaños y entrega video async a OBS. No aloja red ni decoder. Usa IO cancelable, reconecta y borra el último cuadro al perder la conexión. La salida es RAM, no zero-copy.

Compilada y probada con libobs 33.0.1. Prueba sintética H264→Windows→pipe→fuente OBS a1280×720,900 cuadros/~30s,decoder D3D11 RTX3060; la prueba física con Moto sigue pendiente. `install.ps1` instala la DLL con OBS cerrado en la ruta de plugins de ProgramData. Ver `../docs/PRUEBA_INTEGRACION.md` para evidencia y límites.
