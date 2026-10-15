# BoycottAdvance-Reborn — notas de depuración

Reimplementación limpia del emulador GBA BoycottAdvance 0.2.8 (C).
El usuario se comunica en español. Repo privado `Dragonfire51/BoycottAdvance-Reborn`.
`Pokemon Esmeralda.gba` (16 MB, gamecode BPES) vive en la raíz del repo.

## Estado (v1.3.3 + fix Thumb sin tag)

- **HECHO**: GUI nativa Win32 (v1.3.2), renderers GDI/D3D9/Vulkan en Option→Frame Buffer, builds win32/win64, GUI Unicode (v1.3.3).
- **FIX COMMITEADO**: Thumb format 9/10 (LDR/STR/LDRB/STRH inmediate) usaban bits[8:6] como base; debe ser bits[5:3] (Rn). Sin esto, Pokémon Esmeralda se quedaba en el busy-wait VCOUNT de `0x082E7DD0` (`ldrb r0,[r1]` con r1=0x04000006 leía de r0, nunca veía 0x9F) → pantalla negra infinita. Ahora pasa la inicialización.
- **PENDIENTE (pantalla negra Pokémon)**: tras el fix, el PC se descarrila a IWRAM `0x03004xxx` ejecutando datos en modo ARM (ring: bucle en `0x082E9040` → `pop {r1}; bx r1` con stack corrupto). Sospechoso: `LDMIA/POP {pc}` (orden de carga, write-back) o corrupción de pila previa. Primera ejecución IWRAM en frame 4, línea 80, paso ~305k, T=1.

## Harness de depuración

Compilar: `gcc -std=c99 -O2 -Isrc tools/trace_iwram.c src/gba.c src/mem.c src/arm.c src/ppu.c src/apu.c src/bios.c -lm -o /tmp/pk12`
Ejecutar: `/tmp/pk12 g x "Pokemon Esmeralda.gba"` (o `/tmp/pk12 600 /tmp/pokemon.gba`).
Atrapa la primera exec en IWRAM e imprime ring buffer + registros.