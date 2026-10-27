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

**REGLA DE REFERENCIA (obligatoria)**: para la correccion de registros, timings y
renderizado, la referencia es **GBATEK** (`https://problemkaputt.de/gbatek.txt`),
NO el codigo de mGBA. mGBA se usa solo como comparador de salida (framebuffer),
nunca como especificacion: copiar sus trucos/desvios ha introducido regresiones
(ver v1.4.13 -> v1.4.14). Antes de cambiar un offset o una tabla, citar la linea
de GBATEK y anotar el numero de registro exacto.

**Verificacion de offsets**: contrastar SIEMPRE contra la tabla del "GBA I/O Map"
de GBATEK. Mapa de ventanas correcto: WIN0H `0x040`, WIN1H `0x042`, WIN0V
`0x044`, WIN1V `0x046`, WININ `0x048`, WINOUT `0x04A`, MOSAIC `0x04C`,
BLDCNT `0x050`, BLDALPHA `0x052`, BLDY `0x054`. (El commit 8653e81 de v1.4.13
los desplazo +4 bytes y rompio todas las capas con ventana.)

## RELEASE v1.4.14 (publicado 2026-10-23)

- **Tag** `v1.4.14` = commit `43e704e` (main); tag rodante `Latest` movido al mismo commit. Publicado con 5 assets (release id 422393808 + Latest id 413237927).
- **TAGS Y RELEASES ANTIGUOS BORRADOS** por peticion del usuario (sus compilaciones no funcionan bien): quedan solo las tags `v1.4.14` y `Latest`, y solo los releases `v1.4.14` y `Latest`. Los releases v1.2.0..v1.4.13 y sus tags se eliminaron via API.
- Contenido (todo verificado contra GBATEK):
  - **`src/mem.c` (REGLA DE LA REGRESION)**: restaurados los offsets de ventana. El bloque vuelve a WIN0H/WIN1H/WIN0V/WIN1V = 0x40/42/44/46 y WININ/WINOUT = 0x48/4A. El commit 8653e81 de v1.4.13 lo habia desplazado +4 bytes, corrompiendo cajas de dialogo, menus y fundidos en Pokemon Esmeralda, Medabots AX y Fire Emblem. `mem.c` queda funcionalmente identico a v1.4.12.
  - **`src/apu.c`**: tabla de duty PSG segun GBATEK (`{0x80,0xC0,0xF0,0xFC}`, pulso HIGH en el primer paso). La tabla de Pan Docs (`{01,81,87,7E}`) tenia el pulso desfasado 4 pasos => timbre erroneo en los blips de menu.
  - **`src/ppu.c`**: en modos bitmap 3-5 el tile de OBJ indexa el area de 16K en `0x06014000` con mascara de 9 bits (slot 0-511), sin underflow; modos 0-2 usan 10 bits sobre `0x06010000`.
  - **`src/gba.c`**: el flag VBlank de DISPSTAT se pone en lineas 160..226 (no en la 227).
- **No es bug**: el destello blanco al final de la intro del BIOS es autentico (el BIOS real hace fade a blanco antes del handoff al cartucho). Confirmado: ocurre igual en v1.4.12.
- Binarios: los 3 targets compilan sin warnings (`make`, `make -f Makefile.win32`, `ARCH=i686`). Windows nativo sin SDL2.

## RELEASE v1.4.13 (publicado 2026-10-23)

- **Tag** `v1.4.13` = commit `f11007a` (main); tag rodante `Latest` movido al mismo commit (`git tag -f Latest && git push -f origin Latest`).
- **Publicacion manual de assets**: el workflow `build-binaries.yml` (run 40032122931, disparado por el push del tag) se quedo **atascado >35 min** en el paso "Install mingw and zip" de los dos jobs de Windows (`apt-get` del runner, problema de infraestructura, no del codigo). Los jobs `linux` y `macos` si completaron. Como el release quedaba sin publicar, se publicaron los 5 assets **a mano via API** con los binarios compilados y verificados en local (linux/macos ademas desde los artefactos de CI):
  - `POST /releases` con `tag_name=v1.4.13` -> release id 422374127; 5 assets subidos a `uploads.github.com`.
  - Release rodante `Latest` (id 413237927) renombrado a "Latest (v1.4.13)", borrados los 5 assets viejos (DELETE) y subidos los nuevos.
  - `v1.4.12` y `v1.4.11` marcados `prerelease=true` (queda como estable solo `v1.4.13` + `Latest`).
  - Run atascado cancelado (`POST /actions/runs/{id}/cancel`).
  - **Leccion**: si el runner de GitHub vuelve a colgarse en `apt-get`, no esperar; publicar con los binarios locales (`make`, `make -f Makefile.win32` x86_64 + i686) porque reproducen exactamente los assets del CI (mismo Makefile).
- Contenido:
  - **`src/ppu.c`**: `render_objs` usa `obj_base = (mode >= 3) ? 0x14000 : 0x10000`. En modos bitmap (3/4/5) la VRAM de OBJ empieza en `0x06014000` (GBATEK), asi que los tiles 512-1023 se leen del slot correcto en vez de envolver a `0x06010000`. El commit anterior (67b714d) tenia `0x10000` fijo, incorrecto para bitmap.
  - **`src/mem.c`**: registros de ventana en las direcciones GBATEK (`WININ=0x040`, `WIN0H=0x044`, `WIN0V=0x048`).
  - **`tools/`**: nuevos harnesses de comparacion de framebuffer `synt_our.c` / `synt_ref.c` y `gen_blend_test.py` (genera ROMs sinteticos modo 3/4 con blend/BLDY).
- Verificado: los tres targets compilan **sin warnings** (`make`, `make -f Makefile.win32` y `ARCH=i686`); los PE son subsistema GUI=2 con solo DLLs nativas (KERNEL32/USER32/GDI32/SHELL32/d3d9/winmm/comdlg32/msvcrt, **sin SDL2**).

## RELEASE v1.4.12 (publicado 2026-10-23)

- **Tag** `v1.4.12` = commit `37e2443` (main); tag rodante `Latest` movido al mismo commit (`git tag -f Latest && git push -f origin Latest`). GitHub Actions (`build-binaries.yml`, disparado por el push del tag `v1.4.12`) publico el release `v1.4.12` con sus 5 assets (run 39897449744, success).
- Contenido: **OBJ mosaic implementado en `render_objs` (src/ppu.c)** segun GBATEK ("Mosaic Function", seccion OBJ) y contrastado contra el renderer software de mGBA (`software-obj.c` + `video-software.c`):
  - Activacion por OBJ: `attr0` bit 12 (`Mosaic`).
  - Tamano por OBJ: registro `MOSAIC` bits **8-11** = OBJ H-Size (−1), bits **12-15** = OBJ V-Size (−1); deshabilitado si el OBJ no tiene el bit.
  - **Cuadricula alineada a pantalla** (como mGBA): el bloque se engancha en `outX % mosaicH == 0` y `mosaicY = y - (y % mosaicV)`.
  - Cada pixel es colorizado por el **pixel superior-izquierdo** del bloque que lo cubre (GBATEK).
  - El bloque vertical se engancha por **scanline** y se sujeta a las filas visibles del sprite (`if (mos_y < ypos) mos_y = ypos; if (mos_y >= ypos+bh) mos_y = ypos+bh-1`), igual que el clamp de mGBA (`video-software.c:1043-1049`).
  - Affine OBJ: el transform se muestrea en el **origen del bloque** (screen-aligned).
  - Los mosaics de OBJ no-rotados mantienen h/v-flip correcto.
  - `tools/test_obj_mosaic.c`: test sintetico (OBJ 16x16 con mosaic en X=40,Y=20) que togglea el bit 12 de attr0 y verifica que cambian los pixeles renderizados (CRC mosaic `BA8F8CB0` vs normal `F1ED8DD1`).
  - Sin regresion en los 3 test ROMs: Emerald, Medabots AX, Fire Emblem mantienen registros DISPCNT/BLDCNT/BLDY identicos antes/despues.
- Ademas: fix de `-Wformat-truncation` en `path_abspath` (`src/main.c`: `snprintf("%.*s", outsz-1, ...)`).
- Verificado: `make` limpiо (sin warnings); `make -f Makefile.win32` y `ARCH=i686` compilan **sin warnings** y producen PE32/PE32+ de subsistema GUI=2 con solo DLLs nativas (KERNEL32/USER32/GDI32/SHELL32/d3d9/winmm/comdlg32/msvcrt, **sin SDL2**).

## RELEASE v1.4.11 (publicado 2026-10-22)

