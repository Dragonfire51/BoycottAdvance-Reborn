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

## Bootstrap del entorno (antes de tocar codigo)

Claude Code arranca sin memoria de sesiones anteriores: el unico contexto
persistente es este archivo. Leerlo entero al empezar.

**Referencia mGBA (OBLIGATORIA)**: ademas de GBATEK, el codigo fuente de mGBA es
la referencia de comportamiento para bugs de emulacion. Clonarlo una vez:

```bash
git clone --depth 1 https://github.com/mgba-emu/mgba /workspace/mgba
```

Metodo de depuracion probado: compilar el core de mGBA y el nuestro con el mismo
ROM e instrumentar ambos para comparar estado **frame a frame** (DISPCNT, VRAM,
paleta, OAM, framebuffer, histograma de SWI). Asi se localizo la causa real de la
pantalla negra de Esmeralda (SWI de descompresion del BIOS sin implementar). No
basta con leer mGBA: hay que differ el estado observable.

**Dependencias** (una sola vez por entorno):

```bash
# Nucleo + build Linux (SDL2)
sudo apt-get update && sudo apt-get install -y build-essential pkg-config libsdl2-dev

# Cross-compile Windows nativo (GDI/D3D9/WinMM, sin SDL2)
sudo apt-get install -y gcc-mingw-w64-i686 gcc-mingw-w64-x86-64
```

**Compilar los tres targets** (todos deben pasar sin errores antes de commitear):

```bash
make                              # Linux (usa SDL2)
make -f Makefile.win32            # Windows x86_64 (PE32+, sin SDL2)
make -f Makefile.win32 ARCH=i686  # Windows x86   (PE32,  sin SDL2)
```

Verificar que el binario Windows sea nativo (sin SDL2) y de subsistema GUI:

```bash
x86_64-w64-mingw32-objdump -p boycottadvance-reborn-x86_64.exe | grep -i "DLL Name"
# esperado: KERNEL32, USER32, GDI32, SHELL32, d3d9, winmm, comdlg32, msvcrt
```

**Trampa de edicion**: las recetas de `Makefile` y `Makefile.win32` usan
tabuladores reales. Si un editor los convierte a espacios, `make` falla con
`missing separator`. No reformatear esos archivos.

**Verificacion sin pantalla**: en este entorno no hay display ni hardware, asi que
los bugs de video/sonido se verifican por instrumentacion (comparar registros y
memoria contra mGBA / la referencia), no "a ojo". Aportar numeros antes/despues.

**Regla de la rama Win32**: no introducir SDL2. La GUI es nativa (ventana GDI /
Direct3D 9, audio WinMM/DirectSound, menus Win32 completos).

## FIX decompresion a VRAM (SESION 2026-10-22) — barras verticales en Esmeralda

**Sintoma**: bandas verticales blancas/verdes encima del escenario de Pokemon Esmeralda.

**Causa raiz**: las SWI de descompresion del BIOS (`src/bios.c`) escribian **un halfword por byte**
en las variantes VRAM y avanzaban el destino **2 bytes por byte**. Resultado: el flujo quedaba
esparcido al doble de rango y el byte impar de cada halfword sin escribir (patron `0x0011`
repetido en VRAM, que es literalmente una rejilla de 1 pixel, o sea las barras).

**Arreglo**: helper `Dst`/`dst_put()` que empaqueta dos bytes por halfword (GBATEK
"LZ77UnCompVram": un byte de salida por byte de destino). Aplicado a `swi_lz77` (0x11/0x12),
`swi_rluncomp` (0x14/0x15, con el padding de halfword posterior) y `swi_diffunfilter`
(0x16 = 8bit->WRAM, 0x17 = 8bit->VRAM, 0x18 = 16bit->16bit; el dispatch previo pasaba los flags
equivocados). Fuente alineada a 4 en los filtros.

**Verificacion (differ contra mGBA, mismo ROM)**: VRAM en el frame 900 pasó de **30905 bytes
distintos a 0**; paleta y OAM ya coincidian (`0` y `40` bytes). El primer frame divergente era
f10, justo cuando corre `SWI 0x12` (`src=0x08DC3E1C -> dst=0x06000000`, len 1248). El PPU ya era
correcto: renderizando *sus* datos de referencia pintaba la escena bien.

