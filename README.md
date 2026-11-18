# AI Stem Splitter by Oliver Tkach (prototipo 0.1, solo Windows)

Plugin VST3 que separa una canción en stems con Demucs (`htdemucs_6s`) y los reproduce
sincronizados con el DAW. Usa **CUDA** si hay GPU NVIDIA con PyTorch compatible y, si no, **CPU**.

> **Estado:** el código está escrito pero **todavía no fue compilado ni probado**. Este entorno
> no tiene acceso a GitHub ni PyPI, así que no se pudo bajar JUCE ni Demucs. Se espera que el primer
> build en GitHub Actions o Visual Studio muestre algún error de compilación para corregir.

## Cómo funciona

1. Cargás el plugin como **instrumento** (VSTi) en una pista.
2. Soltás un archivo de audio en la ventana (o "Elegir archivo...").
3. "Separar" lanza el motor Python (`engine/stem_engine.py`, embebido en el `.vst3`) en segundo plano.
4. Los stems quedan en `%LOCALAPPDATA%\AIStemSplitter\cache\...` (si repetís el mismo archivo y opciones, no se reprocesa).
5. El plugin los reproduce en sincronía con el transporte del DAW. Hay 7 salidas estéreo:
   - **Mix** (con mute / solo / ganancia por stem)
   - **1 Vocals, 2 Drums, 3 Bass, 4 Guitar, 5 Piano, 6 Other**: salidas individuales sin procesar. Se activan desde el DAW.
   - En modo **2 stems** se usan solo las salidas 1 y 2 (stem elegido y "no_" + stem).
6. También podés **arrastrar cada stem** (botón "arrastrar") o todos juntos a la línea de tiempo.
   El WAV queda con el nombre `<canción> - <stem>.wav`.
7. **Offset**: desplaza los stems respecto al inicio del proyecto. Ponelo igual a la posición del clip original.

### Motor y CUDA
- Prioridad: Python propio del plugin → el entorno `.venv` del ReaScript original (si existe) → `python` / `py -3` del sistema.
  Entre los que tengan Demucs, gana el que tenga **CUDA**.
- Menú **Motor**: "Instalar motor" crea un venv en `%LOCALAPPDATA%\AIStemSplitter\venv` e instala PyTorch con CUDA (prueba cu128, cu126, cu124) si detecta `nvidia-smi`, o CPU si no, más Demucs.
- Si CUDA falla en ejecución (por ejemplo, poca VRAM), reintenta en CPU.
- Ojo: `pip install demucs` en Windows suele traer PyTorch **solo CPU**. El plugin avisa si hay GPU NVIDIA y el torch instalado no tiene CUDA.
- Python recomendado: 3.10 a 3.12.

## Instalador (Windows)

El workflow de GitHub Actions genera `AIStemSplitter-Setup-<version>.exe` (Inno Setup). Un doble clic:

1. Copia el plugin a `C:\Program Files\Common Files\VST3\AI Stem Splitter by Oliver Tkach.vst3` (lo escanean todos los DAWs).
2. Si dejás tildada la opción "Instalar ahora el motor de IA", abre una ventana de PowerShell que, como **tu usuario** (no administrador):
   - busca Python 3.10-3.12 y, si no hay, descarga e instala Python 3.12.10 desde python.org (solo para tu usuario);
   - crea `%LOCALAPPDATA%\AIStemSplitter\venv` con **PyTorch + CUDA** si detecta GPU NVIDIA (prueba cu128, cu126, cu124) o CPU si no, y Demucs;
   - descarga el modelo `htdemucs_6s`;
   - guarda un registro en `%LOCALAPPDATA%\AIStemSplitter\install.log`.
3. Al terminar: reiniciá el DAW y volvé a escanear VST3.

Si el motor falla o lo saltaste, se puede instalar después desde el plugin: menú **Motor > Instalar motor**.
El desinstalador quita el plugin y pregunta si querés borrar también el motor, modelos y caché (varios GB).

Notas: el instalador no está firmado (SmartScreen avisará) y el instalador de Python se descarga sin verificar su hash.

## Compilar

### Opción A: GitHub Actions (no hace falta instalar nada)
1. Creá un repositorio y subí esta carpeta.
2. En la pestaña **Actions** corre "Build VST3 + instalador (Windows)".
3. Descargá el artefacto `AIStemSplitter-Setup` (el instalador) o `AI-Stem-Splitter-VST3-Windows` (solo el `.vst3`).

### Opción B: local
Requisitos: Visual Studio 2022 (carga de trabajo C++), CMake 3.22+, Git, y Inno Setup 6 para el instalador.
```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target AIStemSplitter_VST3
mkdir dist & xcopy /E /I "build\AIStemSplitter_artefacts\Release\VST3\AI Stem Splitter by Oliver Tkach.vst3" "dist\AI Stem Splitter by Oliver Tkach.vst3"
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\AIStemSplitter.iss
```

## Pendientes conocidos
- Remuestreo lineal si el proyecto no está a 44.1 kHz (suficiente para prototipo; mejorar con sinc/Lagrange o pre-resample con ffmpeg).
- Sin segmento/jobs configurables (no pude verificar los límites de `--segment` con htdemucs).
- Instalador y plugin sin firma de código (Windows SmartScreen puede avisar).
- macOS/Linux: el código es portable, pero no se probó ni se configuró.

## Licencias y créditos
- **JUCE** se usa bajo GPLv3 (o licencia comercial). Si distribuís el plugin, el resultado debe cumplir la GPL o necesitás licencia comercial de JUCE.
- **Demucs** (MIT) de Meta/Facebook Research.
- La lógica de perfiles de calidad, modo de 2 stems y la detección de BPM/tonalidad/afinación está adaptada del ReaScript
  **AI Stem Splitter** de **Oliver Tkach** (refactor: SifuInTheShell). Si vas a publicar o vender esto, pedí permiso a los autores
  o reescribí esas partes.