- **Tag** `v1.4.11` = commit `134ecab` (main); tag rodante `Latest` movido al mismo commit (`git tag -f Latest && git push -f origin Latest`). GitHub Actions (`build-binaries.yml`, disparado por el push del tag `v1.4.11`) publico el release `v1.4.11` (id 421109179) con sus 5 assets.
- **Assets de `Latest` reemplazados via API** (release id 413237927, renombrada de "Latest (v1.4.10)" a "Latest (v1.4.11)"): borrar los 5 viejos (DELETE por `asset.url`) y subir los de v1.4.11 descargandolos del release nuevo (GET con `Accept: application/octet-stream`). **OJO**: la subida de assets NO va a `api.github.com` sino a `uploads.github.com` (`POST /repos/{owner}/{repo}/releases/{id}/assets?name=...`); a `api.github.com` da 404.
- **Todas las versiones antiguas marcadas como pre-release** via API (`PATCH /releases/{id}` con `{"prerelease": true}`): v1.2.0..v1.4.10 (16 releases). Quedan como normales solo `v1.4.11` y `Latest`.
- **OJO credenciales git**: el remote `origin` llevaba un token antiguo incrustado (`https://ghu_...@github.com/...`) que provoco prompt de password y push rechazado. Fix: `git remote set-url origin "https://${GITHUB_TOKEN}@github.com/Dragonfire51/BoycottAdvance-Reborn.git"`. Si vuelve a pedir password al hacer push, re-aplicar eso. Tambien: si el remote `main` avanzo (commit ajeno, p.ej. "TEST ROMs"), `git fetch origin && git rebase origin/main` antes de push.
- Contenido de la version: cobro de ciclos de CPU para las SWI de BIOS (`CpuSet`, `Div`, LZ77/RL/Huff, `DiffUnFilter`) via `gba_add_cycles()`, cobro de ciclos por transferencia DMA (`mem.c`, GBATEK "DMA Transfers"), y `wait_mask` selectivo para `IntrWait`/`VBlankIntrWait` (solo la IRQ esperada despierta). Verificacion: Medabots AX entra al gameplay en f973 vs f979 de mGBA (antes f18), frames 100-899 100% identicos; Emerald 576/600 frames DISPCNT identicos.

## RELEASE v1.4.10 (publicado 2026-10-22)

- **Tag** `v1.4.10` = `758207e` (main), tag rodante `Latest` movido al mismo commit.
- **Releases**: `v1.4.10` (id 420224236) y `Latest` (id 413237927, renombrado a "Latest (v1.4.10)"), ambos con los 5 assets.
- GitHub ya marca `v1.4.10` como *latest* del repo (el problema de v1.4.1 con el release viejo marcado como latest queda resuelto).
- Contenido: fix de descompresion a VRAM (barras verticales de Esmeralda) + los fixes de CPU ya en el arbol (FIQ bancado r8-r12, `user_reg()` para LDM/STM con bit S, MUL{S}/MULS solo N/Z, retorno por SPSR, MSR SPSR bloqueado en USR) + escalado con aspecto 3:2.
- **OJO al versionar**: v1.4.9 ya estaba publicado (keypad + clamp de ventanas), asi que este release es v1.4.10. Antes de hacer bump, comprobar `git tag` y `/releases`: si el tag existe, NO reescribirlo.
## FIX velocidad Medabots + sleep selectivo (SESION 2026-10-22) — "Medabots demasiado rapido"

**Sintoma reportado por el usuario**: Medabots AX Rokusho entraba al gameplay en ~f18
(decenas de veces mas rapido que mGBA, que llega a f979).

**Causa raiz**: el nucleo **no cobraba ciclos de CPU por las SWI de BIOS ni por las DMA**.
Las SWI de descompresion ("SWI 0x12" de Medabots, que copia ~50KB a VRAM/EWRAM) terminaban en
casi 0 ciclos y las **DMA** (ROM->EWRAM de las cabeceras, fills de VRAM) escribian al instante
sin avanzar `g->cycle`. Con todo el arranque "gratis", el juego pasaba 18 frames emulados y
entraba al gameplay.

**Arreglos** (GBATEK como referencia):
- `src/bios.c`: costes de ciclos por SWI — `CpuSet` (30+12*n), `Div` (4+13*loops+7), LZ77/RL/Huff
  (20+6*total), `DiffUnFilter` (16+8*total). Nueva llamada `gba_add_cycles()` (src/arm.c) para
  cargar ciclos desde el HLE del BIOS sin acoplarse a `arm_execute`.
- `src/mem.c` `dma_run()`: cobra `count` ciclos (unidad 32-bit) o `count*2` (16-bit), GBATEK
  "DMA Transfers".
- `src/gba.h`/`src/arm.c`/`src/gba.c`/`src/mem.c`/`src/bios.c`: reemplazo de `bool intr_wait`
  por `uint16_t cst.wait_mask`. **Solo la SWI que se espera despierta a la CPU**: `swi_intr_wait`
  (0x04) pone `wait_mask=r1&0x3FFF`; `swi_vblank_intr_wait` (0x05) `wait_mask=IF_VBLANK`;
  `Halt`/`Stop`/HALTCNT directo ponen `wait_mask=0` (cualquier IRQ despierta, GBATEK). En
  `arm_execute()`, una CPU halteada con `wait_mask!=0` solo despierta cuando el flag esperado
  llega; si no, duerme el resto del frame. Antes, cualquier IRQ (p.ej. timer/serial) hacia
  reintentar el bucle del BIOS una vez por scanline y el reloj percibido se destemplaba.
- `tools/headharness.c`/`tools/mgbaharness.c`: comparacion frame a frame de DISPCNT, dumps de
  VRAM/paleta/OAM y render por capas (OBJ-only/BG-only) contra mGBA.

**Verificacion (differ contra mGBA, mismos ROMs)**:
- Medabots: transiciones DISPCNT `mgba [(0,0000),(18,0140),(596,3140),(976,3040),(979,3F40)]`
  vs `nuestro [(0,0000),(15,0140),(591,3140),(971,3040),(973,3F40)]` — el arranque ya entra al
  gameplay en f973 (antes f18), frames 100-899 **100% identicos**, 0-99 al 97% y 900-999 al 92%.
  El "demasiado rapido" queda resuelto.
- Emerald: `mgba [0080@0,0140@12,1F40@207]` vs `nuestro [0080@0,0140@11,1F40@186]`; frames
  300-599 **100% identicos** (576/600 globales).
- gbatests (`/workspace/gbatests`, fleroviux): arm/bios/thumb/nes/unsafe/save al 99.6% y
  memory/ppu al 100% de VRAM identica a mGBA — sin regresiones de CPU/SWI/DMA.
- Los tres targets compilan limpios (Linux `make`, win64/win32 `Makefile.win32`, subsistema GUI,
  sin SDL2). OJO: `make -f Makefile.win32 clean` borra ambos .exe y los `src/*.o` estan
  compartidos con make de Linux — alternar builds sin `clean` produce objetos contaminados
  (errores de link con SDL en win32); siempre `clean` entre targets. Nota de entorno: este
  sandbox pierde /tmp, los mingw-w64 y pkg-config/libsdl2-dev entre sesiones; reinstalar con
  apt si `make`/`Makefile.win32` fallan por toolchain ausente, y para el harness mGBA enlazar
  `-L/usr/lib/chromium -lGLESv2 -lEGL` con symlinks versionados `libGLESv2.so.2`/`libEGL.so.1`
  (libmgba.so referencia GLES2 aunque solo usemos el core) + `--unresolved-symbols=ignore-in-shared-libs`.
- La causa percibida "Medabots graficos corruptos" (BG regs leyendo 0000) era un falso positivo
  del harness (leia `g->io[0x008]` en vez de `g->ppu.bgcnt[]`); los registros BG/scroll/rot
  coinciden ya con mGBA y el renderer por capas pinta contenido de ambos tipos.

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
## Verificacion de fidelidad de render con test ROMs (cinema de mGBA) - 2026-10

- Metodo: `tools/headharness.c` (concluye `dump <dir>`) vuelca `screen.bin` (RGB555), `vram.bin`, `pal.bin`, `oam.bin`. Comparar con PPM de mGBA (`tools/refharness2` pinta PPM) o con los `baseline_*.png` de `/workspace/mgba/cinema/` (decodificar PNG RGB con zlib+struct, sin PIL).
- Medabots AX y Pokemon Esmeralda a frame 10: **0.00% diffs** (38400/38400) — el render del PPU es fiel.
- **RESUELTO (OBJ tile base en modos bitmap)**: `render_objs` usaba `obj_base=0x14000` (`mode>=3`); GBATEK fija OBJ VRAM en `0x06010000` (0x10000) siempre, y en modos bitmap el tile 512+ mapea a `(tno-512)*32`. Fix: `obj_base=0x10000` + `if (mode>=3) slot-=512`. No rompe Medabots/Emerald (0.00%).
- **Divergencias menores observadas aunque sin impacto en juegos comerciales** (casos limite de la suite cinema de mGBA, solo `test.gba` existentes: `obj/2d-wrap`, `blend/disabled-bg-semitrans-blend`, `irq/keyirq`):
  - `2d-wrap`: 128 px (0.33%) de delta de color en un sprite envuelto (rojo vs verde del baseline). No es bug claro: el baseline no esta alineado al frame (el test anima VRAM/paleta), el sprite se renderiza en posicion y forma correctas.
  - `blend/disabled-bg-semitrans-blend`: fondo modo4+fade coincide (diferencias de +-1 canal = artefacto de tone-map 555<->888), pero ~800 px de OBJ colorido (verde/rojo) que mGBA dibuja y nosotros no, con VRAM OBJ supuestamente vacia: sospechoso de pagina del bitmap o prioridad OBJ/BG en modo 4. PENDIENTE de resolver.
- Recordatorio: convertir RGB555 a RGB888 con `(c&31)<<3|(c&31)>>2` para comparar con baselines PNG; un delta de 1 en 8-bit NO es divergencia real de render.

## FIX 2026-10: direcciones de los registros de ventana (WINx) en mem.c

- **Direcciones correctas** (verificadas contra GBATEK y contra mGBA,
  `include/mgba/internal/gba/io.h:51-56`): WIN0H=0x040, WIN1H=0x042,
  WIN0V=0x044, WIN1V=0x046, WININ=0x048, WINOUT=0x04A. Esto es lo que
  implementa `src/mem.c` hoy (reads y writes).