**Los zip `files.zip` / `Claude_Fixes.zip` del repo son copias ANTIGUAS**: no contienen ningun
arreglo que falte aqui. `flash.c` es identico; `apu.c` del zip es el diseno viejo (campos
`ch1_on`/`nr12`, sin ring buffer) y sus 11 fixes A1-A11 ya estan reimplementados en el `apu.c`
actual (FIFO con signo, L/R por bit, volumen maestro, square 16*P, wave 8*P, LFSR, envolventes,
ciclos fraccionarios). `mem.c` del zip NO tiene el clamp de ventanas y `arm.c` del zip NO tiene
FIQ banked / `user_reg()` / MULS N-Z / retorno por SPSR. Los `.zip` se pueden borrar del repo.

**Metodo util**: `mCoreInitConfig()` es obligatorio antes de `mCoreLoadFile()` en mGBA; luego
`core->getPixels()` + `busRead8()` para volcar VRAM/paleta/OAM de referencia y diferenciar
contra `g->vram`.

## Estado (v1.4.8; cambios de fidelidad GBATEK sobre el zip de Claude)

**FIXES DE FIDELIDAD GBATEK (v1.4.8, aplicados sobre el commit "Files fixed by Claude"):**
- `src/bios.c` — `swi_intr_wait` (SWI 0x04) implementa la semantica real de GBATEK "IntrWait/IntrHalt": `r0 bit0 = clear` descarta los flags pendientes antes de esperar; con clear=0 y una IRQ del mask ya pendiente **no** haltea (retorno inmediato). Antes solo hacia `halt=true` incondicional. `swi_div32` (SWI 0x06) maneja los dos casos de UB del C: `0x80000000 / -1` -> cociente `0x80000000` con resto 0 (ARM7TDMI, GBATEK "Div/DivArm" no dispara excepcion), y `x / 0` -> devuelve el dividendo con resto 0 sin evaluar la division. Antes era UB.
- `src/arm.c` — flags de multiplicacion: `MUL{S}/MLA{S}` (ARM) y `MULS` (Thumb formato 4, subop 0xD) solo actualizan **N y Z**; C y V quedan intactos (GBATEK "ARM MUL/MLA", "Thumb ALU ops"). Antes `flags_logic(...,C)` sobreescribia C con el carry entrante. `MSR SPSR` en modo **USR/SYS** no escribe nada (GBATEK: el modo USR no tiene SPSR). Antes escribia al banco USER sin bloquear.
- `src/flash.c`/`src/apu.c` — ya traian las correcciones del zip: flash program escribe al destino real y bank switch se aplica en la escritura inmediata; APU con frecuencias reales (`*16` square, `*8` wave, LFSR `64r/32 << s`), mezcla estereo por bit (0x100/0x200/0x1000/0x2000) y volumen maestro SOUNDCNT_L. Verificado: no es el "mezclador simplificado" que se habia visto antes.

**Verificacion sintetica tras los fixes:** boot fastboot f700 `pc=080008C8 DISPCNT=1F40`; boot BIOS real completa replica la secuencia documentada de v1.4.7 (`0080->1002->9802->handoff f266->0000->0140->1F40 f453`) sin cuelgues; memoria f1200 `VRAM nz=16552 PAL nz=1024/1024 OAM nz=582`; test sintetico `/tmp/gbatek_test.c` (11 checks de div/IntrWait) 11/11 OK. Nota: `swi_intr_wait` y `swi_div32` estan expuestos (no-static) para permitir los tests sinteticos.

**Build v1.4.8 (los tres targets compilan limpios):**
- Linux: `make` -> `boycottadvance-reborn`
- Win64: `make -f Makefile.win32 ARCH=x86_64` -> `boycottadvance-reborn-x86_64.exe` (subsistema GUI=2, DLLs nativas sin SDL2)
- Win32: `make -f Makefile.win32 ARCH=i686` -> `boycottadvance-reborn-i686.exe` (subsistema GUI=2, DLLs nativas sin SDL2)
- OJO: `make -f Makefile.win32 clean` borra AMBOS .exe. Para dejar los dos finales: compilar win32, copiar el .exe a un respaldo, `clean`, compilar win64, restaurar el respaldo.

## FIX keypad (SESION 2026-10-21/22)

