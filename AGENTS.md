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
- **FIX COMMITEADO (nucleo, rama `core-irq-dma-fixes`)**: tres bugs reales, verificados con harness propio:
  1. Los 4 canales DMA ocupan **12 bytes** cada uno (`0x0B0/0x0BC/0x0C8/0x0D4`), no 16. Con el stride de 16 todo acceso por encima de DMA0 caia en el canal equivocado. Ademas el bloque DMA estaba **dentro** del `switch` de `io_read16`/`io_write16` tras `return`s inalcanzables (codigo muerto). Movido **antes** del `switch` con el layout real de 12 bytes.
  2. `arm_execute()` **borraba IF** al tomar la IRQ. El hardware real lo deja puesto: el handler lee `IE&IF` para identificar la fuente y lo limpia escribiendo 1 en IF. Pre-limpiarlo hacia que el dispatcher viera 0 y ejecutara un stub vacio, asi que el flag de espera de VBlank nunca se ponia.
  3. Thumb formato 5: ADD/MOV con Rd=PC es un branch; el PC escrito por la instruccion no debe sobrescribirse con el valor de fall-through.
- **PENDIENTE (pantalla blanca Pokemon Esmeralda)**: tras los fixes anteriores la IRQ ya se entrega (el vector `0x18` del BIOS llega al handler `0x03002750`, `IF`=0005, `IME`=1), pero `DISPCNT`/`DISPSTAT` siguen a `0000` y `IE`=0005 sin que nadie habilite el bit de VBlank (bit 0). El juego espera el flag `[0x030022DC] & 1` en el busy-wait `0x080008C6`; ese flag lo pone el handler de VBlank del juego (`0x08000844`, que ademas llama al helper de DISPSTAT `0x0800103C`). Ese handler **solo corre si la VBlank IRQ esta habilitada**, y nunca lo esta: en el arranque `IE` queda en `0005` (bits 0 y 2 = VBlank + VCount) pero el **bit de habilitacion en DISPSTAT (bit 3)** nunca se escribe. Forzando a mano `DISPSTAT|=8` e `IE|=1` el juego **si progresa**: escribe `DISPSTAT=0x9628` y el flag pasa a 1 (llega a la inicializacion de video). Conclusion: el problema esta aguas arriba, falta una escritura de DISPSTAT (o el estado que la provoca) en la ruta de arranque. Siguiente paso: localizar por que el juego no ejecuta su rutina de init de DISPSTAT (helper `0x0800103C`, llamada desde `0x08000788`), revisando que condicion la salta.
- **Nota de reproduccion**: en este entorno `/tmp` se limpia entre comandos largos; recompilar el harness antes de usarlo. `capstone` puede desaparecer del `python3` por defecto y hay que reinstalarlo (`pip install capstone`).


## Harness de depuraciĂłn

Compilar: `gcc -std=c99 -O2 -Isrc tools/trace_iwram.c src/gba.c src/mem.c src/arm.c src/ppu.c src/apu.c src/bios.c -lm -o /tmp/pk12`
Ejecutar: `/tmp/pk12 g x "Pokemon Esmeralda.gba"` (o `/tmp/pk12 600 /tmp/pokemon.gba`).
Atrapa la primera exec en IWRAM e imprime ring buffer + registros.