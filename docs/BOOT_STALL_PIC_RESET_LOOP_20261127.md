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

## Actualización 2026-11-27 (intervención horaria, continuación de la Sección 40 del doc confidencial)

Repo en VONTAR verificado limpio al inicio (HEAD `ccf802e`, solo los
archivos sin trackear ya conocidos). Durante esta intervención se detectaron
y sincronizaron en paralelo los commits `1a98825` (ASCI1 full-PHI no resuelve
el bloqueo -- ver Hallazgo 4 arriba) y `02267bd` (nota de Samuel sobre el
siguiente paso en las tablas de opcodes del core z180), con un commit de
merge (`cda102c`) para resolver el conflicto de edición concurrente sobre
este mismo archivo.

Se completó el paso pendiente de la Sección 40: se listó
`src/devices/cpu/z180/` completo y se localizó el dispatch de `SLP`
(`ED 76`): `z180ed.hxx:144` llama a la macro `SLP` definida en
`z180ops.h:964` (`_PC -= 2; m_HALT = 2;` -- re-ejecuta el opcode, igual
patrón que `HALT`). Su ciclo está en `z180tbl.h` (`cc_ed[0x76] = 8`). Esto
**no es relevante para el retardo de ~2.79s hasta el primer SLP**: ese
retardo lo determinan las instrucciones ejecutadas ANTES de llegar a `ED 76`
por primera vez (el propio firmware de arranque), no el bucle de
reejecución de `SLP` en sí, que solo corre DESPUÉS de la primera entrada (ya
confirmada en `writer.cpp::z180_halt_pin_asserted()`, que lee el byte crudo
del state entry y ya trata HALT=1 y SLP=2 igual -- sin cambios, correcto,
reconfirmado).

**Pista nueva, concreta, sin verificar aún:** el registro Z180 `DCNTL`
(`Z180_DCNTL_MWI1/MWI0` e `IWI1/IWI0`, `z180.cpp:321-328`) controla la
inserción de wait-states reales de memoria e I/O -- es un mecanismo genérico
del core Z180, no del modelo específico de writer.cpp, y es justo el tipo de
parámetro que la Sección 39 proponía investigar (ciclos extra emulados antes
de llegar a SLP que el silicio real no tendría). Se ve leído/escrito en
`z180.cpp:709,749,969,1012` y consultado en `z180.cpp:1151-1313,1597-1605`.
No se llegó a trazar, en esta intervención, (a) el valor de reset de
`m_dcntl`, (b) si el firmware de la ROM del Writer escribe un valor
distinto durante el arranque, ni (c) en qué punto exacto del bucle de
ejecución esos bits se traducen en ciclos de reloj añadidos (los términos
`wait_state`/`refresh` no aparecen literalmente en el código, así que el
mecanismo usa otro nombre interno todavía no identificado).

**Próximo paso concreto:** trazar desde `m_dcntl` hasta el punto donde se
suman ciclos de espera reales en el bucle de instrucciones; confirmar en
`z180.cpp` (no se verificó aún en esta intervención) cuál es el valor de
reset real que el código usa para `m_dcntl`, y por separado cotejarlo
contra la hoja de datos Zilog Z8018x; y verificar si el firmware del Writer
reprograma DCNTL durante el arranque antes de llegar a SLP (y en qué punto
del flujo ya desensamblado). Si el valor de reset o el momento de la
reprogramación insertan más wait-states de los que el firmware espera, eso
alargaría el tiempo emulado hasta SLP sin que sea un comportamiento del
silicio real -- hipótesis a favor o en contra, nada de esto confirmado
todavía.

Sin medición física. Estado del bloqueo: SIGUE ABIERTO. Cambios de esta
intervención: solo documentación (este archivo) y el commit de merge que
sincroniza el trabajo en paralelo; sin cambios de código.

## Actualización 2026-11-27 (intervención horaria, continuación de la Sección 41 del doc confidencial)

Repo en VONTAR verificado al inicio: HEAD en `fix/irq1-ra2-wakeup` en
`0fa0944` (sin avance en paralelo del usuario desde la Sección 41; sin
cambios de código pendientes). Sin tracked changes; solo los archivos sin
trackear ya conocidos de intervenciones previas (`cfg/`, `error.log`,
`snap/`, `tools/__pycache__/`, `tools/dis_driver.py`).