**Bug (reportado por el usuario): "un direccional presionado constantemente"** (Eragon subia/bajaba solo). Causa raiz: doble inversion del keypad. `key_state()` (win32.c/main.c) construye la mascara **activa en bajo** igual que KEYINPUT real de GBATEK (`0x03FF` = nada, bit 0 = pulsado), pero `gba_update_keypad()` en `src/gba.c` hacia `g->key_input = ~keys`, invirtiendo DOS veces: con nada pulsado el juego leia `0x0000` (las 10 teclas pulsadas). En reposo el D-pad quedaba "pegado". Fix: `g->key_input = keys & 0x3FF` (guardar tal cual, activo en bajo). Verificado: test sintetico 6/6 PASS + regression "UP/DOWN libres en reposo" PASS; `KEYINPUT` devuelve lo mismo que entra. El bug anterior hacia que TODOS los botones estuvieran "pulsados" siempre; el sintoma visible de Eragon era solo el mas evidente.

**Clamp de ventanas (GBATEK "LCD I/O Window Feature")**: `io_write16` en `src/mem.c` ahora clampa `WIN0H/WIN1H` (`X2>240` o `X1>X2` => `X2=240`) y `WIN0V/WIN1V` (`Y2>160` o `Y1>Y2` => `Y2=160`), como hace el hardware. Antes se guardaba el valor crudo. Importante: `case 0x040: case 0x042:` comparten bloque y usan `reg` para elegir ventana — no escribir `p->winh[0]` fijo en ambos.

**Sobre la lista de Deepseek (evaluada contra el codigo real, NO todo aplicable):** eeprom.c (start bit + opcode + addr) y flash.c PROGRAM/SWITCH_BANK ya eran correctos (mGBA savedata.c usa el MISMO protocolo: `0xB0` se latchea y el cambio de banco ocurre en la 4a escritura a `0x0000`; aplicar el cambio en el ciclo `0xB0` lo romperia). arm.c `write_reg` ya alinea r15 (`v &= ~1u`). ppu.c y bios.c ya tenian los fixes. Lo unico que faltaba de esa lista era el clamp de ventanas (incluido arriba).

## Estado (v1.4.7; tags `Latest` = `v1.4.7`; version en src/gba.h)

- **RELEASE v1.4.7 (publicado)**: fix del **pantalla negra tras el logo** (bug 1) — la causa real. En v1.4.6 se creyo resuelto por el fix de HALTCNT, pero al medir la referencia (`/workspace/h/refcore`, mGBA) contra nuestro core, el BIOS **si** entregaba el control pero el juego se quedaba con `DISPCNT=0000` para siempre: **faltaban las SWI de descompresion del BIOS**. Pokemon Emerald descomprime todos sus graficos con `SWI 0x12` (LZ77UnCompVram) en el frame siguiente al handoff; sin handler la transferencia no escribia nada, VRAM quedaba vacia y la pantalla negra. Implementadas en `src/bios.c`: `swi_bitunpack` (0x10), `swi_lz77` (0x11/0x12, variante VRAM con writes de 16 bits y step=2), `swi_huffuncomp` (0x13), `swi_rluncomp` (0x14/0x15), `swi_diffunfilter` (0x16/0x17/0x18) + `swi_read_len` (longitud de 3 bytes LE) y retorno de la longitud descomprimida en `r0`. Verificado con `ourstats` contra `refdisp`: antes del fix `DISPCNT=0000` fijo a f4000+; despues el intro corre y la secuencia DISPCNT (`0000 -> 0080 -> 1002 -> 9802 -> 0140 -> 1F40`) y el framebuffer acompanan a la referencia (f1900 nz=251, f2000+ nz=4800, f3000 nz=38400). Incluye ademas el chip de **flash paralelo** (`src/flash.c`, JEDEC autoselect/program/sector-erase/bank-switch) que Emerald sondea al iniciar el guardado, y su enganche en `mem.c`/`gba.c`/`Makefile`/`Makefile.win32`. Proceso de publicacion: bump `GBA_VERSION` + README + dialogos de historial -> commit -> push `main` -> `git tag -a v1.4.7` + push (dispara `.github/workflows/build-binaries.yml`) -> `git tag -f Latest` + push -f y reemplazar los assets del release `Latest` via API.