- **OJO - esta seccion antes afirmaba lo contrario.** Documentaba como
  "GBATEK" el mapeo WININ=0x040, WINOUT=0x042, WIN0H=0x044, WIN1H=0x046,
  WIN0V=0x048, WIN1V=0x04A. Ese mapeo es **incorrecto** y es exactamente la
  regresion que introdujo el commit 8653e81 (v1.4.13) y que se revirtio en
  v1.4.14 (ver la seccion RELEASE v1.4.14). No re-aplicar ese cambio.
- Impacto de la regresion: con los offsets desplazados, la ventana salia
  deformada (x/y intercambiados) y podia aplicar blend/BG wrong dentro de
  WIN0, corrompiendo cajas de dialogo, menus y fundidos en Pokemon Esmeralda,
  Medabots AX y Fire Emblem.
- Estado actual: Medabots AX 0.00%, Pokemon Esmeralda 0.00%, FE identico a
  previo (screen 0% diff). Test 2d-wrap sin regresion (sigue alineado al frame).

## 2026-10: la comparacion por frame absoluto con mGBA exagera las divergencias

- **Hallazgo clave**: el harness de mGBA (`tools/mgbaharness.c`) va *sistematicamente*
  por detras del nuestro durante las transiciones de carga. Medido:
  - Pokemon Esmeralda: nuestra f900 == mGBA f920 (6150 px); nuestra f900 vs mGBA f900 = 27283 px.
  - Medabots AX: nuestra f600 == mGBA f606.
  - En Esmeralda, mGBA congela la pantalla de f744 a f760 y luego se recupera;
    nosotros arrancamos el cambio en f749. No es un bug nuestro: es la misma
    transicion de carga reproducida a distinta velocidad (DMA/BIOS).
- **Ambos reconvergen**: Medabots AX da 0.00% exacto en f300-f410 y en f460-f570;
  Esmeralda 0.00% en f680-f748. Por tanto un diff grande en un frame concreto NO
  implica un bug de render: hay que buscar el frame equivalente del otro emulador.
- **Regla practica**: antes de perseguir un diff, comparar nuestra fN contra mGBA
  f(N-k) para k en un rango (p.ej. 0..30) y quedarse con el minimo. Si el minimo es
  ~0, es desfase de fase, no divergencia funcional.
- **Correccion de un diagnostico previo**: el supuesto "byte-swap de OAM bytes 6/7"
  (nosotros `00 66` vs mGBA `66 00`) era un artefacto de comparar contra un frame
  de mGBA congelado/obsoleto. La ruta de escritura de OAM en `src/mem.c` es
  little-endian correcta (`wr16_al`/`wr32_al`, 8-bit ignorado en case 0x07).

## 2026-10: build nativo Win32 sin SDL (x64 + x86)

- `make -f Makefile.win32` (x86_64) y `make -f Makefile.win32 ARCH=i686` (i686).
- Los objetos van a `build/$(ARCH)/`: antes ambas arquitecturas compartian `src/*.o`
  y el link de i686 fallaba con "file format not recognized" al enlazar objetos x86_64.
- Verificado con MinGW: los .exe importan solo comdlg32, d3d9, GDI32, KERNEL32,
  msvcrt, SHELL32, USER32 y WINMM. Cero simbolos SDL. `-Wall -Wextra` sin warnings.

## 2026-10: audio nativo Win32 - waveOutWrite fuera del callback

- `wave_proc` (WOM_DONE) llamaba `waveOutWrite` dentro del propio callback. MSDN
  prohibe invocar cualquier funcion waveOut desde un callback; varios drivers
  corrompen el stream -> el tipico crujido/distorsion del audio nativo.
- Arreglo: el callback solo hace `SetEvent(done)` (evento auto-reset); un hilo
  dedicado mezcla el siguiente fragmento y lo reencola. `audio_close` para el hilo
  antes de `waveOutReset` para que el drain no compita con el reencolado.

## 2026-10: Esmeralda - el diff de la transicion es desfase, no render

- Con VRAM/paleta/registros identicos, el diff de f190+ es fase: contenido identico
  al alinear con delta +1..+2, y va creciendo hasta ~15 frames (mGBA congela la
  pantalla de carga mientras nosotros avanzamos). Confirma la regla de fase anterior.

## 2026-10: suite de regresion gba-tests (baseline 73/133)

- `tools/gba_test_suite.py` ejecuta alyosha-tas/gba-tests contra el harness headless
  y decodifica el veredicto en pantalla. El ROM expande cada bit de glifo a un byte
  0/1 en VRAM (modo 4 BG2); el decoder debe replicar eso, no comparar el bitmask.
- Baseline actual: PASSED 73 / FAILED 51. Los fallos NO son de render: son precision
  de ciclos/pipeline (IRQ, DMA, timer, LDM/STM con S, POP vacio). Mismo estado que
  antes; el suite solo aporta visibilidad, no una regresion nueva.
- `ppu/hello` decodifica "Hello world!" -> el decoder es correcto.

## 2026-10: Medabots AX (intro f300) - sin bug de render

- A f300 OAM y EWRAM son identicos a mGBA (diff 0.00%); VRAM/pal/IWRAM difieren
  1-3%, coherente con desfase de fase de carga, no con corrupcion de tiles.
- La pantalla de intro se renderiza correctamente (texto + logos).

## 2026-10: GBATEK fix flash data# polling (suite 73 -> 75)

- GBATEK ("GBA Cart Backup Flash ROM") especifica que tras *Write Single Data
  Byte* la lectura de `[E00xxxxh]` devuelve el byte escrito (wait until
  `[E00xxxxh]=dat`), y tras *Erase* devuelve FFh. El codigo anterior devolvia el
  byte con el **bit 7 invertido durante 64 lecturas** tras cada program/erase
  (`f->settle`), lo que corrompia el valor que el ROM lee de vuelta.
- Eliminado `settle` de `flash.c`/`gba.h`. `save/flash64` y `save/flash128`
  pasan. Baseline del suite: **75 PASSED**.

## 2026-10: ARM7TDMI SPSR y bit 4 del campo modo (psr 001 -> 008)

- GBATEK (CPSR/SPSR): *"Only the SPSR of the current mode can be accessed; In
  User and System modes no SPSR exists."* `MRS SPSR` en User/System devuelve el
  CPSR; `MSR SPSR` se ignora (`has_spsr()` en `arm.c`).
- El bit 4 del campo modo se lee siempre como 1 para las codificaciones
  invalidas `0xxnnb`: `MSR cpsr_c, 0x03` cae en Supervisor (psr test 004). Un
  valor de modo 0 se deja intacto (es el modo de arranque y debe seguir siendo
  privilegiado, arm `psr_transfer` test 252). `change_mode()` fuerza `|= 0x10`
  solo si el modo != 0.
- `psr/psr` pasa de fallar en 001 a 008 (lo restante es timing de ciclos).
  Baseline del suite se mantiene en 75.

## 2026-10: comparador mGBA vs BoycottAdvance (verdad de base)

- `tools/mgbaharness.c` -> binario `/tmp/mgh` (enlazar con
  `-DENABLE_VFS -DENABLE_DIRECTORIES -I/workspace/mgba/include
  -L/workspace/mgba/build-nogl -lmgba`, ejecutar con
  `LD_LIBRARY_PATH=/workspace/mgba/build-nogl DUMPDIR=<dir>`; vuelca
  `<dir>/vram.bin`, que hay que copiar a `<dir>.vram.bin` para el parser).
- Script de comparacion: ejecuta cada ROM en ambos emuladores y clasifica
  REAL BUG (mGBA pasa, nosotros fallamos) / REGRESION (nosotros pasamos) /
  PARIDAD (ambos fallan).
- **Hallazgo**: los 8 fallos `LDM/*` (`LDM_ALU` 002, `LDM_ALU_IMM` 002,
  `LDM_ALU_Store` 006, `LDM_Bx` 001, `LDM_LD` 002, `LDM_MUL_UL_32` 002,
  `LDM_MUL_UL_SL` 002, `LDM_Swap` 002) son **paridad con mGBA**: el emulador de
  referencia falla exactamente los mismos subtests. No son bugs nuestros; el
  suite (alyosha-tas) prueba un quirk de hardware que ninguno de los dos
  implementa. No malgastar esfuerzo ahi.

## 2026-10: coste real de bus del DMA (GBATEK 2N+2(n-1)S+xI)

- `dma_run()` cobraba `1 ciclo/32-bit` y `2/16-bit` (constante) al bus y no
  tocaba los timers. GBATEK (GBA DMA Transfers, "Transfer Rate/Timing") define
  `2N + 2(n-1)S + xI`: el `2` es por unidad (no por acceso), `N/S` son los
  waitstates non-sequential/sequential de la region, y el `I` es el ciclo
  interno del bus de 16 bit que obliga a partir un acceso de 32 bit en dos.
- Implementado en `src/mem.c`: `dma_region()` clasifica por direccion
  (BIOS/IWRAM/IO/PAL/VRAM/OAM sin waitstates, EWRAM=2 en reset, ROM segun WS0
  de WAITCNT), `dma_access_cycles()` y `dma_total_cycles()` suman el coste
  first/rest. El DMA ahora tambien alimenta a los timers con esos ciclos
  (`io_update_timers(g, dma_cyc)`), que es lo que GBATEK documenta: los timers
  siguen contando mientras el DMA tiene el bus.
- Efecto: `timing/dma_long` pasa de FAIL 001 a **PASS**. Los dos `DMA/DMA_ROM_*`
  cambian de codigo de fallo (009 -> 172/108) porque ahora el coste de ROM
  (WS0=4) se modela; siguen fallando por el resto del timing de pausa.