**Corrección de ruta importante para próximas intervenciones:** el árbol
compartido `mame-as2k-stable-arm64` NO vive bajo
`/home/spc/projects/WRITER_INSTRUCTOR/` como se asumió -- vive en
`/home/spc/projects/mame-as2k-stable-arm64` (un nivel arriba, hermano de
`WRITER_INSTRUCTOR/`, confirmado contra
`docs/ASCI1_FULL_PHI_AB_TEST_20261127.md:17` y
`docs/PIC_ASCI1_HANDSHAKE_ANALYSIS_20261126.md:10` en este mismo repo). Un
primer intento de localizarlo con `find` acotado a `WRITER_INSTRUCTOR/`
(maxdepth 2) no encontró nada; el `find /` de respaldo lanzado para
compensar resultó demasiado lento para la ventana de 3s de esta
intervención y se agotó el presupuesto de 5 llamadas a Desktop Commander
antes de poder leer `z180.cpp` con la ruta correcta. Ese proceso de
`find /` (PID 1024734 en VONTAR, saliendo a `/tmp/dcntl_investigation2.txt`)
puede seguir corriendo en segundo plano en la máquina real de forma
independiente a esta sesión; su resultado no es necesario ya que la ruta
correcta ya quedó confirmada por otra vía (grep local del repo, sin gastar
llamadas a Desktop Commander).

**No se avanzó** el trazado de `m_dcntl` (próximo paso concreto de la
Sección 41: valor de reset, uso en el bucle de ciclos, posible
reprogramación por el firmware) -- queda pendiente para la siguiente
intervención, ahora con la ruta correcta ya conocida de antemano
(`/home/spc/projects/mame-as2k-stable-arm64/src/devices/cpu/z180/z180.cpp`),
lo que debería permitir completarlo con margen dentro del límite de 5
llamadas.

Sin medición física. Estado del bloqueo: SIGUE ABIERTO. Cambios de esta
intervención: solo documentación (este archivo); sin cambios de código.

## Actualización 2026-11-27 (intervención horaria, continuación de la Sección 42 del doc confidencial)

Repo en VONTAR verificado al inicio: HEAD en `fix/irq1-ra2-wakeup` en
`aea307e` (fin de la Sección 42), sin avance en paralelo del usuario. Sin
cambios de código pendientes; solo los archivos sin trackear ya conocidos.

Con la ruta correcta ya confirmada (`/home/spc/projects/mame-as2k-stable-arm64/src/devices/cpu/z180/z180.cpp`
y `z180.h`), se completó el trazado de `m_dcntl` pedido por la Sección 41/42,
dentro del presupuesto de 5 llamadas a Desktop Commander:

1. **Valor de reset confirmado:** `z180.cpp:1741` (dentro de
   `device_reset()`): `m_dcntl = 0xf0; // maximum number of memory and I/O
   wait states`. Es un valor literal puesto por el propio código de MAME,
   con ese comentario explícito -- no una inferencia nuestra.
2. **Significado de los bits confirmado** (`z180.h:321-324`):
   `Z180_DCNTL_MWI1=0x80`, `MWI0=0x40`, `IWI1=0x20`, `IWI0=0x10`. Con
   `m_dcntl=0xF0` los cuatro bits están en 1: máximo de wait-states tanto de
   memoria como de I/O.
3. **Fórmulas de wait-states confirmadas** (`z180.h:180-181`):
   `memory_wait_states() = (m_dcntl & 0xc0) >> 6` → con `0xF0` da **3**
   wait-states de memoria por acceso. `io_wait_states() = (m_dcntl & 0x30)
   == 0 ? 0 : ((m_dcntl & 0x30) >> 4) + 1` → con `0xF0` da **4** wait-states
   de I/O por acceso.