- **RELEASE v1.4.6 (publicado)**: fix del **pantalla negra tras el logo** (bug 1). Causa raiz: en `gba_write8` la deteccion de HALTCNT usaba `(addr & 0x0FFFFFFF) == 0x00000301`, pero `0x04000301 & 0x0FFFFFFF = 0x4000301`, asi que **nunca** coincidia; la escritura del BIOS al HALTCNT se trataba como un byte normal (POSTFLG), la CPU no dormia y el BIOS giraba para siempre en su bucle de halt (`0x344`-`0x374`) en vez de saltar al cartucho. Arreglo: mascara `0x00FFFFFF` (`src/mem.c:481`). Verificado contra el emulador de referencia: la secuencia DISPCNT ahora es identica (`0080, 1002 x108, 9802 x101`), es decir la pantalla se enciende. Incluye ademas: `arm.c` (CPU en halt despierta con IRQ habilitada pendiente), `gba.c` (DISPSTAT con bits vivos VBlank/HBlank/VCount + DMA por linea), `mem.c/h` (recarga de direcciones en DMA repetido + `dma_trigger()`), `win32.c` (cierre de audio sin dejar el proceso colgado; History solo de la version actual). Proceso de publicacion: bump `GBA_VERSION` + README + dialogos de historial -> commit -> push `main` -> `git tag -a v1.4.6` + push (dispara `.github/workflows/build-binaries.yml`) -> `git tag -f Latest` + push -f y reemplazar los assets del release `Latest` via API.

- **RELEASE v1.4.4 (publicado)**: bump `GBA_VERSION` a `v1.4.4` + README + dialogos de historial (main.c/win32.c). Incluye el fix del barrel shifter (`fa80359`, ya en main). Binarios verificados localmente: Linux (make, SDL2), win64 PE32+ y win32 PE32, ambos subsistema GUI=2 y **sin** SDL2 (solo KERNEL32/GDI32/D3D9/WINMM). Proceso de publicacion identico a v1.4.3: commit -> push `main` -> `git tag -a v1.4.4` + push (dispara `.github/workflows/build-binaries.yml`) -> `git tag -f Latest` + push -f y reemplazar los assets del release `Latest` via API.

- **FIX (persistencia del ini, sin versionar aun)**: dos quejas del usuario ("el `boycottadvance-reborn.ini` no se escribe" y "Skip BIOS Boot no se guarda al desmarcarlo"). Causa raiz: `cfg_save()` usaba una ruta **relativa** (`"boycottadvance-reborn.ini"`), asi que el archivo caia en el CWD del proceso (p.ej. el directorio de un acceso directo), no junto al exe, y ademas solo se escribia en una salida **limpia**. **Arreglos** (`src/win32.c`, y `src/main.c` por paridad del formato): (1) `ini_path()` resuelve la ruta junto al ejecutable con `GetModuleFileNameA`, con reserva al CWD (`cfg_load_best`/`save_cfg`); (2) `cfg_save()` escribe de forma **atomica** (`.tmp` + `MoveFileExA(REPLACE_EXISTING|WRITE_THROUGH)`) y devuelve exito; (3) `save_cfg()` se llama tras **cada** cambio (`gui_cb_action`, `gui_cb_keyset/joyset`, `on_key_assign`/`on_joy_assign`, `gui_cb_setbios`) y al salir, asi el ajuste nunca se pierde aunque se cierre a la fuerza; (4) se persisten campos que se conmutaban pero nunca se guardaban: `SmoothEmulation`, `ScaleMode`, `ReverseStereo`. Verificado: builds win32 x86_64/i686 limpios; prueba funcional del formato/round-trip y de la escritura atomica (sin `.tmp` residual).
- **CAUSA PERCIBIDA del 2do bug (RESUELTO en v1.4.6)**: al desmarcar `Skip BIOS Boot` con un dump de BIOS real, `gba_reset()` arranca el BIOS en `0x00000000` y este **no termina**: giraba en la rutina de descompresion del BIOS (`0x1060`-`0x10E8`) al menos 40000 frames emulados (verificado con `/tmp/biosdiag.c`). La causa era el mismo bug de HALTCNT de v1.4.6: el bucle de `Halt`/`IntrWait` del BIOS (`0x344`-`0x374`) escribia HALTCNT pero la mascara de deteccion era incorrecta, asi que la CPU nunca dormia y el BIOS giraba indefinidamente. Con la mascara `0x00FFFFFF` el BIOS entrega el control al cartucho (secuencia DISPCNT identica a la referencia).

