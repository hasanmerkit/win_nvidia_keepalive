# GPU Keep-Alive

A tiny Windows utility that keeps an NVIDIA GPU **awake with ZERO load**. It opens a CUDA
context (so the GPU/driver stay active) and just sleeps - no kernels, no math. GPU utilization
stays at **0%**. Uses the installed `nvcuda.dll` directly, so **no CUDA Toolkit / `nvcc`
needed**; builds with the bundled [TCC](https://bellard.org/tcc/) compiler.

## ⚠️ Vibe-coding warning

**This project was vibe-coded** - written quickly with an AI assistant, no real review or
testing. Read the source first and use it at your own risk. **No guarantees.**

Used: Claude Opus 4.8
Tested on: ASUS Rog Strix SCAR 18 (INTEL + NVIDIA), ASUS TUF Gaming A16 (AMD + NVIDIA)

## Why it can help some ASUS laptops

On some ASUS (and similar) laptops with a MUX switch, the system can **stutter or micro-freeze
when the NVIDIA dGPU is powered off** and the driver keeps parking/unparking it. Holding a light
CUDA context keeps the dGPU **settled**, which can smooth out those stutters - with no GPU load.
A workaround, not an official fix; results vary by machine/BIOS/driver.

## Build & run

```cmd
build.cmd                    Build gpu_keepalive.exe (GUI app - runs hidden, no console)

gpu_keepalive.exe            Run hidden in the background, keep the GPU awake
gpu_keepalive.exe --help     Show help
gpu_keepalive.exe --startup  Install to Windows startup
                             (Administrator -> all users, otherwise current user)
```

Runs hidden, so stop it from **Task Manager** (`Stop-Process -Name gpu_keepalive`).

## Behavior

- **No NVIDIA GPU present** -> exits immediately, opens nothing.
- **GPU removed while running** (e.g. MUX disables the dGPU) -> detects it and shuts down cleanly.
- **Load** -> ~0% CPU (sleeps/yields) and 0% GPU (no kernels).

## Notes

- Holding a context keeps the GPU initialized, not at max clocks - it may still idle to a low
  power state (P8). For high clocks, use the driver's *Prefer Maximum Performance* setting.
- The "hidden + copy to startup" pattern can trip some antivirus heuristics.
