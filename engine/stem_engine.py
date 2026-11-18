#!/usr/bin/env python3
"""AI Stem Splitter VST - motor de separacion (Demucs).

Subcomandos:
  probe     Informa que hay instalado (demucs, torch, CUDA, ffmpeg).
  separate  Separa un archivo de audio en stems (WAV) usando CUDA si esta
            disponible y, si no, CPU.
  fetch-model  Descarga el modelo de Demucs (lo usa el instalador).
  setup     Crea un entorno virtual propio e instala torch (CUDA si hay GPU
            NVIDIA), demucs y el resto de dependencias.

Protocolo de salida (una linea por mensaje, stdout, UTF-8):
  PROBE  {json}
  STATUS texto
  PROGRESS 0-100
  RESULT {json}
  ERROR  texto

La logica de perfiles de calidad, modo de 2 stems y deteccion de BPM/tonalidad/
afinacion esta adaptada del ReaScript "AI Stem Splitter" de Oliver Tkach.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import traceback

try:  # salida en UTF-8 y sin buffer, para que el plugin lea en vivo
    sys.stdout.reconfigure(encoding="utf-8", line_buffering=True)
    sys.stderr.reconfigure(encoding="utf-8", line_buffering=True)
except Exception:
    pass

DEFAULT_MODEL = "htdemucs_6s"
STEMS_6 = ["vocals", "drums", "bass", "guitar", "piano", "other"]
QUALITY = {  # shifts, overlap (igual que el script original)
    "fast": (1, 0.1),
    "balanced": (1, 0.25),
    "high": (2, 0.25),
}
# Ruedas de PyTorch con CUDA, de mas nueva a mas vieja. Se prueban en orden.
CUDA_INDEXES = ["cu128", "cu126", "cu124"]
DEMUCS_DEPS = [
    "dora-search", "einops", "julius", "lameenc", "openunmix", "pyyaml",
    "tqdm", "soundfile", "numpy", "scipy", "librosa",
]


def emit(tag: str, payload: str = "") -> None:
    print(f"{tag} {payload}".rstrip(), flush=True)


# --------------------------------------------------------------------------
# probe
# --------------------------------------------------------------------------
def cmd_probe(_args) -> int:
    info = {
        "python": ".".join(map(str, sys.version_info[:3])),
        "python_ok": sys.version_info >= (3, 10),
        "demucs": False,
        "torch": None,
        "cuda": False,
        "gpu": None,
        "nvidia_gpu_present": shutil.which("nvidia-smi") is not None,
        "ffmpeg": shutil.which("ffmpeg") is not None,
        "soundfile": False,
        "librosa": False,
    }
    try:
        import demucs  # noqa: F401
        info["demucs"] = True
    except Exception:
        pass
    try:
        import torch
        info["torch"] = torch.__version__
        info["cuda"] = bool(torch.cuda.is_available())
        if info["cuda"]:
            info["gpu"] = torch.cuda.get_device_name(0)
    except Exception:
        pass
    try:
        import soundfile  # noqa: F401
        info["soundfile"] = True
    except Exception:
        pass
    try:
        import librosa  # noqa: F401
        info["librosa"] = True
    except Exception:
        pass
    emit("PROBE", json.dumps(info))
    return 0


# --------------------------------------------------------------------------
# analisis BPM / tonalidad / afinacion (adaptado del script original)
# --------------------------------------------------------------------------
def analyze_audio(path: str) -> dict:
    out = {"bpm": 0, "key": "", "hz": 0}
    try:
        import scipy.signal
        if not hasattr(scipy.signal, "hann"):
            try:
                scipy.signal.hann = scipy.signal.windows.hann
            except AttributeError:
                pass
        import librosa
        import numpy as np
    except Exception:
        return out

    try:
        y, sr = librosa.load(path, sr=None, duration=60)

        def to_scalar(x, default=0.0):
            try:
                arr = np.asarray(x)
                if arr.size == 0:
                    return float(default)
                return float(arr.reshape(-1)[0])
            except Exception:
                return float(default)

        y_bpm = librosa.effects.percussive(y)
        onset_env = librosa.onset.onset_strength(y=y_bpm, sr=sr, aggregate=np.median)
        tempo = to_scalar(librosa.feature.tempo(onset_envelope=onset_env, sr=sr,
                                                aggregate=np.median))
        if tempo <= 0:
            tempo_bt, _ = librosa.beat.beat_track(y=y_bpm, sr=sr)
            tempo = to_scalar(tempo_bt)
        if tempo > 0:
            if tempo < 55:
                tempo *= 2.0
            elif tempo > 205:
                tempo /= 2.0
        out["bpm"] = int(round(tempo)) if tempo else 0

        chroma = librosa.feature.chroma_cqt(y=y, sr=sr)
        chroma_vals = np.sum(chroma, axis=1)
        maj = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
        mnr = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])
        maj_c = [np.corrcoef(np.roll(maj, i), chroma_vals)[0, 1] for i in range(12)]
        min_c = [np.corrcoef(np.roll(mnr, i), chroma_vals)[0, 1] for i in range(12)]
        names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
        if np.max(maj_c) > np.max(min_c):
            out["key"] = names[int(np.argmax(maj_c))]
        else:
            out["key"] = names[int(np.argmax(min_c))] + "m"

        tuning = to_scalar(librosa.estimate_tuning(y=y, sr=sr))
        out["hz"] = int(round(440 * (2 ** (tuning / 12))))
    except Exception:
        pass
    return out


# --------------------------------------------------------------------------
# lectura de audio
# --------------------------------------------------------------------------
def load_audio(path: str, samplerate: int, channels: int):
    """Devuelve un tensor float32 [canales, muestras] a `samplerate`."""
    import torch

    try:  # camino rapido: soundfile (WAV/FLAC/OGG/MP3), sin ffmpeg
        import soundfile as sf
        data, sr = sf.read(path, dtype="float32", always_2d=True)
        wav = torch.from_numpy(data.T.copy())
        if sr != samplerate:
            import julius
            wav = julius.resample_frac(wav, sr, samplerate)
        if wav.shape[0] == 1 and channels == 2:
            wav = wav.repeat(2, 1)
        elif wav.shape[0] > channels:
            wav = wav[:channels]
        return wav
    except Exception:
        from demucs.audio import AudioFile  # respaldo: ffmpeg
        return AudioFile(path).read(streams=0, samplerate=samplerate, channels=channels)


# --------------------------------------------------------------------------
# progreso: demucs usa tqdm sobre stderr
# --------------------------------------------------------------------------
class ProgressTee:
    """Reemplaza sys.stderr y traduce las barras de tqdm a lineas PROGRESS."""

    PCT = re.compile(r"(\d+)%\|")

    def __init__(self, passes: int):
        self.passes = max(1, passes)
        self.index = 0
        self.last = -1
        self.buf = ""

    def write(self, s):
        self.buf += s
        parts = re.split(r"[\r\n]", self.buf)
        self.buf = parts.pop()
        for part in parts:
            m = self.PCT.search(part)
            if not m:
                if part.strip():
                    emit("LOG", part.strip()[:300])
                continue
            pct = int(m.group(1))
            if pct < self.last:  # empezo otra pasada (shifts > 1)
                self.index = min(self.index + 1, self.passes - 1)
            self.last = pct
            overall = (self.index + pct / 100.0) / self.passes
            emit("PROGRESS", str(int(min(99, overall * 100))))
        return len(s)

    def flush(self):
        pass

    def isatty(self):
        return False


# --------------------------------------------------------------------------
# separate
# --------------------------------------------------------------------------
def resolve_device(requested: str) -> str:
    import torch
    if requested == "cpu":
        return "cpu"
    if requested == "cuda":
        return "cuda" if torch.cuda.is_available() else "cpu"
    return "cuda" if torch.cuda.is_available() else "cpu"  # auto: CUDA primero


def run_separation(model, wav, device, shifts, overlap, jobs):
    import torch
    from demucs.apply import apply_model

    ref = wav.mean(0)
    mean, std = ref.mean(), ref.std()
    wav = (wav - mean) / (std + 1e-8)
    with torch.no_grad():
        sources = apply_model(
            model, wav[None], device=device, shifts=shifts, split=True,
            overlap=overlap, progress=True, num_workers=jobs,
        )[0]
    return sources * (std + 1e-8) + mean


def patch_torch_load(torch) -> None:
    """Los modelos oficiales de Demucs son "pickles" completos: en torch >= 2.6
    torch.load usa weights_only=True por defecto y los rechaza."""
    _orig_load = torch.load

    def _load(*a, **k):
        k.setdefault("weights_only", False)
        return _orig_load(*a, **k)

    torch.load = _load


def cmd_fetch_model(args) -> int:
    """Descarga el modelo ahora (lo usa el instalador) para que la primera
    separacion no tenga que esperar."""
    try:
        import torch
        patch_torch_load(torch)
        from demucs.pretrained import get_model
    except Exception as exc:
        emit("ERROR", f"Faltan dependencias: {exc}")
        return 12
    emit("STATUS", f"Descargando el modelo {args.model}...")
    try:
        get_model(args.model)
    except Exception as exc:
        emit("ERROR", f"No se pudo descargar el modelo: {exc}")
        return 16
    emit("RESULT", json.dumps({"model": args.model}))
    return 0


def cmd_separate(args) -> int:
    try:
        import torch
    except Exception as exc:
        emit("ERROR", f"No se pudo importar torch: {exc}")
        return 12

    patch_torch_load(torch)

    try:
        import soundfile as sf
        from demucs.pretrained import get_model
    except Exception as exc:
        emit("ERROR", f"Faltan dependencias (demucs/soundfile): {exc}")
        return 12

    if not os.path.isfile(args.input):
        emit("ERROR", f"No existe el archivo: {args.input}")
        return 15

    out_dir = os.path.join(args.out, "stems")
    shutil.rmtree(out_dir, ignore_errors=True)
    os.makedirs(out_dir, exist_ok=True)

    shifts, overlap = QUALITY.get(args.quality, QUALITY["balanced"])

    emit("STATUS", "Analizando BPM / tonalidad...")
    analysis = analyze_audio(args.input)

    emit("STATUS", "Cargando modelo (la primera vez se descargan ~2 GB)...")
    try:
        model = get_model(args.model)
    except Exception as exc:
        emit("ERROR", f"No se pudo cargar el modelo {args.model}: {exc}")
        return 16
    model.cpu()
    model.eval()

    emit("STATUS", "Leyendo audio...")
    try:
        wav = load_audio(args.input, model.samplerate, model.audio_channels)
    except Exception as exc:
        emit("ERROR", f"No se pudo leer el audio: {exc}")
        return 17

    device = resolve_device(args.device)
    gpu_name = torch.cuda.get_device_name(0) if device == "cuda" else None
    emit("STATUS", f"Separando con {'CUDA (' + gpu_name + ')' if gpu_name else 'CPU'}...")

    real_stderr = sys.stderr
    sys.stderr = ProgressTee(passes=shifts)
    try:
        try:
            sources = run_separation(model, wav, device, shifts, overlap, args.jobs)
        except Exception as exc:
            if device != "cuda":
                raise
            # CUDA fallo (memoria, driver...): reintento en CPU
            sys.stderr = real_stderr
            emit("STATUS", f"CUDA fallo ({str(exc)[:80]}). Reintentando en CPU...")
            torch.cuda.empty_cache()
            device = "cpu"
            sys.stderr = ProgressTee(passes=shifts)
            sources = run_separation(model, wav, device, shifts, overlap, args.jobs)
    except Exception as exc:
        sys.stderr = real_stderr
        emit("ERROR", f"Fallo la separacion: {exc}")
        traceback.print_exc(file=sys.stdout)
        return 18
    finally:
        sys.stderr = real_stderr

    names = list(model.sources)
    if args.two_stems and args.two_stems != "off":
        if args.two_stems not in names:
            emit("ERROR", f"El stem '{args.two_stems}' no existe en {names}")
            return 19
        keep = sources[names.index(args.two_stems)]
        rest = torch.zeros_like(keep)
        for i, n in enumerate(names):
            if n != args.two_stems:
                rest += sources[i]
        items = [(args.two_stems, keep), ("no_" + args.two_stems, rest)]
    else:
        items = [(n, sources[i]) for i, n in enumerate(names)]
        # orden canonico (el del script original); el modelo devuelve otro orden
        items.sort(key=lambda it: STEMS_6.index(it[0]) if it[0] in STEMS_6 else 99)

    subtype = {"float32": "FLOAT", "int24": "PCM_24"}.get(args.precision, "PCM_16")
    saved = []
    for name, tensor in items:
        t = tensor.detach().cpu()
        if subtype != "FLOAT":  # igual que demucs: evitar clipping con 16/24 bits
            t = t / max(1.01 * float(t.abs().max()), 1.0)
        sf.write(os.path.join(out_dir, f"{name}.wav"), t.numpy().T, model.samplerate,
                 subtype=subtype)
        saved.append(name)

    result = {
        "stems_dir": out_dir,
        "stems": saved,
        "samplerate": int(model.samplerate),
        "device": device,
        "gpu": gpu_name,
        "bpm": analysis["bpm"],
        "key": analysis["key"],
        "hz": analysis["hz"],
    }
    with open(os.path.join(args.out, "result.json"), "w", encoding="utf-8") as f:
        json.dump(result, f)
    emit("PROGRESS", "100")
    emit("RESULT", json.dumps(result))
    return 0


# --------------------------------------------------------------------------
# setup: entorno virtual propio con torch (CUDA si hay GPU NVIDIA) + demucs
# --------------------------------------------------------------------------
def venv_python(venv: str) -> str:
    if os.name == "nt":
        return os.path.join(venv, "Scripts", "python.exe")
    return os.path.join(venv, "bin", "python")


def run_logged(cmd) -> int:
    emit("LOG", " ".join(cmd))
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, encoding="utf-8", errors="replace")
    for line in p.stdout:
        line = line.strip()
        if line:
            emit("LOG", line[:300])
    return p.wait()


# El indice de PyTorch no tiene todas las dependencias de torch (con --index-url pip falla, p. ej.,
# al compilar typing-extensions): torch/torchaudio van sin dependencias y estas se instalan desde PyPI.
TORCH_DEPS = ["filelock", "typing-extensions", "sympy", "networkx", "jinja2", "fsspec", "setuptools"]


def install_torch_cuda(py: str, tag: str) -> int:
    rc = run_logged([py, "-m", "pip", "install", "--no-deps", "--upgrade", "--force-reinstall",
                     "torch", "torchaudio", "--index-url", f"https://download.pytorch.org/whl/{tag}"])
    if rc != 0:
        return rc
    return run_logged([py, "-m", "pip", "install", *TORCH_DEPS])



def cmd_setup(args) -> int:
    if sys.version_info < (3, 10):
        emit("ERROR", "Se necesita Python 3.10 o superior (recomendado 3.11 o 3.12).")
        return 11
    venv = args.venv
    py = venv_python(venv)

    emit("STATUS", "Creando entorno virtual...")
    emit("PROGRESS", "5")
    if not os.path.isfile(py):
        if run_logged([sys.executable, "-m", "venv", venv]) != 0:
            emit("ERROR", "No se pudo crear el entorno virtual.")
            return 20

    emit("STATUS", "Actualizando pip...")
    run_logged([py, "-m", "pip", "install", "--upgrade", "pip"])
    emit("PROGRESS", "10")

    have_nvidia = shutil.which("nvidia-smi") is not None and not args.cpu_only
    installed_torch = False
    if have_nvidia:
        for tag in CUDA_INDEXES:
            emit("STATUS", f"Instalando PyTorch con CUDA ({tag})... (varios GB)")
            rc = install_torch_cuda(py, tag)
            if rc == 0:
                installed_torch = True
                break
    if not installed_torch:
        emit("STATUS", "Instalando PyTorch (CPU)...")
        if run_logged([py, "-m", "pip", "install", "torch", "torchaudio"]) != 0:
            emit("ERROR", "No se pudo instalar PyTorch.")
            return 21
    emit("PROGRESS", "70")

    emit("STATUS", "Instalando Demucs y dependencias...")
    # --no-deps: demucs 4.0.1 fija versiones viejas de torchaudio que romperian
    # el torch recien instalado; las demas dependencias se instalan aparte.
    if run_logged([py, "-m", "pip", "install", "--no-deps", "demucs"]) != 0:
        emit("ERROR", "No se pudo instalar demucs.")
        return 22
    if run_logged([py, "-m", "pip", "install", *DEMUCS_DEPS]) != 0:
        emit("ERROR", "No se pudieron instalar las dependencias de demucs.")
        return 23
    emit("PROGRESS", "100")
    emit("RESULT", json.dumps({"python": py}))
    return 0


# --------------------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("probe")

    sp = sub.add_parser("separate")
    sp.add_argument("input")
    sp.add_argument("--out", required=True, help="carpeta de salida (cache)")
    sp.add_argument("--model", default=DEFAULT_MODEL)
    sp.add_argument("--quality", choices=list(QUALITY), default="balanced")
    sp.add_argument("--two-stems", default="off")
    sp.add_argument("--precision", choices=["default", "int24", "float32"], default="default")
    sp.add_argument("--device", choices=["auto", "cuda", "cpu"], default="auto")
    sp.add_argument("--jobs", type=int, default=0)

    fm = sub.add_parser("fetch-model")
    fm.add_argument("--model", default=DEFAULT_MODEL)

    st = sub.add_parser("setup")
    st.add_argument("--venv", required=True)
    st.add_argument("--cpu-only", action="store_true")

    args = ap.parse_args()
    try:
        return {"probe": cmd_probe, "separate": cmd_separate, "setup": cmd_setup,
                "fetch-model": cmd_fetch_model}[args.cmd](args)
    except SystemExit:
        raise
    except Exception as exc:
        emit("ERROR", f"Error inesperado: {exc}")
        traceback.print_exc(file=sys.stdout)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