- **RELEASE v1.4.3 (publicado)**: `main` = `eacce91`; tag `v1.4.3` y tag `Latest` apuntan a ese commit; releases `v1.4.3` y `Latest` (id 415506383 / 413237927) con los 5 assets (linux-x86_64, macos, win32.zip, win64.zip, src.tar.gz). Proceso de publicacion: bump de `GBA_VERSION` + README + dialogos de historial -> commit -> push a `main` -> `git tag -a v1.4.3` + push (dispara `.github/workflows/build-binaries.yml`, que compila y crea el release) -> `git tag -f Latest` + push -f y reemplazar los assets del release `Latest` via API. Los binarios Windows se verificaron localmente con mingw-w64 (x86_64 e i686, subsistema GUI=2, sin SDL2).

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
- **PENDIENTE (pantalla blanca Pokemon Esmeralda) - causa raiz localizada**: el juego **si** escribe DISPSTAT, pero por su mecanismo de escrituras diferidas: el helper `0x080010B4` guarda el valor en una cola (tabla en `0x03000818`, con `0x03000878` como lista de pendientes) salvo que `DISPCNT & 0x80` (forced blank) este puesto o `VCOUNT` este en la ventana de VBlank (comprueba `0x04000006` contra el rango 0xA1..0xE0, i.e. 161..224). La cola la vacia el handler de VBlank del juego (`0x08000788`, que llama a `0x0800103C`). En fastboot en frio `VCOUNT`=0 y `DISPCNT`=0, asi que la escritura queda **encolada y nunca se aplica**, y como el bit 3 de DISPSTAT (habilitacion de VBlank) nunca llega al registro, el handler que la vaciaria nunca corre: deadlock circular. Verificado: forzando `DISPSTAT|=8` el juego aplica `DISPSTAT=0x9628` (pc `0x0800104E`/`0x08001058`) y el flag de espera `[0x030022DC]` pasa a 1.
- **RESUELTO (bucle BIOS `0x120`)**: el `MSR` a CPSR cambiaba el modo pero **no conmutaba los registros bancados** (`r13`/`r14`), asi que el `bx r0` del BIOS heredaba `lr`=0 del banco SYS. Fix en `src/arm.c` (`arm_exec_at`): el caso MSR-to-CPSR pasa ahora por `change_mode()` cuando cambia el modo, igual que un retorno de excepcion. Verificado con BIOS real: el BIOS cruza `0x120`, hace su init y avanza por la descompresion (`0x1074`-`0x10E4`, `r0` avanzando).
- **RESUELTO (deadlock DISPSTAT, opcion c)**: `gba_reset()` (fastboot) replica ahora el estado que deja el BIOS real tras el POST: `DISPCNT|=0x80` (forced blank), `DISPSTAT|=0x0008` (VBlank IRQ), `IE|=0x0001`, `IME=true`, `POSTFLG=1` y el vector IRQ por defecto del BIOS (`0x03FFFFFC`=0x300, solo si hay dump de BIOS mapeado). La pieza **critica** es `DISPCNT=0x80`: el helper de cola del juego (`0x080010B4`) solo aplica las escrituras de display si hay forced blank o esta en VBlank, y el BIOS real deja forced blank puesto. Con ese estado el juego aplica `DISPSTAT=0x9628`, arma sus propias IRQ (`IE=5`, `IME=1`) y ejecuta su bucle principal (`0x0808xxxx`, `0x080Bxxxx`) en vez de reiniciar al BIOS. Verificado por aislamiento: quitar solo `DISPCNT` reintroduce el bucle del BIOS.
- **RESUELTO (arranque del BIOS real)**: con un dump de BIOS real cargado (BIOS habilitado, sin fastboot) el BIOS **ya arranca la ROM** (frame 267). Dos bugs del core lo impedian:
  1. **Thumb formato 8 (offset de registro, opcode 0101) mal decodificado** en `thumb_exec_at` (`src/arm.c`): `STRH` escribia 32 bits (corrompia el byte de complemento del header en IWRAM y desplazaba el resto), y las cargas con signo (`LDRSB`/`LDRSH`) leian 8/16 bits sin extender el signo. El memcpy del BIOS (rutina en `0xB4C`) usa `STRH` con offset de registro para copiar el header del cartucho a `0x03000088`, asi que la verificacion del logo fallaba siempre (153/156 bytes distintos) y el BIOS nunca cedia el control. Ahora se decodifica `b`/signo correctamente: `STRH` escribe 16 bits, `LDRSB`/`LDRSH` extienden el signo.
  2. **HALTCNT** (`src/mem.c`, escritura a `0x04000301`): el bucle `IntrWait` del BIOS escribe HALTCNT para suspenderse hasta la IRQ; sin manejarlo el CPU nunca entra en Halt y el BIOS se quedaba en su spin. La condicion previa (`addr & 0xFFFFFF`) no podia coincidir nunca porque enmascaraba justo el nibble del registro.
  - Aislamiento: con solo el fix de HALTCNT el BIOS avanza hasta `0x10D4` pero no arranca; el fix de Thumb formato 8 es el que completa el arranque. Ambos son necesarios.
  - Verificado: BIOS real -> `BOOTED to ROM at frame 267, pc=0x082E7DCE`; fastboot -> frame 1. La IRQ de VBlank se entrega al handler del juego (`0x03002750`) y su bucle de espera `0x080008C6` entra y sale una vez por frame.
