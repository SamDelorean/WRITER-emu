# Nuevo bloqueo tras el Hallazgo 7: ciclo periódico de ~34.4s con /RESET del PIC

**Fecha:** 2026-11-28. **Contexto:** descubierto al extender la verificación del
parche del Hallazgo 7 (`docs/HALLAZGO7_PATCH_APPLIED_20261128.md`) de 30s a
90s de corrida emulada. El bloqueo original (bucle de ~2.016s, documentado en
`docs/BOOT_STALL_PIC_RESET_LOOP_20261127.md`) sigue resuelto; esto es un
fenómeno **distinto**, visible solo ahora porque antes el sistema nunca
avanzaba lo suficiente para llegar a este punto.

**Estado: NO es necesariamente un bug.** La evidencia reunida hasta ahora
apunta a que es el comportamiento normal y esperado del firmware mientras
espera una pulsación de tecla del usuario en la pantalla de advertencia de
memoria. Falta confirmarlo inyectando una tecla real. Este documento reúne
todo lo necesario para que la próxima intervención (automática o de Samuel)
lo confirme o lo descarte.

## 1. El fenómeno observado

Arranque normal, sin bypass, con el parche del Hallazgo 7 aplicado, 90s
emulados, trazas `WRITER_TRACE_PIC_PORTB=1`:

- `/RESET` del Z180 se pulsa **tres veces** en 90s, con un periodo exacto y
  reproducible: **34.407s** entre cada pulso (medido dos veces, idéntico en
  ambas: t=0.583s→34.990s y t=35.134s→69.541s).
- Cada vez que esto ocurre, la pantalla del LCD se **redibuja desde cero con
  el mismo mensaje exacto**, carácter por carácter (120 bytes `rs=1`
  idénticos en los tres segmentos del log): `"MEMORY ERROR WARNING!!! Data
  may be corrupted! Please download or backup data as soon as possible."`
- No hay progreso entre ciclos: el sistema no avanza a ninguna otra pantalla
  ni estado tras el reset; simplemente repite el mismo ciclo.
- No hay crash, ni excepción, ni corrupción de estado visible en el log
  (22706 líneas revisadas completas para el run de 90s).

Logs completos guardados en VONTAR:
`/home/spc/projects/WRITER_INSTRUCTOR/diag/verified_hallazgo7_20261128/error_90s.log`
(y `build_post_patch.log`, `error_patch_test.log` del run de 30s).

## 2. Causa identificada en el disassembly del PIC (alta confianza)

Fuente: `willem/ghidra_writer/analysis_20261121/out/pic_disassembly.txt`
(direcciones de **palabra PIC**, no de byte; mismo dump ya usado en
`docs/hardware/PIC16C54C_U11_ANALYSIS.md`).

El bucle principal (`0x082–0x0E2`, ya descrito en la Sección 2 de ese
documento) tiene **dos** esperas secuenciales con timeout antes de volver a
`0x082`:

1. **Primera espera (`0xAC–0xB3`):** contador de 128 iteraciones (`0x0F` <-
   `0x80`) esperando `RB7=0` (TXA1 bajo). Esta es la espera que arreglamos
   en el Hallazgo 7 -- antes del parche, el Z180 jamás bajaba TXA1 a tiempo,
   así que esta espera siempre se agotaba y saltaba directo a `0xD7`, dando
   el periodo de ~2.016s del bloqueo original. Con el parche, esta condición
   ahora se cumple rápido y el flujo avanza a `0xB4` (transmite los 4 bytes
   de `0x08–0x0B` por RB0, ya documentado en la Sección 3 del análisis del
   PIC) y entra a la **segunda espera**.

2. **Segunda espera (`0xBC–0xD5`), nunca antes alcanzada en una corrida
   normal:** reconfigura OPTION/TMR0, limpia RAM `0x0E`, y entra a un bucle
   anidado: pulsa RA2 (`/INT1` -- el heartbeat que ya veíamos en los logs),
   llama a la rutina de perfil `0x59` (re-afirma el estado "RESET CLEAR"),
   evalúa de nuevo `(PORTB & 0x82) == 0x80` (la misma expresión de la
   Sección 4 del análisis del PIC, aquí usada una **segunda vez**, con
   semántica propia). Si se cumple, salta a `0xD6`→`0xD7`. Si no, sigue
   revisando `TMR0` (timeout corto, 8 bits) anidado dentro de un contador
   `0x0E` (timeout largo) que se decrementa en cada vuelta de `TMR0`. Cuando
   **ambos** se agotan, también cae a `0xD7`.

   **Conclusión clave: `0xD7` se alcanza SIEMPRE, tanto si la condición se
   cumple como si no.** En `0xD7` el firmware llama a `0x62` y luego a
   `0x6B` -- dos rutinas de "perfil de puertos" (ya señaladas como
   pendientes de reconstrucción en la Sección 2 del análisis del PIC,
   líneas `0x059`, `0x062`, `0x06B`). `0x6B` fija `TRISA=0` (todo salida) y
   `PORTA=0x00`, forzando `RA1=0` -- **esto es exactamente el pulso de
   `/RESET` que vemos en el log.**

