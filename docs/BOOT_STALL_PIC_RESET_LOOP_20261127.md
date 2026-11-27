# The Writer: bucle infinito de /RESET del PIC en arranque normal (sin bypass)

**Fecha:** 2026-11-27. **Fuente:** trazas reales de `writerdiag` (instrumentación
existente `PIC RA1=... -> Z180 /RESET ASSERT/CLEAR`, ya presente en writer.cpp),
corridas en VONTAR. No es inferencia: son eventos reales logueados por la
emulación del PIC real (pic16c54_device ejecutando el dump físico de U11).

## Hallazgo 1: el PIC reinicia el Z180 cada ~2.016s, indefinidamente

En una corrida normal (SIN `WRITER_TEST_DISABLE_PIC_RESET`, SIN ningún otro
bypass de diagnóstico), el PIC pulsa `/RESET` del Z180 de forma **periódica e
indefinida**, cada ~2.016s de tiempo real/emulado, durante toda la duración de
la corrida (probado hasta 30s reales con `-nothrottle`, 14+ ciclos completos
sin romperse nunca):

```
PIC RA1=0 (TRISA output) -> Z180 /RESET ASSERT t=2.599121s
PIC RA1=1 (TRISA input/floating+pullup) -> Z180 /RESET CLEAR t=2.743652s
PIC RA1=0 (TRISA output) -> Z180 /RESET ASSERT t=4.759399s
... (se repite cada ~2.016s, sin fin, hasta el final de la corrida)
```

**Consecuencia directa:** el firmware del Z180 nunca llega a ejecutar el código
que imprime texto real en el LCD (confirmado: 0 escrituras `rs=1` en 30s reales
de corrida normal), porque se reinicia antes de llegar ahí.

## Hallazgo 2 (2026-11-27, actualizado): experimento descarta la hipótesis de SRAM en frío

Se probó `WRITER_TEST_VALID_RAM_SIGNATURE=1` SIN `WRITER_TEST_DISABLE_PIC_RESET`
(firma de Master Reset D000/D002 inyectada artificialmente, para ver si el
ciclo de reset dependía de una SRAM "en frío"). Resultado: el ciclo de reset
continuó IDÉNTICO (7 asserts en 15s, mismo periodo ~2.016s). **Esto descarta
la hipótesis 1 del documento original** (SRAM en frío como causa). El ciclo
no tiene nada que ver con el estado de la SRAM.

## Hallazgo 3 (2026-11-27): causa más probable -- carrera de tiempos entre el PIC y el Z180

Revisando el código real del PIC (`PIC16C54C_U11_ANALYSIS.md` sec.4) y de
`writer.cpp` (líneas 383-402, ya implementado correctamente): el PIC pulsa
RA2, y luego comprueba `(PORTB & 0x82) == 0x80`, es decir:
- RB7=1 (Z180 TXA1 en reposo/idle-high)
- RB1=0 (Z180 /HALT asertado -- writer.cpp ya modela esto correctamente,
  tratando tanto HALT como SLP como /HALT asertado, vía
  `z180_halt_pin_asserted()`)

Si no se cumple, decrementa un contador y repite; al agotrarlo, reinicia el
Z180 (RA1).

**El problema es de tiempos, no de lógica:** el Z180 tarda ~2.79s en llegar a
SLP@0x0E27 (medido en corridas con el reset del PIC suprimido, ver Secciones
28/33 del doc confidencial de Drive), pero el periodo completo reset-a-reset
del PIC medido aquí es de solo ~2.016s. Es decir, **el PIC se rinde y
reinicia el Z180 antes de que este llegue a HALT/SLP, en cada intento, sin
excepción.** Esto explicaría el bucle infinito sin necesidad de ningún otro
mecanismo adicional: es una carrera que el Z180 siempre pierde.