- **FIX COMMITEADO (nucleo, barrel shifter ARM)**: `barrel_shift()` aplicaba la semantica del desplazamiento **inmediato** a los desplazamientos **por registro**: trataba un conteo 0 como "32" (LSR/ASR #32) o RRX (ROR #0). En ARM, un desplazamiento por registro con conteo 0 **no desplaza** y deja el carry intacto. Consecuencia observable: la rutina de descompresion del BIOS (`0x0FB4`-`0x1000`, `LSR ip, fp, r4` con `r4=0`) corrompia el primer par de bits de cada palabra al escribir VRAM (p.ej. `0x060000A4`: `0x01010200` en vez de `0x01010203`), asi que el descompresor producia datos erroneos y la init del juego divergia. Detectado comparando **todas** las escrituras de I/O (direccion+valor) contra la implementacion de referencia (`amyanger/gba-emulator`): la primera divergencia estaba en la escritura #99742, en el frame 1, y era puramente de computo de CPU (99741 escrituras previas identicas). Fix: parametro `bool reg` en `barrel_shift()`; si `reg && amt==0`, devuelve el valor sin desplazar. Los tres call sites: `get_operand` (registro -> `true`, inmediato -> `false`) y single-data-transfer (`false`, es inmediato de 5 bits). El path Thumb ya lo hacia bien (format 1 y format 4 comprueban `sh==0`). Verificado: `-Wall -Wextra` limpio y el primer diff de I/O se elimina.
- **PENDIENTE (pantalla blanca, resto)**: con lo anterior el juego ya no se cuelga ni reinicia, pero **aun no enciende el display** (`DISPCNT` termina en 0 y no hay escrituras a VRAM/paleta/OAM). Es un problema independiente: el juego sale de su spin de VBlank una vez por frame (`0x080008C8`) y corre su bucle principal, pero no llega a poner `DISPCNT` con capas activas. Trazas PC de ROM desde el frame 268: la referencia entra al `main()` de la ROM (`0x08000000`, `0x08000204`, `0x08000238`, `0x080003A4`) y sigue por `0x082EE834`, mientras la nuestra sigue en el bucle de espera VBlank. Siguiente paso: comparar el estado de IWRAM/EWRAM al frame 268 (en nuestra ejecucion hay ~7852 bytes distintos, incluidas tablas de dispatch y colas de escritura sin inicializar) para localizar el siguiente bloqueo.

- **Nota de reproduccion**: en este entorno `/tmp` se limpia entre comandos largos; recompilar el harness antes de usarlo. `capstone` puede desaparecer del `python3` por defecto y hay que reinstalarlo (`pip install capstone`).


## Harness de depuraciĂłn

Compilar: `gcc -std=c99 -O2 -Isrc tools/trace_iwram.c src/gba.c src/mem.c src/arm.c src/ppu.c src/apu.c src/bios.c -lm -o /tmp/pk12`
Ejecutar: `/tmp/pk12 g x "Pokemon Esmeralda.gba"` (o `/tmp/pk12 600 /tmp/pokemon.gba`).
Atrapa la primera exec en IWRAM e imprime ring buffer + registros.