3. **Tras el reset (`0xDD–0xE2`):** el firmware llama a varias rutinas
   auxiliares y luego **evalúa `RB3`** (`BTFSC PORTB,#3` en `0xE0`). Según
   la propia instrumentación ya existente en `writer.cpp`
   (`WRITER_TRACE_PIC_PORTB`, línea con `RB3=KEY`), **RB3 está etiquetado
   como la señal de teclado**. Si `RB3` está activo, continúa a `0x82`
   (reinicia todo el ciclo principal desde el top); si no, repite el tramo
   `0xDD` esperando.

### Hipótesis principal

El ciclo completo de ~34.4s que vemos es la duración de la **segunda
espera** (`0xBC–0xD5`, con su doble timeout TMR0+RAM `0x0E`), que antes del
Hallazgo 7 **nunca se alcanzaba** porque la primera espera (`0xAC–0xB3`,
~2.016s) siempre fallaba primero. No es necesariamente un bug nuevo: es la
misma máquina de estados de siempre, ahora progresando un paso más, y
**reiniciando el Z180 como parte normal de su propio ciclo de reintento/
resincronización mientras espera que el usuario reconozca la advertencia de
memoria con una tecla (`RB3`)**. Si esto es correcto, en hardware real el
usuario vería la pantalla de advertencia, el sistema se "respira" cada
~34s (retransmite los 4 bytes, resetea brevemente el Z180, vuelve a
dibujar el mismo mensaje) hasta que se presiona una tecla, momento en el
cual debería progresar a otra pantalla.

**Esto todavía no está confirmado.** Es la hipótesis mejor sustentada por
el código, no un hecho verificado.

## 3. Qué falta para confirmar o descartar esta hipótesis

1. **Inyectar una pulsación de tecla real durante la pantalla de
   advertencia** (vía el input de MAME para el driver `akwriter`, no una
   traza) y comprobar si el sistema progresa a otra pantalla en vez de
   repetir el ciclo de 34.4s. Requiere revisar el mapeo de teclado ya
   trabajado en `tools/buscar_teclado.py`, `tools/gen_keyboard_ports.py` y
   los scripts Ghidra `tools/ghidra/WriterKeyboard*.java` para saber qué
   tecla/entrada de MAME corresponde a RB3 en este contexto.
2. **Confirmar con `WRITER_TRACE_PIC_RAM`** (ya existente) correlacionado
   con `WRITER_TRACE_PIC_PORTB` en una corrida de ~40s: ver el valor exacto
   de `TMR0` y RAM `0x0E` en el momento de llegar a `0xD7`, para confirmar
   que el timeout se agota por la vía `0xCD–0xD5` (doble timeout) y no por
   la vía de éxito `0xC7–0xCA` (condición `(PORTB&0x82)==0x80` cumplida) --
   ambas dan el mismo resultado observable (reset) pero con causas
   completamente distintas, y el tratamiento sería diferente si la
   condición SÍ se cumple pero el firmware resetea de todos modos (eso sí
   sería más sospechoso de bug).
3. **Revisar si hay una tecla "reconocer" esperada específicamente**, dado
   que el mensaje dice literalmente "Please download or backup data as
   soon as possible" -- en hardware real podría requerir una secuencia de
   teclado específica (no cualquier tecla), o incluso una acción externa
   (conexión serie para volcar datos) antes de continuar.
4. **Descartar WDT real del PIC como causa alternativa:** el vector de
   reset del PIC (`0x1FF → 0x074`) comienza con `CLRWDT` (ver línea 074 del
   disassembly), confirmando que el watchdog SÍ se usa en este firmware,
   pero el período nominal del WDT del PIC16C54C (~18ms típico, sin
   prescaler porque `OPTION=0x20` asigna el prescaler a TMR0, no a WDT) es
   ~1900x más corto que los 34.4s medidos -- por lo que el WDT real casi
   seguro NO es la causa directa de este ciclo. Se deja aquí documentado
   para no tener que redescartarlo en el futuro.

## 4. Lo que NO cambia

- El Hallazgo 7 (parche en `z180asci.cpp`) sigue siendo correcto y sigue
  resuelto: el bucle original de ~2.016s (que impedía CUALQUIER progreso,
  incluyendo dibujar texto) no vuelve a aparecer. Este es un fenómeno
  posterior, en una capa de comportamiento que antes era inalcanzable.
- El trabajo físico/hardware sigue pausado, sin tocar, por indicación de
  Samuel.
- No se modificó ningún archivo de código en esta intervención; solo
  análisis de logs ya generados y lectura del disassembly del PIC
  (ya existente en VONTAR, sin re-dumpear nada).
