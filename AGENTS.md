# BoycottAdvance-Reborn â€” notas de depuraciĂłn

ReimplementaciĂłn limpia del emulador GBA BoycottAdvance 0.2.8 (C).
El usuario se comunica en espaĂ±ol. Repo privado `Dragonfire51/BoycottAdvance-Reborn`.
`Pokemon Esmeralda.gba` (16 MB, gamecode BPES) vive en la raĂ­z del repo.

## Documentación de referencia (OBLIGATORIA)

**Usar SIEMPRE GBATEK como referencia** para corregir bugs y añadir implementaciones:
<https://problemkaputt.de/gbatek.htm> (texto plano: <https://problemkaputt.de/gbatek.txt>).
Es el documento técnico de referencia de no$gba (Martin Korth): mapas de memoria/IO,
registros LCD/sonido/DMA/timers/interrupciones, opcodes ARM/THUMB, funciones BIOS.
Antes de tocar el núcleo, comprobar el comportamiento contra GBATEK y citar la sección.

Nota de acceso: el sitio devuelve **403** a `curl`/`wget` directos (bloqueo de
user-agent). Descargarlo con la herramienta de extracción web (Tavily) o desde un
navegador; no usar `curl` para leerlo.

## Estado (v1.4.2; tags `Latest` = `v1.4.1` movidos; version en src/gba.h)

- **HECHO (v1.4.2)**: `File > Choose State Slot` es ahora un submenú con 10 slots (Slot 0..9), radio-check del activo, `Load/Save State` usan el slot elegido y se persiste como `StateSlot` en `boycottadvance-reborn.ini` (cargado en cfg_load con validación de rango). Mismo comportamiento en gui.c (nativo) y main.c (SDL). IDs `M_SLOT0..M_SLOT_LAST` en uiactions.h.

- **CAUSA RAĂŤZ DE DOS QUEJAS DEL USUARIO (v1.4.1)**: existĂ­an DOS releases. GitHub marca como "latest" el release **`v1.4.0`** (id 414357160, tag `v1.4.0` = commit `8b48db5`), NO el release con tag `Latest`. Ese binario v1.4.0 es antiguo: subsistema **CONSOLE (3)** â†’ abre ventana de consola, y escribe **`BoycottAdvance.ini`**. El release "Latest" (id 413237927) sĂ­ tiene el binario bueno (GUI subsystem 2, `boycottadvance-reborn.ini`). Por eso el usuario veĂ­a consola + ini viejo + controles viejos: descargaba el "Latest" de GitHub = v1.4.0. **Fix**: subir versiĂłn, reconstruir y publicar el release `Latest` como la versiĂłn mĂˇs nueva, y mover el tag a ese commit.
- **FIX COMMITEADO (controles/ini, v1.4.1)**: los binarios Windows de v1.4.0 se construyeron desde `8b48db5`, ANTES de `ccec1d1` (que introdujo el menĂş Controls/joystick) y de `885e9ee` (que cambiĂł el ini a reborn y el subsistema GUI). Por eso esos binarios no tienen el diĂˇlogo nuevo. **LecciĂłn**: al publicar, compilar SIEMPRE desde el commit del tag de release, no reutilizar exes antiguos.
- **HECHO (v1.4.1)**: `Options > Controls > Input Configuration...` abre un diĂˇlogo modal nativo (`gui_native_input_config` en gui.c) con pĂˇginas Keyboard/Joystick, una fila por botĂłn GBA (Button A/B/L/R, SELECT, START, UP, DOWN, LEFT, RIGHT), OK/Cancel y captura de tecla/botĂłn. Sustituye los items "Map X" del submenĂş. Callbacks nuevos en `GuiCallbacks`: `key_get/key_set/joy_get/joy_set`.
- **HECHO (v1.4.1)**: Helpâ†’History muestra los cambios de v1.4.1 y v1.4.0 (siempre los de la Ăşltima versiĂłn).
- **HECHO**: GUI nativa Win32 (v1.3.2), renderers GDI/D3D9/Vulkan en Optionâ†’Frame Buffer, builds win32/win64, GUI Unicode (v1.3.3).
- **FIX COMMITEADO**: Thumb format 9/10 (LDR/STR/LDRB/STRH inmediate) usaban bits[8:6] como base; debe ser bits[5:3] (Rn). Sin esto, PokĂ©mon Esmeralda se quedaba en el busy-wait VCOUNT de `0x082E7DD0` (`ldrb r0,[r1]` con r1=0x04000006 leĂ­a de r0, nunca veĂ­a 0x9F) â†’ pantalla negra infinita. Ahora pasa la inicializaciĂłn.
- **FIX COMMITEADO (controles)**: `ini_get()` comparaba por prefijo, asĂ­ que `Key_R`/`Key_L` colisionaban con `Key_Right`/`Key_Left` y los remaps R/L se perdĂ­an al cargar. Ahora exige delimitador tras el nombre. SubmenĂş nativo Joystick con remap/reset y D-pad (POV). `M_RESETJOY` solapaba con `M_JOY0` (id duplicado).
- **FIX COMMITEADO (nĂşcleo)**: `dma_run` decodificaba mal los bits de control de direcciĂłn (0=incremento, 1=decremento, 2=fijo). SRAM/Flash de cartucho en `0x0E000000` se leĂ­a/escribĂ­a sobre el array de ROM (las escrituras se descartaban) â†’ las partidas no se guardaban. `CpuSet` enmascaraba el contador a 20 bits (son 21). Los buffers de save/load state eran locales de 1 MB â†’ desbordaban la pila del hilo y crasheaban; ahora son `static`.
- **FIX COMMITEADO (ajustes BIOS)**: los toggles BIOS/SkipIntro solo cambiaban el flag; ahora re-ejecutan `gba_reset()` si hay ROM cargada, y `skip_intros` se copia de la config al nĂşcleo en el arranque (antes nunca se leĂ­a).
- **PENDIENTE (pantalla blanca/crash PokĂ©mon Esmeralda)**: el nĂşcleo se queda atascado en la inicializaciĂłn; en 400 frames `DISPCNT` sigue en `0000` y `0x03003090` nunca recibe cĂłdigo, asĂ­ que nunca se configura el vĂ­deo. El bucle visible es el *busy-wait* de `0x082E9040` (`ldrh r0,[r1]; subs r0,#1; ldrh r0,[r1]; strh r0,[r1]; cmp r0,#0; bne`). El PC acaba en ARM sobre `0x03003090` (regiĂłn a cero) vĂ­a el epĂ­logo `pop {r4-r7}; pop {r1}; bx r1` de `0x0800B75C`; el valor `0x03003090` (bit 0 = 0 â†’ ARM) se carga de `[0x03007DC0]`. Antes de tocar `bx`/`LDM` hay que confirmar por quĂ© ese valor estĂˇ en la pila y por quĂ© el bloque de cĂłdigo esperado en `0x03003090` no se copia (en el log hay un `CpuSet` con `src` fijo `0x03007DF0` â†’ `0x03003090`, que escribe 80 bytes; puede ser un borrado deliberado). `swi_register_ram_reset` (SWI 0x01) sigue siendo un no-op.


## Harness de depuraciĂłn

Compilar: `gcc -std=c99 -O2 -Isrc tools/trace_iwram.c src/gba.c src/mem.c src/arm.c src/ppu.c src/apu.c src/bios.c -lm -o /tmp/pk12`
Ejecutar: `/tmp/pk12 g x "Pokemon Esmeralda.gba"` (o `/tmp/pk12 600 /tmp/pokemon.gba`).
Atrapa la primera exec en IWRAM e imprime ring buffer + registros.