- `prefetcher/prefetcher_dma` deja de dar PASS **espurio**: leia TM0CNT_L y
  obtenia 0 porque los timers solo se actualizaban por scanline, no por
  instruccion. Con el DMA cobrando a los timers el valor ya es distinto de 0 y
  el test revela el fallo real. Baseline neto: **75 PASSED** (dma_long arreglado,
  prefetcher_dma expuesto).
- Los tres juegos objetivo (Pokemon Esmeralda, Fire Emblem, Medabots AX) siguen
  siendo **bit-identicos** frame a frame tras 900 frames (screen/vram/pal/oam),
  es decir, el cambio no altera el render.
- **Causa raiz pendiente**: los timers solo avanzan en bloques de scanline
  (`gba.c` pre-carga `visible_cycles`/`hblank_cycles` antes de ejecutar la CPU),
  asi que un timer habilitado a mitad de scanline no ve los ciclos de CPU que
  consumen las instrucciones.
- **Experimento 2 (2026-10-15, descartado)**: se probo un *watermark* por timer
  (`last_cycle` por timer + `io_sync_timers(g)` que avanza desde esa marca hasta
  `g->cycle`) con sync en `arm_execute` y en la lectura de `TMxCNT_L`. Modelo
  correcto segun GBATEK, pero la suite cae de **75 a 45 PASSED** con 31
  regresiones y **0 mejoras** (`prefetcher/prefetcher_boundary_*`,
  `prefetcher_branch_thumb*`, `fifo_dma/fifo_2..5`, `DMA/DMA_Mode_Change`,
  `arm/Coprocessor_14`, `ppu/start_up_vbl`, `irq/BL_IRQ_R14`, ...). Los juegos
  (Esmeralda, Fire Emblem) siguen bit-identicos, asi que no hay ganancia visual.
  Motivo: el resto del modelo de ciclos es aproximado (1 ciclo por instruccion,
  sin waitstates de ROM ni pipeline), asi que adelantar la visibilidad de los
  timers desajusta el orden relativo IRQ/instruccion en decenas de tests.
  Conclusion: **no se puede granular los timers sin antes modelar los ciclos de
  bus** (`add_cycles` ARM/Thumb con waitstates de ROM, prefetch buffer y el
  ciclo interno de 32 bit). Ese es el prerrequisito, no un cambio aislado.
- **Verificado**: los "PASS" de `dma_from_bios`, `cpy_data_bios` y
  `prefetch_enable` en el baseline son **espurios** igual que `prefetcher_dma`:
  el handler de fallo hace `mov r12, r0` y, como el timer lee 0 (nunca avanzo),
  `r12 == 0` se interpreta como "All tests passed". El baseline real es 71
  legitimamente + 4 espurios.
- **Hito SDL2 (Win32) y D-pad POV**: ambos ya estan en `main`
  (`22cb833` no-SDL + `a8c709f`/`862882c` D-pad). `Makefile.win32` no enlaza
  SDL2; `video.c` solo usa SDL en el path no-Windows y GDI/D3D9 en Win32.

## 2026-10-15 (sesion de verificacion): estado real de Fire Emblem y correccion

- **Correccion de un dato erroneo previo**: la afirmacion "los tres juegos
  objetivo son bit-identicos frame a frame tras 900 frames" es **falsa**. Con el
  comparador real (`/tmp/hhm`, mGBA + BIOS oficial) Fire Emblem difiere:
  - fastboot (por defecto): f300 19.86%, f500 95.06%, f900 95.21%.
  - BIOS real (`FASTBOOT=0 BIOS=gba_bios.bin`): match **1.08%** hasta f353,
    salto a **99.99%** en f354.
- **El hito SDL2/Win32 ya esta completo y publicado**: `Makefile.win32` compila
  x86_64 e i686 **sin SDL2** y el PE importa solo
  `comdlg32/d3d9/GDI32/KERNEL32/msvcrt/SHELL32/USER32/WINMM`. No hay nada
  pendiente de commit+push de esa parte (commits `22cb833` no-SDL, `a8c709f`
  D-pad, `862882c` merge; todos en `main` y en `origin`).
- **Sintoma de FE**: en f354 mGBA hace un fade de transicion (pantalla a negro y
  luego a blanco); nosotros nos quedamos en el texto del menu y solo empezamos a
  cambiar ~f420, con la transicion degradada.
- **BLDCNT/BLDALPHA**: mGBA `4000050=3F68`, `4000052` sube 0010->0110->0210;
  nosotros `bldcnt=2020 blda=0000` clavado. El juego si escribe 3F68/0310.. (pc
  0x0800122C/0x08001234), pero el valor final que queda en el registro es 2020.
- **Estado IRQ/registros**: mGBA `dispstat=0019 if=0001 ie=2003 ime=0`;
  nosotros `dispstat=0018 if=0002 ie=2003 ime=1`. Diferimos ya en f340 (no solo
  en f354): la divergencia de registros es gradual, la de pantalla es un evento
  agudo en f354.
- **Descartado en esta sesion** (sin efecto sobre la divergencia):
  `HDRAW=1008/HBLANK=224` (mGBA) en vez de 960/272; VBlank IRQ en linea 160 en
  vez de 159; `BA_NO_PREARM=1`. Los tres dejan f354 en 99.99%.
- **Ciclos**: ~281139 por frame vs 280896 de hardware (+243, ~0.09%). El modelo
  de ciclos (1 ciclo/instruccion, sin waitstates de ROM ni pipeline) sigue siendo
  el **prerrequisito real** documentado abajo; sin el, ni el sync fino de timers
  ni la paridad frame a frame con BIOS real son alcanzables.

## 2026-10-15 (analisis de glitches): el glitch NO es del PPU (demostrado)

Objetivo: glitches visuales en Groudon, Kyogre, post-Rayquaza y el Profesor de
Pokemon Esmeralda; analisis cuadro a cuadro e investigacion de ppu/apu/dma/thumb/ARM.

**Conclusion verificada: la logica del PPU es correcta; la causa es el modelo de
ciclos del CPU (sin waitstates de ROM ni pipeline), el prerrequisito ya
documentado.** Evidencia reproducible:

1. ROM sintetica de modo 4 (`python3 tools/gen_blend_test.py` ->
   `tmp_t2_noblend.gba`): rellena las filas 0-79 con el indice de paleta 3 (rojo)
   y las 80-159 con el 4 (verde), sin blend. Comparada en fastboot en ambos
   emuladores: frame 3 diff 19200 px (50%), medias identicas (85.0), la mitad
   inferior discrepa (nosotros verde, mGBA rojo).
2. Volcados en el mismo frame: **VRAM, PAL y OAM byte-identicos** (0 bytes de
   diferencia). Con el estado de mGBA inyectado en nuestro PPU (`renderstate`,
   `dispcnt=1404`, su VRAM/PAL/OAM) nuestra salida es exactamente la que dice el
   VRAM (rojo arriba, verde abajo) y coincide con nuestro propio frame: **nuestro
   PPU renderiza correctamente el VRAM de mGBA**.
3. El frame que mGBA *muestra* (rojo tambien abajo) contradice su propio VRAM:
   lo dibujo a mitad del relleno. Es una diferencia de **fase dentro del frame**,
   no de logica de render.

Consecuencia: nuestro core completa los bucles de relleno antes que mGBA (1
ciclo/instruccion, sin waitstates de ROM), asi que en un limite de frame el PPU
puede quedar a mitad de una actualizacion y mostrar la mitad vieja/nueva. Lo
mismo explica Groudon/Kyogre/Profesor: con BIOS real y alineacion por frame la
divergencia sigue siendo ~90% y no baja con ningun offset porque el estado ya
diverge antes de las escenas.

Descartado en esta sesion (sin efecto sobre el glitch): `renderstate` como
comparador directo contra el frame de mGBA (Emerald escribe registros a mitad de
frame, un re-render estatico difiere legitimamente); PA/PD que el trace lee como
0x7FFF vs 0x0100 de mGBA (artefacto: BGxPA/BGxX son de solo escritura en mGBA y
el trace los lee por `io_read16`).

## 2026-10-26: verificacion del hito no-SDL (Win32) y del modelo de ciclos

### Hito SDL2/Win32: verificado, sin cambios de codigo pendientes
Reinstalado mingw-w64 (`gcc-mingw-w64-x86-64` / `gcc-mingw-w64-i686` v14) y
compilado el build nativo en ambas arquitecturas:
- `make -f Makefile.win32` -> `boycottadvance-reborn-x86_64.exe`
- `make -f Makefile.win32 ARCH=i686` -> `boycottadvance-reborn-i686.exe`

`objdump -p` del PE confirma los imports: comdlg32, d3d9, GDI32, KERNEL32,
msvcrt, SHELL32, USER32, WINMM — ningun SDL. `video.c` usa GDI/D3D9 bajo
`_WIN32` y SDL solo en el path no-Windows; `gui.c` construye la barra nativa
completa (File/Window/Options/Help) con las opciones del original y sin Debug.
El hito no-SDL ya esta en `main` (`22cb833`) y el D-pad POV tambien
(`a8c709f`): no hay trabajo pendiente de commit+push en esa parte.