4. **Dónde se consumen, confirmado parcialmente:** `memory_wait_states()` se
   usa en `z180.cpp` en el bloque de transferencia DMA (líneas 1131-1235 y
   1309, contexto con variables `bcr1/mar1/iar1`, es decir el canal DMA, no
   necesariamente el fetch de opcodes normal) y una vez más en línea 2034
   (`int extra_cycles = memory_wait_states(); ... handle_io_timers(3 +
   extra_cycles); m_icount -= 3 + extra_cycles;` -- contexto aún sin
   identificar con certeza, probablemente refresh/M1, PENDIENTE de
   confirmar). **No verificado todavía, y es el punto más importante que
   falta:** si el *fetch de opcode normal* (la ruta caliente de ejecución de
   cada instrucción del firmware, no solo DMA/refresh) también suma
   `memory_wait_states()`/`io_wait_states()` en cada acceso a memoria/IO. Si
   sí, el valor de reset `0xF0` (máximo) estaría multiplicando el costo de
   CASI CADA ACCESO A MEMORIA durante todo el arranque -- explicación
   cuantitativa plausible y fuerte del déficit de tiempo-a-SLP. Si no (si
   solo afecta DMA/refresh, que el firmware del Writer no parece usar
   activamente en el arranque), el efecto sería mucho menor y la pista
   perdería fuerza.
5. **Confirmado:** `src/mame/akt/writer.cpp` (ruta real del driver,
   corregida: vive en `mame-as2k-stable-arm64/src/mame/akt/writer.cpp`, NO
   en el repo público `writer-emu`) **no contiene ninguna referencia** a
   `DCNTL`, `dcntl`, ni al puerto `0x32` -- el driver nunca lee, escribe,
   intercepta ni loguea este registro. Cualquier valor que tenga en
   cualquier momento del arranque es puramente el que ponga (a) el reset de
   MAME (`0xF0`) o (b) la propia ROM del Writer vía `OUT` al puerto interno
   0x32, sin ninguna intervención del driver.
6. **No confirmado todavía (pendiente, próximo paso concreto):** si la ROM
   del Writer efectivamente reprograma `DCNTL` durante el arranque. Un grep
   rápido sobre `docs/` y `tools/` del repo público no encontró ninguna
   nota de desensamblado mostrando un `OUT` directo al puerto `0x32`
   (`tools/buscar_teclado.py` solo tiene una tabla de nombres de puerto que
   incluye `0x32: 'DCNTL'`, no un hallazgo de uso real). Esto requiere
   buscar directamente en el desensamblado de la ROM real (Ghidra / doc de
   Drive) un `OUT (032h),A` o equivalente cerca del vector de reset, no
   solo en los docs/tools del repo público.

**Hipótesis actualizada, más concreta, aún sin confirmar:** si el fetch de
opcode normal del Z180 también aplica wait-states de memoria/I/O según
`DCNTL`, y la ROM del Writer NO reprograma `DCNTL` hasta pasado cierto punto
del arranque (o nunca lo hace, asumiendo que el hardware real tiene otro
valor de reset o un circuito externo distinto), entonces el `0xF0` de MAME
(que es un valor de la plantilla genérica del core Z180, no necesariamente
el del Z8018x real usado en el Writer, ni necesariamente el que tendría un
Z180 recién salido de un `/RESET` externo del PIC en vez de un power-on
reset) podría estar insertando wait-states de memoria/I/O que el hardware
real no tiene en ese momento, alargando artificialmente el tiempo hasta SLP
más allá de lo que tardaría el silicio real. **Esto sigue siendo una
hipótesis, no una medición ni una confirmación** -- exactamente igual de
abierta que la de carrera de tiempos (Hallazgo 3), de la cual esta pista de
DCNTL podría ser la causa raíz subyacente (una carrera de tiempos real, pero
causada por wait-states emulados de más, no por el diseño original).

**Próximo paso concreto para la siguiente intervención:** (1) confirmar si
el fetch de opcode normal (no DMA) también suma `memory_wait_states()`
revisando el cuerpo de la función de ejecución principal del core (fuera de
los bloques DMA ya localizados); (2) si es así, medir el efecto real
forzando `m_dcntl=0` vía un parche de diagnóstico temporal (no vía un
`WRITER_TEST_*` existente -- no hay ninguno para DCNTL todavía) y comparar
el tiempo-a-SLP contra el valor confirmado hasta ahora de ~2.79s; (3) en
paralelo, revisar el desensamblado Ghidra de la ROM (doc de Drive) buscando
un `OUT` al puerto 0x32 cerca del vector de reset del Z180.

Sin medición física. Estado del bloqueo: SIGUE ABIERTO -- se avanzó el
trazado de `DCNTL` (valor de reset, significado de bits, fórmulas de
wait-states, y confirmación de que `writer.cpp` no lo toca), pero **no**
se confirmó todavía si eso explica cuantitativamente el déficit de tiempo
medido. Cambios de esta intervención: solo documentación (este archivo);
sin cambios de código.