Esto también conecta con el hallazgo de la Sección 33 (doc confidencial): el
propio Z180, durante esos ~2.79s, pasa ~2.0s de ese tiempo haciendo spin en
`ram:0636` esperando un byte serial o un tick de D017 -- que a su vez depende
de un pulso RA2 del PIC. Es decir, hay una dependencia circular entre ambos
lados del sistema cuyos tiempos relativos exactos (quién pulsa qué y cuándo,
en microsegundos reales) no están medidos físicamente todavía.

## Hipótesis actualizadas (ninguna confirmada por medición física)

1. ~~Comportamiento esperado para SRAM en frío.~~ **DESCARTADA** (Hallazgo 2).
2. **Carrera de tiempos PIC/Z180** (Hallazgo 3): el periodo de reintento del
   PIC es más corto que el tiempo real que el Z180 necesita para completar
   su primer ciclo hasta HALT/SLP. Candidato más fuerte actualmente.
3. ~~Una condición de handshake por el enlace serial PIC<->Z180 (bit-bang,
   baud rate/polaridad) que nunca se satisface.~~ **Descartada como causa de
   la carrera de tiempos** (Hallazgo 4: corregir el baudrate ASCI1 no cambia
   el tiempo-a-SLP ni el bloqueo). El baud rate/polaridad exacto del enlace
   bit-bang sigue sin medición física y puede importar para otros fines, pero
   no es la causa del bloqueo de arranque.

**Tras el Hallazgo 4, la hipótesis 2 (carrera de tiempos PIC/Z180) queda como
la única explicación viable del bloqueo, aún pendiente de resolución.**
Cerrarla requeriría cambiar la relación real entre el periodo de reintento
del PIC y el tiempo que tarda el Z180 en llegar a SLP -- ninguno de los dos
es, hasta ahora, algo que se haya identificado como corregible por software
sin evidencia adicional (ver Sección 34 del doc confidencial: el modelo de
reloj del PIC ya es fiel al hardware real). Sin medición física del hardware
real (pospuesta por el usuario), no hay forma de distinguir si este
comportamiento es el del hardware real (en cuyo caso no habría "bug" que
corregir) o un parámetro de modelado aún no identificado.

## Hallazgo 4 (2026-11-27, esta intervención): la corrección ASCI1 full-PHI NO resuelve el bloqueo

Se probó la hipótesis de que el mismatch de reloj ASCI1 (150 vs 300 bps,
ver `PIC_ASCI1_HANDSHAKE_ANALYSIS_20261126.md` y
`ASCI1_FULL_PHI_AB_TEST_20261127.md`) pudiera estar causando o agravando esta
carrera, ya que un receptor mal temporizado podría forzar al Z180 a salir
siempre del spin-loop de `ram:062f` por la rama lenta (umbral D017) en vez
de por la rama rápida (byte serial recibido), alargando el tiempo hasta SLP.

Dos corridas con el binario ya existente (`writerdiag`, SHA256
`9b549ca53b147293b4f8b83ee0560a8ee28f6e03bc023ee4cec7cc3e586642d8`,
que ya incluye el flag opt-in `WRITER_TEST_ASCI1_FULL_PHI` fusionado en
`mame-as2k-stable-arm64` commit `7218da95`):

1. **Arranque normal (sin bypass) + `WRITER_TEST_ASCI1_FULL_PHI=1`, 35s
   emulados:** el bucle de `/RESET` del PIC continúa indefinidamente
   (16 asserts). El periodo cambió de ~2.016s (línea base sin el flag) a
   ~2.160s con el flag activo. `FINAL PC=0639` en los 16 ciclos: el Z180
   queda atrapado en el mismo spin-loop de siempre (`ram:062f-064d`), nunca
   avanza. Cero escrituras `rs=1` (ningún texto real).
2. **`WRITER_TEST_DISABLE_PIC_RESET=1` + `WRITER_TEST_ASCI1_FULL_PHI=1`,
   corrida corta:** `first /HALT or SLP assertion at t=2.788818s PC=0E27` --
   **el mismo valor exacto**, al microsegundo, que la medición original sin
   el flag (Secciones 28/33 del doc confidencial). 120 escrituras `rs=1`
   confirmando que el arranque completo sigue funcionando igual que antes.