### Modelo de ciclos de bus: experimento parcial descartado (75 -> 42)
Se probo `charge_fetch()` (cobra el fetch de opcode de 32 bit con waitstates de
ROM) + tick de timers por instruccion (`io_tick_timers`, marca de agua
`timer_ticked`) quitando el tick por scanline. Resultado: 42 PASSED con 35
regresiones (`prefetcher/*`, `timing/*`, `fifo_dma/*`, `DMA_Mode_Change`,
`arm/Coprocessor_14`, `thumb/Push_no_regs`, ...). No es un defecto de la marca
de agua: los tests de prefetch/timing miden el buffer de prefetch de 8
entradas de 16 bit (GBATEK "GamePak Prefetch"), que `charge_fetch` no modela
(solo exime 1 ciclo cuando WAITCNT.14 y la busqueda es secuencial). El modelo
completo (fetch de 32 bit + buffer de prefetch + ciclo interno de 32 bit) es el
prerrequisito; este intento parcial no debe publicarse. Revertido; baseline
restaurado a 75 PASSED.


## FIX 2026-10-27: mapeo OBJ 2D (test cinema `obj/2d-wrap`)

- **Sintoma**: en el test cinema de mGBA `cinema/gba/obj/2d-wrap` (un OBJ 16x8,
  tile 31, mapeo 2D), la mitad derecha del sprite salia con el tile equivocado:
  el nuestro daba rojo (tile 32, color 3) donde mGBA daba verde (tile 31, color 2).
  64 px de diff (0.33%) con VRAM/PAL/OAM **identicos** entre ambos cores.
- **Causa**: con **mapeo de caracteres OBJ 2D** (`DISPCNT` bit 6 = 0) la columna
  de tile debe **envolver dentro del bloque de 32 tiles** que contiene el tile
  base del sprite; el nuestro dejaba que la columna **acarreara** al bloque
  siguiente (`tno + (tx>>3)*tstep`), leyendo el tile 32 en vez del 31.
  mGBA lo hace con `maskLo = 0x3FE` (no 1D) y `maskHi = charBase & 0x7C00`
  (`src/gba/renderers/software-obj.c`, `GBAVideoSoftwareRendererPreprocessSprite`):
  `(yBase + ((xBase + charBase) & maskLo)) & 0x7FFE`.
- **Fix** (`src/ppu.c`, `render_objs`): solo en 2D (`!one_d`),
  `slot = (base & ~0x1F) + ((base + col*tstep) & 0x1F) + row*32`, con
  `base = tno & ~(pal256?1:0)`. El camino 1D (que usa Esmeralda: `DISPCNT=1F40`,
  bit 6 = 1) queda **intacto**. Formula verificada contra una simulacion del
  direccionamiento de mGBA: 0 discrepancias en 400k combinaciones aleatorias.
- **Verificacion**: `obj/2d-wrap` pasa de 128 px de diff a **0 px** (pixel-perfect).
  Suite `gba-tests` sin regresion (74 PASSED / 50 FAILED / 9 UNKNOWN, identico
  antes y despues). `blend/disabled-bg-semitrans-blend` sigue con 8854 px de diff
  (pre-existente, identico antes y despues; es el bug de blend/target2, no este).
  Builds Linux y Win32 (x64 + i686, sin SDL2) limpios.
- **Nota**: el diff de 1814 px (4.72%) de Esmeralda a frame 400 **no** es este
  bug (Esmeralda usa 1D). Ahi, ademas de OAM (64 bytes) y PAL (6 bytes), divergen
  IWRAM (~3350 bytes) y EWRAM (~137 bytes), y la OAM nuestra tiene 20 palabras
  `0x7FFF` en la zona de parametros affine donde mGBA tiene `0x0100`. Es
  divergencia del **modelo de CPU** (el juego ejecuta codigo distinto), no del
  PPU: por eso la pista del 2D-wrap (que si era PPU puro) se cierra aqui.
- **Herramienta nueva**: `tools/cinema_check.py <testdir>` ejecuta un test cinema
  (necesita `test.gba` + `baseline_*.png`) contra el harness headless y reporta
  el diff por frame en RGB555->888 con replicacion de bits.

## VERIFICACION 2026-10-27: el diff de Esmeralda a f400 NO es del PPU (probado)

Verificacion a fondo de la afirmacion "divergencia del modelo de CPU, no del PPU".
**Conclusion: confirmada, y ahora con prueba dura.** Metodo: inyeccion de estado.

- **Prueba de exoneracion (definitiva)**: `tools/headharness.c` acepta
  `renderstate <prefijo>`, que carga `vram.bin`/`pal.bin`/`oam.bin` + `*.regs`
  (texto `dispcnt=.. bgcnt0=.. bldcnt=..` etc.) y renderiza un frame con
  `ppu_render_frame`. Alimentando nuestro renderer con el estado **exacto de
  mGBA** a f400 (Esmeralda), el resultado es **0 px de diferencia (0.00%)** contra
  el frame de mGBA. Nuestro PPU es **bit-exacto**: no hay bug de render.
  Repetido con las dos rutas de arranque (fastboot HLE y BIOS real): 0 px en ambas.
- **Que difiere realmente** (f400, fastboot): VRAM **0 bytes**, PAL 6 bytes,
  OAM 64 bytes, IWRAM 3360 bytes, EWRAM 140 bytes. Con **BIOS real en ambos**:
  VRAM/PAL/OAM **0 bytes**, IWRAM 117 bytes, EWRAM 1 byte. La diferencia es el
  **estado que el juego ha escrito**, no como lo pinta el PPU.
- **El fastboot HLE amplifica la divergencia**: mismo juego, misma ruta en ambos
  emuladores -> fastboot 3360 bytes de IWRAM vs BIOS real 117 bytes. Nuestro
  arranque HLE no replica exactamente lo que deja el BIOS de verdad (esperado).
- **La divergencia es transitoria, no un bug estatico**: Esmeralda con BIOS real
  da 1334 px (3.47%) de f350 a f436 y **0.00% sostenido de f440 a f452**.
  Es decir, reconverge a pixel-perfect. Un bug de render no se "arregla" solo.
- **Patron espacial del transitorio**: 4 bandas de **exactamente 8 filas**
  (y=48-55, 64-71, 80-87, 96-103, separadas 16 px) en la zona central
  (x=56..183), con el mismo color de fondo alrededor -> escritura de OAM/PAL a
  **mitad de frame** (la CPU escribe en la scanline que no toca). Estable frente a
  desplazamiento de fase (k=0..8 dan el mismo 1334 px), asi que NO es desfase de
  frame: es timing de escritura intra-frame.
- **Medabots AX** (BIOS real) reproduce el mismo patron: f400 con VRAM/PAL/OAM
  **identicos** y 1078 px de diferencia (banda y=65..77). Inyectando el estado de
  mGBA, nuestro renderer reproduce **exactamente** nuestra propia pantalla (0 px),
  lo que confirma que la pantalla es funcion de (vram,pal,oam,regs) y que lo que
  cambia es el **valor/timing** de esos datos, no el renderer.
- **Regla practica reforzada**: antes de atribuir un diff de pantalla al PPU,
  ejecutar la exoneracion por inyeccion de estado. Si da 0 px, el PPU es inocente
  y el diff es de estado/timing de la CPU. `cmp` de VRAM/PAL/OAM no basta: los
  registros (BLDCNT/BLDALPHA/BLDY/mosaic/scroll) y el instante de escritura
  dentro del frame no estan en esos dumps.

## RELEASE v1.4.19 (verificacion PPU + correccion de nota WINx)

- **Version**: `GBA_VERSION` = `v1.4.19` (`src/gba.h`); README + dialogos de
  historial (`src/win32.c`/`src/main.c`) actualizados. **Release de
  verificacion: NO hay cambios de emulacion.** Solo docs + bump de version.
- **Contenido**: (a) la verificacion de la sesion 2026-10-27 que prueba que el
  diff de Esmeralda f400 es de estado/timing de CPU y no del PPU (ver seccion
  "VERIFICACION 2026-10-27"); (b) correccion de la seccion "FIX 2026-10:
  direcciones de los registros de ventana (WINx)" de este mismo archivo, que
  afirmaba como GBATEK el mapeo desplazado (WININ=0x040, WINOUT=0x042,
  WIN0H=0x044, WIN1H=0x046, WIN0V=0x048, WIN1V=0x04A). Ese mapeo es el de la
  **regresion de v1.4.13**, corregida en v1.4.14; la nota podia hacer
  re-introducir el bug. Mapeo correcto (GBATEK + mGBA
  `include/mgba/internal/gba/io.h:51-56`): WIN0H=0x040, WIN1H=0x042,
  WIN0V=0x044, WIN1V=0x046, WININ=0x048, WINOUT=0x04A.
- **Nota**: el build Windows nativo sin SDL2 **ya se publico en v1.4.0**
  (`22cb833`/`b1b0ddc` estan dentro del tag `v1.4.0`), y el fix de WINx ya salio
  en v1.4.14. Este release no los reintroduce ni los "arregla"; los re-confirma.
- **Verificado en local**: `make` (Linux), `make -f Makefile.win32` (x86_64) y
  `ARCH=i686` compilan **sin warnings** con `v1.4.19` embebida; los PE siguen
  siendo subsistema GUI=2 con solo DLLs nativas (sin SDL2). Regresion 2d-wrap
  sin cambios (8 px = artefacto de baseline).
- **Tag** `v1.4.19` = commit `d5cc055` (main); tag rodante `Latest` movido al
  mismo commit (`git tag -f Latest && git push -f origin Latest`).
- **CI** (run `40564019853`): **los 4 jobs + `release` completaron en <1 min**;
  `apt-get` no se colgo. Release `v1.4.19` (id `425860352`) publicado por CI con
  sus 5 assets.
