# Interfaz y ciclo de vida Windows

`CMakeLists.txt` y `main.cpp` crean el cascarón Qt 6 Widgets de StreamCam para Windows. La pantalla deja claros los estados offline, la selección futura USB/Wi‑Fi y los espacios de métricas/preview. Los botones de pairing quedan deshabilitados hasta que exista el receptor autenticado; la UI no simula una conexión.

## Compilación

Requiere Qt 6.6 o superior con Widgets, CMake 3.25 o superior y un compilador C++20 de Windows (MSVC recomendado). Desde una terminal de desarrollo:

```powershell
cmake -S desktop/app -B desktop/out -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64"
cmake --build desktop/out --config Release
```

Estado actual: las herramientas CMake, compilador C++ y Qt no están instaladas en esta máquina, así que este esqueleto aún no se compiló. El schema preliminar de control está en `protocol/control-v1.schema.json` y su sintaxis JSON se verificó; falta validación de esquema integrada al build. Próximo componente: `desktop/media` debe exponer una interfaz de sesión que la UI pueda iniciar/detener y escuchar sin recibir callbacks de frames en el hilo gráfico.
