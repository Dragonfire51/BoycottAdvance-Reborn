# BoycottAdvance-Reborn — notas de depuración

Reimplementación limpia del emulador GBA BoycottAdvance 0.2.8 (C).
El usuario se comunica en español. Repo privado `Dragonfire51/BoycottAdvance-Reborn`.
`Pokemon Esmeralda.gba` (16 MB, gamecode BPES) vive en la raíz del repo.

## Estado (v1.4.0 + fixes de controles/núcleo, tag `Latest` = a61402e)

- **HECHO**: GUI nativa Win32 (v1.3.2), renderers GDI/D3D9/Vulkan en Option→Frame Buffer, builds win32/win64, GUI Unicode (v1.3.3).
- **FIX COMMITEADO**: Thumb format 9/10 (LDR/STR/LDRB/STRH inmediate) usaban bits[8:6] como base; debe ser bits[5:3] (Rn). Sin esto, Pokémon Esmeralda se quedaba en el busy-wait VCOUNT de `0x082E7DD0` (`ldrb r0,[r1]` con r1=0x04000006 leía de r0, nunca veía 0x9F) → pantalla negra infinita. Ahora pasa la inicialización.
- **FIX COMMITEADO (controles)**: `ini_get()` comparaba por prefijo, así que `Key_R`/`Key_L` colisionaban con `Key_Right`/`Key_Left` y los remaps R/L se perdían al cargar. Ahora exige delimitador tras el nombre. Submenú nativo Joystick con remap/reset y D-pad (POV). `M_RESETJOY` solapaba con `M_JOY0` (id duplicado).
- **FIX COMMITEADO (núcleo)**: `dma_run` decodificaba mal los bits de control de dirección (0=incremento, 1=decremento, 2=fijo). SRAM/Flash de cartucho en `0x0E000000` se leía/escribía sobre el array de ROM (las escrituras se descartaban) → las partidas no se guardaban. `CpuSet` enmascaraba el contador a 20 bits (son 21). Los buffers de save/load state eran locales de 1 MB → desbordaban la pila del hilo y crasheaban; ahora son `static`.
- **FIX COMMITEADO (ajustes BIOS)**: los toggles BIOS/SkipIntro solo cambiaban el flag; ahora re-ejecutan `gba_reset()` si hay ROM cargada, y `skip_intros` se copia de la config al núcleo en el arranque (antes nunca se leía).
- **PENDIENTE (pantalla blanca/crash Pokémon Esmeralda)**: el núcleo se queda atascado en la inicialización; en 400 frames `DISPCNT` sigue en `0000` y `0x03003090` nunca recibe código, así que nunca se configura el vídeo. El bucle visible es el *busy-wait* de `0x082E9040` (`ldrh r0,[r1]; subs r0,#1; ldrh r0,[r1]; strh r0,[r1]; cmp r0,#0; bne`). El PC acaba en ARM sobre `0x03003090` (región a cero) vía el epílogo `pop {r4-r7}; pop {r1}; bx r1` de `0x0800B75C`; el valor `0x03003090` (bit 0 = 0 → ARM) se carga de `[0x03007DC0]`. Antes de tocar `bx`/`LDM` hay que confirmar por qué ese valor está en la pila y por qué el bloque de código esperado en `0x03003090` no se copia (en el log hay un `CpuSet` con `src` fijo `0x03007DF0` → `0x03003090`, que escribe 80 bytes; puede ser un borrado deliberado). `swi_register_ram_reset` (SWI 0x01) sigue siendo un no-op.


## Harness de depuración

Compilar: `gcc -std=c99 -O2 -Isrc tools/trace_iwram.c src/gba.c src/mem.c src/arm.c src/ppu.c src/apu.c src/bios.c -lm -o /tmp/pk12`
Ejecutar: `/tmp/pk12 g x "Pokemon Esmeralda.gba"` (o `/tmp/pk12 600 /tmp/pokemon.gba`).
Atrapa la primera exec en IWRAM e imprime ring buffer + registros.