- **Release rodante `Latest`** (id `413237927`) renombrado a "Latest (v1.4.19)",
  borrados los 5 assets viejos (DELETE, 204 sin cuerpo) y subidos los nuevos
  descargandolos de `v1.4.19` con `gh release download --pattern` y
  `gh release upload Latest ... --clobber`. Verificado: el `win64.zip` de
  `Latest` contiene `README.md`, `boycottadvance-reborn.exe`, `gba_bios.bin` y el
  `.exe` lleva `v1.4.19`.
- **Regla del usuario (3 anteriores a pre-release)**: al publicar, marcar como
  `prerelease=true` las **3 versiones anteriores**. En este release:
  `v1.4.18` (id `425033828`), `v1.4.17` (`424985617`) y `v1.4.16` (`424909653`).
  Quedan estables solo `v1.4.19` y `Latest`. Aplicar esto en cada release nuevo.


## RELEASE v1.4.18 (publicado 2026-10-27)

- **Version**: `GBA_VERSION` = `v1.4.18` (`src/gba.h`); README + dialogos de
  historial (`src/win32.c`/`src/main.c`) actualizados. Contenido: el fix del
  mapeo OBJ 2D (commit `f8fc092`, ver la seccion "FIX 2026-10-27: mapeo OBJ 2D").
- **Tag** `v1.4.18` = commit `8011e79` (main); tag rodante `Latest` movido al
  mismo commit (`git tag -f Latest && git push -f origin Latest`).
- **CI** (run `40449632601`, disparado por el push del tag): **los 4 jobs
  (`linux`, `windows (win32)`, `windows (win64)`, `macos`) y el job `release`
  completaron en <1 min**; esta vez `apt-get` **no** se colgo. Release `v1.4.18`
  publicado por CI con sus 5 assets.
- **Release rodante `Latest`** (id `413237927`) renombrado a "Latest (v1.4.18)",
  borrados los 5 assets de v1.4.17 y subidos los de v1.4.18 (mismo nombre de
  fichero, `gh release upload --clobber`). `v1.4.17` marcado `prerelease=true`;
  GitHub ya marca `v1.4.18` como *latest* del repo.

## RELEASE v1.4.17 (publicado 2026-10-27)

- **Version**: `GBA_VERSION` = `v1.4.17` (`src/gba.h`); README + dialogos de
  historial (`src/win32.c`/`src/main.c`) actualizados. Contenido (commit
  `1c973a6`): mezcla del PSG al nivel de mGBA (ver la seccion de diagnostico
  mas arriba, "el PSG sonaba a la mitad"). El "plim!" de seleccion de menu en
  Pokemon sonaba flojo porque (a) el volumen maestro PSG (`SOUNDCNT_H` bits
  0-1) usaba `2^vol/4` en vez de `2^(vol-1)` y (b) la ganancia final era x64
  en vez de x48. Ratio ours/mgba del test PSG sintetico: 0.65 -> 0.98-0.99.
- **Tag** `v1.4.17` = commit `1c973a6` (main); tag rodante `Latest` movido al
  mismo commit (`git tag -f Latest && git push -f origin Latest`).
- **CI** (run `40438698478`, disparado por el push del tag): `linux`,
  `windows (win32)` y `macos` completaron; `windows (win64)` **se colgo en
  `apt-get`** (>23 min, el mismo fallo de infraestructura de v1.4.13/v1.4.15).
  Se aplico la leccion ya documentada: **no esperar**, publicar a mano.
- **Publicacion manual**: se creo `v1.4.17` con `gh release create` y se
  subieron los 5 assets con `gh release upload`. Los binarios Linux
  (`make`), win32/win64 (`make -f Makefile.win32 [ARCH=i686]`) se compilaron en
  local; el de macOS se descargo del artefacto de CI (`gh run download ...
  -n macos`) porque no se puede cruzar-compilar aqui. El job colgado se
  cancelo (`gh run cancel`). Release rodante `Latest` (id `413237927`)
  renombrado a "Latest (v1.4.17)", borrados los 5 assets viejos y subidos los
  nuevos via `uploads.github.com`.
- **`v1.4.16` marcado `prerelease=true`** para que GitHub marque `v1.4.17`
  como *latest* del repo.

## RELEASE v1.4.16 (publicado 2026-10-27)

- **Version**: `GBA_VERSION` = `v1.4.16` (`src/gba.h`); README + dialogos de
  historial (`src/win32.c`/`src/main.c`) actualizados. Contenido (commits
  `93220d1`, `56aa782`, `c96ce9d`, `2ed3c6a`, `bc5c669`): decodificacion de los
  campos de WAITCNT por GBATEK, reloj del FIFO de Direct Sound por timer a
  nivel de muestra, deadlines absolutos por linea en el bucle de frame, arranque
  por BIOS real por defecto (HLE opt-in) con SIOCNT en el fast boot, y sobre
  todo el **resampleo con interpolacion lineal del Direct Sound** (bc5c669) que
  elimina el audio "rayado" (0 saltos >8000 en 5 min vs 3568 antes; mGBA 973).
- **Tag** `v1.4.16` = commit `5571157` (main); tag rodante `Latest` movido al
  mismo commit (`git tag -f Latest && git push -f origin Latest`).
- **CI** (run `40426259110`, disparado por el push del tag): los 4 jobs
  (`macos`, `linux`, `windows (win32)`, `windows (win64)`) y el `release`
  completaron en <2 min — **sin** el cuelgue de `apt-get` de v1.4.13/v1.4.15.
  El job `release` publico `v1.4.16` (id `424909653`) con los 5 assets
  (`linux-x86_64`, `macos`, `win32.zip`, `win64.zip`, `-src.tar.gz`).
- **Release rodante `Latest`** (id `413237927`) renombrado a "Latest (v1.4.16)":
  borrados los 5 assets viejos (DELETE, 204 sin cuerpo) y subidos los nuevos
  descargandolos de `v1.4.16` (GET `Accept: application/octet-stream`) y
  subiendolos a `Latest` (`gh release upload ... --clobber`, va a
  `uploads.github.com`). Verificado: el `win64.zip` descargado de `Latest`
  contiene `README.md`, `boycottadvance-reborn.exe`, `gba_bios.bin` y el `.exe`
  lleva `BoycottAdvance Reborn v1.4.16`.
- `v1.4.15` marcado `prerelease=true`; quedan estables solo `v1.4.16` y
  `Latest`.
- **Binarios locales** (reproducen los del CI, mismo Makefile): `make`,
  `make -f Makefile.win32` y `ARCH=i686`, los tres **sin warnings** (`-Wall
  -Wextra`) y con `v1.4.16` embebida.
- **OJO credenciales git**: el remote `origin` llevaba el token viejo otra vez;
  fix aplicado:
  `git remote set-url origin "https://x-access-token:${GITHUB_TOKEN}@github.com/Dragonfire51/BoycottAdvance-Reborn.git"`.

## RELEASE v1.4.15 (publicado 2026-10-26)

- **Version**: `GBA_VERSION` = `v1.4.15` (`src/gba.h`); README + dialogos de
  historial (`src/win32.c`/`src/main.c`) actualizados. Contenido (commits
  `8bb7802`, `b301fb0`, `64b4919`, `3f7136a`, `257f87c`, `6035c18`): reglas de
  SPSR (GBATEK), latch de prefetch de BIOS al salir de la region + quirk de
  Thumb `POP{}`, coste real de bus de DMA (`2N+2(n-1)S+xI`) cobrado a los
  timers, retirada de la inversion de polling `data#` del flash, primera capa
  del modelo de ciclos de bus (WAITCNT + buffer de prefetch, neutra), y en
  Win32 el refill de waveOut desde un hilo en vez del callback `WOM_DONE`.
- **Tag** `v1.4.15` = commit `58ce8e2` (main); tag rodante `Latest` movido al
  mismo commit (`git tag -f Latest && git push -f origin Latest`).
- **CI colgado**: el run `40374383530` (disparado por el push del tag) se
  quedo **atascado >10 min** en el paso "Install mingw and zip" del job
  `windows (win64)` — el mismo fallo de infraestructura del runner
  (`apt-get`) que en v1.4.13. Los jobs `linux`, `macos` y `windows (win32)`
  si completaron. Run cancelado (`POST /actions/runs/{id}/cancel`).
- **Publicacion manual via API** (leccion de v1.4.13 aplicada: no esperar al
  runner):
  - Release `v1.4.15` creado con `POST /releases` (`tag_name=v1.4.15`) ->
    id `424603696`; 5 assets subidos a `uploads.github.com`.
  - Release rodante `Latest` (id `413237927`) renombrado a "Latest (v1.4.15)",
    borrados los assets viejos (DELETE, 204 sin cuerpo -> no parsear JSON) y
    subidos los 5 nuevos.
  - `v1.4.14` marcado `prerelease=true`; quedan estables solo `v1.4.15` y
    `Latest`.
  - Assets: `linux-x86_64`, `macos` (descargado del artefacto de CI), `win32.zip`,
    `win64.zip`, `-v1.4.15-src.tar.gz`. Verificado: descarga del `win64.zip` y
    `strings` del `.exe` confirma `BoycottAdvance Reborn v1.4.15`.
- **Binarios locales** (reproducen los del CI, mismo Makefile): `make`,
  `make -f Makefile.win32` y `ARCH=i686`, los tres **sin warnings**
  (`-Wall -Wextra`) y con `v1.4.15` embebida.