**Conclusión verificada:** la corrección ASCI1 full-PHI (justificada por el
manual Zilog, corrige un mismatch real de modelado 150 vs 300 bps) no tiene
ningún efecto medible sobre el tiempo hasta SLP ni sobre el bloqueo de
arranque normal. Esto refuerza la Sección 33 del doc confidencial: el
spin-loop dominante de `ram:062f` se rompe por el umbral D017/IRQ1 (pulsos
RA2 del PIC), no por la recepción de un byte serial bien temporizado -- por
eso corregir el baudrate no cambia nada. La hipótesis de "mismatch de reloj
ASCI1 causa o agrava el bloqueo" queda **descartada**.

**No se aplicó** `WRITER_TEST_ASCI1_FULL_PHI` como comportamiento por
defecto en `writer.cpp`: no resuelve el bloqueo y no hay evidencia que
justifique cambiar el default de producción. El flag queda como
instrumentación opt-in para pruebas, igual que antes.

**Anomalía sin explicar (marcada honestamente, no asumida):** el periodo de
reintento del PIC cambió de ~2.016s a ~2.160s al activar el flag, pese a que
el contador de reintento del PIC es puramente interno (RAM 0x0E/0x0F, reloj
propio de 32.768 kHz) y no debería depender del reloj ASCI1 del Z180. Es
candidato a ser un artefacto de la reprogramación del quantum/scheduler de
MAME al cambiar el `DERIVED_CLOCK` de un dispositivo, no necesariamente un
efecto real de hardware. No se investigó más a fondo en esta intervención;
no cambia la conclusión principal porque el tiempo-a-SLP fue IDÉNTICO en
ambas corridas con bypass.

## Próximos pasos posibles (sin medición física)

- Medir con mayor precisión, vía logs ya existentes, el momento exacto
  (dentro del ciclo del PIC) en que se hace el pulso RA2 y la comprobación
  de PORTB, para confirmar si el margen real es de ~0.77s de déficit
  (2.79s necesarios vs ~2.016s disponibles) o si hay otro factor.
- Revisar si el periodo de reintento del PIC (los contadores de
  `PIC16C54C_U11_ANALYSIS.md` sec.2, down-counter en RAM `0x0F` para RB7,
  contador adicional en RAM `0x0E` para TMR0) está modelado con el prescaler
  y reloj correctos (32.768kHz, confirmado en Sección 34) -- si el conteo de
  iteraciones real en el firmware del PIC no coincide con el medido, el
  periodo de ~2.016s podría estar mal calculado en vez de ser un bug de
  carrera genuino del diseño original.
- Medición física (pendiente, pospuesta): el periodo real del PIC en el
  hardware real podría no coincidir exactamente con el modelo actual si hay
  algún parámetro no capturado en el desensamblado (p.ej. un NOP de
  temporización, un WDT reset intermedio, etc.).

## Actualización 2026-11-27 (intervención horaria, continuación de Sección 39 del doc confidencial)

Repo verificado limpio en VONTAR, HEAD sin cambios (`ccf802e`). Se localizó
el core compartido `mame-as2k-stable-arm64/src/devices/cpu/z180/{z180.cpp,z180.h}`,
pero un primer grep de `slp`, `refresh`, `extra_cycles`, `wait_state`, `IOCR`,
`DCNTL` en `z180.cpp` no dio coincidencias: la tabla de opcodes ED (donde vive
`SLP`, `ED 76`) probablemente está en otro archivo del mismo directorio, aún
no inspeccionado. Se agotó el presupuesto de llamadas a Desktop Commander de
esta intervención antes de confirmarlo. Próximo paso: listar el directorio
`z180/` completo e identificar dónde se dispatchea/cuenta ciclos el opcode
`SLP`. Detalle completo en la Sección 40 del doc confidencial de Drive.
Bloqueo SIGUE ABIERTO.
