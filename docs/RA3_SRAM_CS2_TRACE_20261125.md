# PIC RA3 / SRAM CS2 — traza dinámica

Fecha: 2026-11-25
Ejecución: build de las 16:10:06, corrida terminada 16:10:11 (hora local)
Rama: `fix/irq1-ra2-wakeup`

## Resultado

La señal PIC RA3, conectada físicamente a `CS2` de SRAM U16/U17 según el informe de continuidad del proyecto, **conmuta dinámicamente**. No está fija en un nivel. En la ejecución controlada de Master Reset:

- RA3 bajo: el registro PORTA del PIC vale `0x00`; la transición fue registrada desde PIC PC `0x071` (o `0x044` durante inicialización).
- RA3 alto: PORTA vale `0x0C`; la transición fue registrada desde PIC PC `0x056`.
- RA2 cambia simultáneamente: PORTA `0x00` afirma Z180 `/INT1`; PORTA `0x0C` libera `/INT1`.
- Después de la transición a RA3 alto de la línea 582 de `diag/error.log`, no se registra otro cambio de RA3 hasta la línea 1621, posterior a la detección de las firmas de Master Reset. Por ello, RA3 permaneció alto durante el intervalo que incluye la ejecución de la rutina ROM `0x2206` (escaneo de bloques).

Esto vincula el estado alto de RA3 con el periodo activo en que el Z180 despierta y ejecuta firmware, mientras el nivel bajo coincide con el retorno a la espera `SLP` en `0x0E27` y, posteriormente, `0x2568`.

## Correlación con el desensamblado PIC

El listado `willem/ghidra_writer/analysis_20261121/out/pic_disassembly.txt` muestra:

- Rutina `0x59`: escribe `PORTA=0x0C`; configura `TRISA=0x02`. RA3 (bit 3) queda como salida y en nivel alto.
- Rutina `0x62`: vuelve a escribir `PORTA=0x0C`, con `TRISA=0x00`. RA3 sigue como salida alta.
- Rutina `0x6B`: escribe `PORTA=0x00`, con `TRISA=0x00`. RA3 queda como salida baja.
- El flujo PIC `0x0C1-0x0C3` pulsa RA2 para IRQ1 y luego llama a `0x59`; el flujo posterior vuelve a llamar `0x62` y `0x6B` para alternar el estado de PORTA.

La traza MAME registra las transiciones en el callback de escritura de PORTA, junto con el PC del PIC y el PC del Z180. La instrumentación solo observa el bit RA3: **no altera PORTA, no fuerza CS2 y no implementa todavía el gating de SRAM**.

## Lecturas observadas

Extracto representativo de `diag/error.log`:

| Línea | RA3 | PORTA | PIC PC | Z180 PC | Interpretación |
|---:|---:|---:|---:|---:|---|
| 8 | 0 | 00 | 044 | 2419 | Inicialización |
| 54 | 1 | 0C | 056 | 1882 | PIC libera IRQ1; Z180 atiende interrupción |
| 159 | 0 | 00 | 071 | 0639 | PIC afirma IRQ1 |
| 168 | 1 | 0C | 056 | 0CBB | PIC libera IRQ1; Z180 en ruta de eventos |
| 250 | 0 | 00 | 071 | 0E27 | Z180 vuelve a SLP |
| 582 | 1 | 0C | 056 | 0531 | Periodo activo previo a Master Reset |
| 1617 | — | — | — | — | ROM ya escribió D000=916A y D002=5A3C |
| 1621 | 0 | 00 | 071 | 0E27 | Primer cambio a bajo después de las firmas |

El PC del Z180 mostrado es una muestra simultánea al callback de PORTA, no una captura de cada ciclo de bus. La conclusión de que RA3 permaneció alto entre líneas 582 y 1621 se basa en que el trazador registra cada cambio de bit 3 y no aparece otra transición en ese intervalo.

## Qué demuestra y qué no demuestra

**Demostrado:** el nivel lógico de RA3 conmuta 0/1; el PIC lo genera en las rutinas identificadas; el nivel alto se mantiene durante el intervalo que incluye el escaneo ROM de `0x2206`. En ese periodo, el firmware explora la ventana física `0x40000-0x7FFFF` buscando bloques con firma `3C 5A`.

**Actualización con la documentación de hardware de Drive (2026-11-25):** las fotografías identifican U16/U17 como Samsung K6X1008C2D-GB55 y el PDF `01_Hardware/K6X1008C2D_SAM.pdf` confirma el pinout de 32-SOP: CS2, activa en alto, está en pin 30; /CS1, activa en bajo, está en pin 22. El informe de continuidad `WRITER_RE_06_PIC_mapa_conexiones_U11.docx` confirma U11 RA3 pin 2 → CS2 pin 30 de ambas memorias. Por tanto, RA3=1 durante `0x2206` afirma la entrada CS2 de ambas SRAM.

**Aún no demostrado:** qué nivel presenta /CS1 en cada SRAM durante la exploración; qué chip responde a cada dirección; el mapa exacto de bancos; las continuidades de /OE (pin 24), /WE (pin 29) y buses; ni el contenido real de las SRAM físicas. CS2 alto es necesario, pero no suficiente para seleccionar la memoria: también se requiere /CS1 bajo.

La consolidación del pinout, datos de la hoja y discrepancias con el libro de referencia está en `docs/SRAM_K6X1008C2D_HARDWARE_EVIDENCE_20261125.md`.

La observación tampoco cambia la conclusión del informe Application BIOS: el modelo actual devuelve bytes de la EPROM en la dirección física `0x40000`, donde los bytes `FF 00` no coinciden con la firma `3C 5A` esperada. El emulador aún no separa EPROM y SRAM.

## Artefactos

- `diag/error.log`: SHA-256 `55c770cfc6c18c13eea3a9f83974626a13ddfac7e4e845aa936f3af6a9abd070`
- `diag/build.log`: SHA-256 `e4cdd49e4b6453720d4078820750e611e8038fee1c0a17c344c64070131c7cc1`
- `snap/live_snapshot_20261125_160819.bin`: SHA-256 `4d2db640385fe3b9212db51cf95055bb82e4d8f870f13c7c53f31c626ea10d8c`
- `snap/frame_20261125_160819.ppm`: SHA-256 `59f9ed09e706f2932ce0881b9648cdde4a19e7e1208682ffa90d09a69a47c6f9`

La corrida terminó con `RUN_EXIT=0`, aproximadamente 29 segundos emulados y velocidad media de 657.51%. El snapshot de RAM coincide byte a byte con la captura anterior de Master Reset.

## Siguiente prueba

1. **Resuelto:** identificar U16/U17 y polaridad de CS2 a partir de fotografías y datasheet. U16/U17 = Samsung K6X1008C2D-GB55; CS2 pin 30 activa-alta; /CS1 pin 22 activa-baja.
2. Confirmar por continuidad/esquema las líneas /CS1 individuales, direcciones, datos, `/OE` pin 24 y `/WE` pin 29 de ambos chips.
3. Repetir una prueba con un punto de observación de lectura física durante `0x2206`, correlacionando el nivel RA3 y los bytes devueltos. La lectura debe ser pasiva y no inyectar datos ni firmas.
4. Solo con ese mapa confirmado, modelar la separación EPROM/SRAM, la selección dinámica por CS2 y la persistencia del bloque SRAM.

No se modifica todavía el modelo de memoria ni el contenido de ROM. La autenticidad de la entrada de contraseña `ACE135` sigue siendo una línea de trabajo separada.