- **OJO credenciales git**: el remote `origin` volvio a llevar el token viejo
  (`ghu_...`) y el push fallo con "could not read Password". Fix:
  `git remote set-url origin "https://x-access-token:${GITHUB_TOKEN}@github.com/Dragonfire51/BoycottAdvance-Reborn.git"`.


## 2026-10-26 (2): modelo de bus por capas y confirmacion del prerrequisito

### Capa 1 (fetch + prefetch) implementada y neutral: 75 PASSED preservado
Se separo el modelo de ciclos en una capa de bus en `mem.c`/`mem.h` en vez del
`charge_fetch` plano que fallo antes:
- `bus_update_waitstates(g)`: deriva `bus_ns16/s16/ns32/s32[16]` de WAITCNT
  (tablas de GBATEK "Memory Map": SRAM/WS0/WS1/WS2 no-secuencial y secuencial,
  `ns32 = ns16 + 1 interno + s16`, `s32 = 2*s16 + 1`). Se llama al reset y en
  cada escritura de WAITCNT (`io_write16` caso 0x204).
- `bus_fetch_cycles(g, pc)`: coste del fetch. **Clave**: con el prefetch
  desactivado (WAITCNT.14 = 0, el caso de toda la suite salvo `prefetch_enable`)
  cada fetch es 1 ciclo, igual que el baseline; con el prefetch activo, un
  fetch secuencial (PC == `prefetch_pc`) se sirve del buffer (1 ciclo) y una
  rama paga el coste no-secuencial. `arm_exec`/`thumb_exec` cobran
  `bus_fetch_cycles - 1` (el ciclo base ya esta en el total por opcode), y las
  ramas ARM (B/BL/BX) llaman a `bus_prefetch_branch` para invalidar el buffer.
- `bus_data_cycles` queda bajo `#if 0` (documentado): un acceso a datos y el
  fetch de la siguiente instruccion se solapan en hardware, asi que no tiene
  sentido cobrarlo por separado sin el pipeline completo.
- Resultado: suite **75 PASSED / 49 FAILED / 9 UNKNOWN**, identico al baseline
  (0 regresiones, 0 mejoras) y sin warnings nuevos. Efecto medible en juego
  real: Esmeralda pasa de 113735189 a 114041282 ciclos en 400 frames (+0.27%),
  acercandose a hardware (280896/frame), sin cambiar el render.

### Por que el fetch plano no basta (remedido esta sesion)
- Cobrar el fetch **no-secuencial en cada instruccion** cuando el prefetch esta
  off baja la suite a 67 (8 regresiones `DMA_pause_timing_*`,
  `Internal_Cycle_DMA_*`): sin prefetch, los fetches en linea recta siguen
  siendo **secuenciales (S)**, no no-secuenciales (N). Corregido: sin prefetch
  todo fetch cuesta 1 ciclo.
- La visibilidad de timers por instruccion (marca de agua `io_catchup_timers`)
  baja a 46 PASSED. Confirma lo ya documentado: hace falta el modelo completo
  (fetch de 32 bit + buffer de 8x16 + ciclo interno de 32 bit) **y** sustituir
  los cobros por opcode de `arm.c` por el modelo de acceso a datos, no solo
  anadir el fetch. Revertido; el baseline de 75 queda intacto.

### Prerrequisito exacto (mGBA, `/workspace/mgba`)
El modelo mGBA no cobra un "fetch" aparte: `LOAD_32` del opcode y el acceso a
datos comparten los mismos contadores `activeNonseqCycles`/`activeSeqCycles` y
`GBAMemoryStall(cpu, wait)` (distancia `lastPrefetchedPc` vs PC). El buffer de
prefetch son `cpu->prefetch[0..1]` recargados por `ARMWritePC`/`ThumbWritePC`
con `activeNonseqCycles32 + activeSeqCycles32`. Portar esto fielmente es el
siguiente paso; esta capa de bus (tablas + invalidacion) es su cimiento.



## 2026-10-26 (sesion de audio/graficos): fixes de WAITCNT y del reloj del FIFO

Revision linea por linea contra GBATEK (`/tmp/gbatek.txt`) de `apu.c`, `mem.c`,
`ppu.c` y `arm.c`. Dos bugs reales encontrados y corregidos, verificados con la
suite y con mGBA de referencia (`tools/mgbaharness.c` sobre `build-nogl`):

### Bus / WAITCNT (mem.c): el decodificado estaba desplazado un campo
`bus_update_waitstates` leia WS0 de los bits 0-1 (que son **SRAM**), WS1 de los
2-3 y WS2 de los 5-6; el mismo error estaba en `dma_region_ws` (ROM desde los
bits 0-1 de SRAM). GBATEK "4000204h - WAITCNT": SRAM 0-1, **WS0 first 2-3**,
WS0 second 4, **WS1 first 5-6**, WS1 second 7 (4/1), **WS2 first 8-9**, WS2
second 10 (8/1). Corregido en ambos sitios, con los valores second reales
(WS1 = 4/1 y WS2 = 8/1, no la tabla duplicada de WS0). Efecto: los waitstates
de ROM ya no se toman de SRAM; el prefetch y el coste de fetch por region son
los que dicta el registro.

`bus_fetch_cycles` ademas ahora cobra el segundo ciclo de un fetch de 32 bit en
los buses de 16 bit (VRAM/OAM/Palette), que antes devolvian 1 ciclo.

### Audio "rayado" (apu.c): el FIFO se consumia en rafagas por scanline
`apu_timer_overflow` hacia los pops **dentro** del overflow del timer, que el
nucleo agrupa por scanline (`io_update_timers(g, 960)` una vez por linea). El
Direct Sound salia como sample-and-hold a ~13.6 kHz con escalones del tamano de
la rafaga, y el DMA de FIFO se pedia a saltos: de ahi el "rayado".

Ahora el reloj del FIFO lo lleva `apu_fifo_tick()`, llamado por muestra de audio
desde `gen_sample()` con `STEP_FP` (16.16): saca **una** muestra por periodo del
timer (`(0x10000-reload)*prescaler`) y pide DMA si quedan <= 16 bytes. El
`Apu` guarda `fifo_next[2]` (ciclos hasta el proximo pop, 16.16); el overflow
del timer ya no hace nada.

Verificacion (Esmeralda, 400 frames, `PCMDUMP`):
- rachas de muestras identicas > 40 en la zona activa: 5 -> 1 (la que queda es
  el silencio de arranque, 3.16 s);
- sin clipping (peak 8128, antes 8192 tocando el techo);
- espectro vs mGBA (44100 y 65536 Hz remuestreados): high/low (>10k / <4k)
  ours 0.0195 vs mGBA 0.0163, es decir sin el exceso de agudos que delataba el
  sample-and-hold.

### PPU: revisado, correcto
`ppu.c` (BG 0-5, OBJ normal+afin, windows, efectos de color, mosaic) coincide
con GBATEK y con las formulas de mGBA (`software-obj.c`: `stride = width>>!256`,
`xBase`/`yBase`); el direccionamiento de tiles OBJ en modo bitmap (base
0x14000, `slot-512`) tambien. **No se toco.**

### Estado de los glitches y de la suite
Los glitches de video NO son del PPU (ya documentado) sino del modelo de ciclos;
Fire Emblem sigue en **19.86% de divergencia en el frame 300**, exactamente el
baseline de `AGENTS.md`, tanto antes como despues de estos fixes (los fixes de
WAITCNT no cambian la fase porque el fetch secuencial ya costaba 1 ciclo).
Suite: **75 PASSED / 49 FAILED / 9 UNKNOWN / 133 TOTAL**, igual que el baseline.

Los 49 fallos siguen dominados por **irq (11), LDM (8), Interactions (5), DMA
(5), Bus (5)**, que dependen del modelo completo de bus (fetch de 32 bit +
prefetch 8x16 + ciclo interno de 32 bit y su interaccion con la visibilidad de
timers), el prerrequisito ya documentado. Ese es el siguiente trabajo, no un bug
puntual. Los fixes de esta sesion son su cimiento correcto (tablas de waitstate
por region y coste de fetch por region).

## 2026-10-26 (2): reloj de frame absoluto y el falso "modelo de ciclos"

El muro de los glitches de video **no era el modelo de ciclos de bus**: era el
reloj del frame. `run_frame_core` corria la CPU con presupuestos **relativos**
(`arm_execute(g, 960)` / `arm_execute(g, 272)`); cada llamada puede pasarse al
ejecutar la ultima instruccion, y ese exceso se sumaba 456 veces por frame. El
frame medido salia en **285776 ciclos** cuando el hardware son **228*1232 =
280896** (+1.7%), asi que el juego derivaba de fase frame a frame.

Corregido: los limites de linea son **absolutos** respecto a la base del frame
(`base + line*1232 + 960` y `base + (line+1)*1232`), de modo que cada linea
recupera el exceso de la anterior. Frames sin DMA salen **exactos**; con DMA
queda un residuo pequeno y variable (unos 1000/frame) que es tiempo de DMA real
que se sale del frame.

Efecto medido: Fire Emblem a 900 frames sigue en 95.2% de divergencia (95.21 ->
95.18). Es decir, **los glitches NO son deriva de reloj ni del PPU**: ppu.c
coincide con GBATEK y con mGBA, y el reloj de frame ya es exacto. Quedan la
deriva de fase por el modelo de ciclos fino (prefetch 8x16, stall por acceso) y,
sobre todo, el **arranque HLE**: nuestro harness usa `bios_fastboot = true` y
compara contra mGBA con `FASTBOOT=1` (que solo escribe DISPCNT/DISPSTAT/IE/IME
por encima), no son arranques equivalentes; a partir del frame 300 el juego esta
en pantallas distintas. La comparacion fiable exige un **BIOS real** en ambos
(`BIOS=<dump> gba_load_bios` en el nuestro, `BIOS=<dump>` en mGBA) y el mismo
punto de partida.

Suite con el reloj absoluto: **74 PASSED / 50 FAILED / 9 UNKNOWN / 133** (baja
`ppu/start_up_vbl`; `DMA_ROM_Fixed`, `DMA_pause_timing_ROM_to_IWRAM`,
`IRQ_sub_slow` y `Sprite_Last_VRAM_Access` acercan su veredicto). El reloj
correcto es el cimiento; el test que se pierde mide el prefetcher, que aun no
esta portado.

## 2026-10-26 (3): el muro de divergencia es el arranque HLE, no el PPU

Con **BIOS real en ambos lados** (`BIOS=gba_bios.bin NOFASTBOOT=1` en el
harness, `BIOS=<dump>` en mGBA) Fire Emblem pasa de **19.86% a 1.08%** de
divergencia a frame 300. Y a frame 500, con el estado del juego ya casi
identico a mGBA (VRAM 0.7%, EWRAM 0.1%, IWRAM 3.3% de bytes distintos),
seguimos renderizando 22% distinto **solo por estar unos frames por delante**:
la animacion de intro gira, asi que un desfase de fase produce un diff enorme
sin ningun bug de render. mGBA reporta `frameCycles=280896` en cada frame, que
confirma el objetivo del reloj.

Los 4 bytes de IO que difieren a frame 30 entre nuestro fastboot y el BIOS real
lo prueban: `WAITCNT` (0x204) ya lo escribio el juego en nuestro run pero no en
el de BIOS (vamos por delante) y `SIOCNT` (0x128) = 0x6003 lo deja el BIOS y
nuestro HLE no lo ponia. **Corregido**: el fastboot replica `SIOCNT=0x6003`.

Conclusion: ppu.c no tiene un bug evidente; el ruido de la comparacion es el
**arranque HLE vs BIOS real** y la fase. Para diagnosticar glitches de render
hay que comparar **ambos con BIOS real y en la misma fase**, o renderizar el
mismo estado de video. El modo `renderstate` del harness no sirve para esto
porque aplica los registros de fin de frame a todas las lineas y no reproduce
el HDMA por scanline.

Cambios de esta sesion: reloj de frame con base propia (`frame_base`) para que
el exceso de DMA no desplace el reloj del PPU, guarda de resincronizacion tras
cargar savestate, `SIOCNT` en el fastboot, y volcado de `io.bin` en el harness.

**Default de arranque cambiado a BIOS real** (`BIOSFastBoot` default 0). La app
ya trae `gba_bios.bin` y lo carga; con el dump presente arranca por BIOS real y
coincide con mGBA. Si el dump no esta, `gba_load_bios` deja `bios_loaded=false`
y el reset cae al HLE fastboot, asi que sin el fichero nada se rompe. El menu de
Options sigue permitiendo alternarlo.





## 2026-10-26 (5): el "rayado" era el resampleo del Direct Sound (resuelto)

**Causa raiz.** El Direct Sound (DAC de 8 bit relojado por un timer, p.ej.
Timer0 a 13379 Hz en Pokemon Esmeralda) se estaba reproduciendo en **escalones**:
cada byte del FIFO se repetia tal cual durante N muestras de salida de 44100 Hz
(`fifo_held` directo al mezclador). Reproducir un flujo de 13.4 kHz por
repeticion de muestra es un **sample-and-hold sin filtro**: mete energia de alta
frecuencia en cada transicion (los "clics"/rayado). Medido: 3568 saltos de
amplitud >8000 en 5 min (mGBA: 973), con picos espectrales de ruido HF
(centroide ~4298 Hz frente a ~119 Hz de mGBA).

**No era** inanicion de FIFO (8 underruns en 4.0M pops), ni el PSG (con
`APUMUTE=2`, FIFO muteado, 0 saltos), ni el DMA (los 4.0M pops de A y B
coinciden bit a bit con mGBA), ni el ring buffer (0 descartes con el consumidor
drenando).

**Arreglo** (`src/apu.c`, `apu_fifo_tick`): resampleo con **interpolacion
lineal** al estilo del resampler de mGBA. Por cada muestra de salida la fase
avanza `STEP_FP/period` (16.16) y se interpola entre las dos ultimas muestras
sacadas del FIFO (`fifo_prev`/`fifo_held`), guardando el resultado en
`fifo_interp` (Q8) que consume el mezclador. El pop del FIFO y la peticion DMA
ocurren cuando la fase cruza 1.0, o sea a la frecuencia exacta del timer.
Se elimino el `fifo_next` (reloj en ciclos de CPU) que solo servia para decidir
*cuando* hacer pop; ahora lo decide la fase de resampleo, que es equivalente y
mas simple.

**Resultado** (misma partida de 5 min vs mGBA): **0 saltos >8000** (antes 3568;
mGBA 973), RMS 3133 (mGBA 2954), y por ventana de 1 s 0 saltos en las 15
ventanas (mGBA tiene 973 en total). El audio nativo (waveOut, 44100 Hz) ya no
raya.

**ROM sintetica de prueba**: `tools/gen_sound_test.py` genera un GBA de 220
bytes que arma DMA1 (timing 3, 32 bit, repeat) hacia FIFO A con una rampa en
EWRAM y Timer0 como reloj. Trampas resueltas al generarla: (1) los offsets de
`ldr`/`str` son en **bytes** (no palabras); (2) al repetir, **ni mGBA ni
nosotros** recargamos la direccion origen del DMA (solo el destino con
`DEST_RELOAD`), asi que el DMA corre mas alla de la tabla: para comparar streams
largos hay que usar `DEST_RELOAD` o un solo bloque. Sirvio para confirmar que el
flujo de bytes del FIFO que consumimos es identico al de mGBA.

Instrumentacion nueva (bajo `APUDBG`, sin coste en release): `FIFODUMP=<path>`
vuelca cada muestra del FIFO que se consume (para el diff byte a byte contra
mGBA) y `apudbg`/`apudbg2` imprimen contadores de pops/underrun/maxlen y el
estado de los timers. En mGBA se anadio el mismo `FIFODUMP` en
`src/gba/audio.c` (parche local, no versionado).

## 2026-10-27: el PSG sonaba a la mitad (nivel de mezcla y ganancia final)

**Reporte del usuario**: el "plim!" de seleccion de menu en Pokemon no suena
como en el original (y en general los efectos PSG suenan flojos frente a la
musica del Direct Sound).

**Diagnostico.** El PPU y el generador de tono PSG eran correctos: comparado
con mGBA, la **frecuencia** y el **duty** de cada canal coinciden (duty 0-3:
fraccion HIGH 0.124/0.248/0.498/0.751 vs 0.122/0.253/0.504/0.748 de mGBA, y el
espectro de armonicos tambien: el 50% anula los pares, el 25% el 4o). Lo que
estaba mal era el **nivel**:

1. **Volumen maestro PSG** (`SOUNDCNT_H` bits 0-1): usabamos la tabla
   `PSGVOL[4] = {1,2,4,4}` dividida por 4, o sea ganancia `2^vol/4`. mGBA
   (`GBAudioSamplePSG` + `GBAAudioSample`: `sample << 3` y luego
   `>> (4 - volumen)`) da `2^(vol-1)`: 25% => x0.5, 50% => x1, 100% => x2,
   200% => x4. La nuestra era **la mitad** en 25/50/100% y un cuarto en 200%.
2. **Ganancia final**: escalabamos x64 al final del filtro paso alto; mGBA usa
   `sample * masterVolume(0x100) * 3 >> 4` = **x48**. Con x64 la salida quedaba
   un 33% por encima de la referencia.

**Medicion** (test PSG sintetico, `gen_psg_duty.py`, AC RMS quitando DC):
antes ratio ours/mgba = 0.65 en los 4 duties (la mitad + algo de offset DC);
tras el arreglo **0.98-0.99** (dentro del 2%). En la captura real de Pokemon
Esmeralda el RMS global pasa de 2446 (x64) a 1898, frente a 2032 de mGBA.

**Arreglo** (`src/apu.c`, `gen_sample`): `int pv = 1 << (a->cnt_h & 3);` con
`l = lsum * vol_l * pv / 2` (equivalente a `2^(vol-1)` sobre la suma), y la
ganancia final del filtro pasa a `hp_y/256 * 48`.

**Verificacion**: `tools/gba_test_suite.py` sin regresion (74 PASSED, identico
al baseline) y los binarios Windows nativos (x86_64 e i686, sin SDL2)
recompilados limpios.

**Nota sobre el test Direct Sound**: la ROM de `tools/gen_sound_test.py` con
`DMA1CNT_H=0xB660` (destino increment/reload) NO recarga la fuente al repetir
ni nosotros ni mGBA, asi que el DMA lee mas alla de la tabla de 32 bytes y la
prueba deja de ser comparable (nuestra salida muere cuando el FIFO se agota;
mGBA sigue porque su `GBAAudioWriteSOUNDCNT_HI` fija `destOffset=0`). No es un
bug del emulador: en Pokemon Esmeralda el FIFO A se rellena correctamente
(underA=3 en 335595 pops). La afirmacion "DMA FIFO muere tras un pase" de la
nota anterior era un artefacto de la ROM de prueba, no del core.

