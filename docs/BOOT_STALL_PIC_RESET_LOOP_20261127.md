> **Nota de este documento:** las "Secciones" referenciadas abajo con numeración (39, 40, 41, 42, 43, 44...) corresponden a la numeración del documento confidencial de Google Drive (WRITER_RE_08), que contiene el detalle técnico completo. Este archivo público resume solo lo necesario para seguimiento, sin RE confidencial.

# The Writer: bucle infinito de /RESET del PIC en arranque normal (sin bypass)

**Fecha:** 2026-11-27 (actualizado 2026-11-28). **Fuente:** trazas reales de `writerdiag` (instrumentación
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

**Actualización 2026-11-28 (ver Hallazgo 6): esta "carrera de tiempos" queda
reinterpretada. No es un simple margen insuficiente: es un mutual-wait
(Hallazgo 5 más abajo) causado, a su vez, por un bug concreto y localizado en
el core Z180 ASCI de MAME (Hallazgo 6), identificable sin medición física.**

## Hallazgo 4 (2026-11-27): la corrección ASCI1 full-PHI NO resuelve el bloqueo

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
efecto real de hardware. No se investigó más a fondo; no cambia la
conclusión principal porque el tiempo-a-SLP fue IDÉNTICO en ambas corridas
con bypass.

## Hallazgo 5 (2026-11-27, esta intervención): DCNTL/wait-states REFUTADO como causa del déficit de tiempo

Se confirmó, leyendo directamente `z180.cpp` en el árbol compartido
(`mame-as2k-stable-arm64/src/devices/cpu/z180/z180.cpp`), que
`memory_wait_states()`/`io_wait_states()` (derivados de `DCNTL`, ver
Secciones 41-43 del doc confidencial) **solo** se invocan desde las
funciones de transferencia DMA (`z180_dma0()`/`z180_dma1()`) y desde la
función separada `burn_cycles()` -- **nunca** desde el bucle normal de
`execute_run()`, que ejecuta cada instrucción como
`curcycles = exec_op(ROP()); curcycles += m_extra_cycles;` sin ninguna
llamada a esas funciones. Esto descarta la hipótesis de que el valor de
reset `DCNTL=0xF0` (máximo de wait-states) esté insertando ciclos extra en
cada acceso a memoria/I/O del arranque normal, a menos que el firmware
active DMA o invoque `burn_cycles()` durante esos ~2.79s -- no verificado,
pero sin evidencia de uso de DMA en el arranque hasta ahora. Detalle
completo en la Sección 44 del doc confidencial de Drive.

## Hallazgo 6 (2026-11-27/28): mutual-wait confirmado entre PIC y Z180 en el handshake TXA1/RXA1

Instrumentación `WRITER_TRACE_PIC_PORTB` + `WRITER_TRACE_ASCI1_RX` (4s
emulados, arranque normal sin bypass): 207 lecturas del PIC en su PC de
espera (`0x0B0`) devuelven siempre `PORTB=0x86` (RB7=1, TXA1 alto). El PIC
jamás toma la rama de transmisión de sus 4 bytes (`0x0B4-0x0BB`) porque esa
rama sólo se toma cuando RB7 (=TXA1) está en bajo.

Auditoría completa del desensamblado ROM (10,033 líneas Ghidra): **no existe
ninguna instrucción `OUT0 (0x08),r`** (escritura a TDR1, el registro de
datos de transmisión ASCI1) en todo el firmware analizado. El Z180 nunca
transmite un byte de datos normal por TXA1. En paralelo, el Z180 configura
ASCI1 (CNTLB1=0x06, CNTLA1=0x64 con RE=1/TE=1) y entra en un bucle de
espera de recepción (`ram:062f`, lectura de STAT1/RDR1) sin enviar nada
primero.

Resultado: **mutual-wait real, confirmado por trazas, no solo hipotético**:
1. El PIC espera TXA1 (RB7) en bajo antes de transmitir sus 4 bytes.
2. El Z180 espera datos del PIC por RXA1 y nunca inicia una transmisión
   TXA1 vía TDR1.
3. TXA1 se queda alto indefinidamente; el PIC nunca sale de su bucle de
   espera, agota su contador de reintento y reinicia el Z180.

El tap `WRITER_TRACE_PIC_RAM` (RAM 0x08-0x0C del PIC) confirma además que,
en consecuencia, **el PIC nunca llega a transmitir ningún byte** en ningún
momento del arranque normal observado.

Detalle completo, incluyendo auditoría de todas las instrucciones `OUT`/
`OUT (C)` del firmware y la comprobación de dirección TRISB del PIC, en
`docs/PIC_Z180_TXA1_MUTUAL_WAIT_20261127.md` y
`docs/PIC_TX_PAYLOAD_BYTES_20261127.md` (este repositorio, commit `75eef5f`).

## Hallazgo 7 (2026-11-28): CAUSA RAÍZ IDENTIFICADA — bug concreto en el core Z180 ASCI de MAME, verificable por código, SIN medición física

El Hallazgo 6 dejaba abierta una pregunta: ¿por qué TXA1 nunca baja, si el
protocolo del firmware tiene un mecanismo explícito para forzarlo? La
respuesta se encontró leyendo directamente `z180asci.cpp` (árbol compartido
`mame-as2k-stable-arm64/src/devices/cpu/z180/`), sin necesidad de tocar
hardware real.

### El firmware SÍ tiene un mecanismo para bajar TXA1: el bit BRK_SEND de ASEXT1

El Z180 tiene un registro `ASEXT1` (puerto `0x13`) con un bit `BRK_SEND`
(forzar condición de "break", TXA1 en bajo continuo, independiente de
cualquier dato en TDR1 -- mecanismo estándar Zilog, no requiere haber
transmitido nunca un byte normal). El audit previo (Hallazgo 6 /
`PIC_Z180_TXA1_MUTUAL_WAIT_20261127.md`) ya había localizado que la rutina
de arranque Z180 llama a `0x046b` (que escribe ASEXT1 con `AND 0xFC OR
0x05`, es decir: `BRK_EN=1` y `BRK_SEND=1`) inmediatamente después de
configurar ASCI1 y antes de entrar al bucle de espera de recepción
(`ram:062f`). Es decir: **el firmware sí intenta poner TXA1 en bajo antes de
esperar datos del PIC.** Esto no se había investigado hasta ahora porque el
audit previo sólo confirmó "no es un TDR1 write" sin preguntar qué hace
realmente ASEXT1.

### El bug: el estado BREAK se fija lógicamente pero nunca se refleja en el pin físico

Código relevante de `z180asci.cpp` (rutas confirmadas por lectura directa):

```cpp
void z180asci_channel_base::asext_w(uint8_t data)
{
    if (m_asci_ext & Z180_ASEXT_BRK_EN)
        m_tx_state = (m_asci_ext & Z180_ASEXT_BRK_SEND) ? STATE_BREAK : STATE_WAIT;
    device_clock_changed();   // <- cancela el timer de transmisión (ver abajo)
}

void z180asci_channel_base::device_clock_changed()
{
    ...
    m_tra_clock->adjust(attotime::never);   // <- el timer de TX queda SIEMPRE detenido aquí
    ...
    if (!m_sample_rate.is_never())
        m_rcv_clock->adjust(m_sample_rate, 0, m_sample_rate);  // sólo se rearma el de RX
}

void z180asci_channel_base::transmit_edge()   // única función que llama output_txa() para STATE_BREAK
{
    if (m_asci_cntla & Z180_CNTLA_TE)
    {
        switch (m_tx_state)
        {
        case STATE_DATA:  output_txa(BIT(m_tsr, 0)); ...
        case STATE_WAIT:  ...
        case STATE_BREAK: output_txa(0); break;   // <- esto es lo único que pondría TXA1 en bajo
        }
    }
}

void z180asci_channel_base::tdr_w(uint8_t data)   // ÚNICO lugar que rearma m_tra_clock
{
    ...
    if (!m_bit_rate.is_never())
        m_tra_clock->adjust(m_bit_rate, 0, m_bit_rate);
}
```

`transmit_edge()` (la única función que ejecuta `output_txa(0)` para
`STATE_BREAK`) sólo se invoca periódicamente desde el timer `m_tra_clock`
(hay también una ruta alternativa vía reloj externo `cka_wr()`, pero el
canal 1 fuerza `CKA1D=1` -- reloj externo deshabilitado -- y de todas formas
no hay ninguna señal física de reloj externo cableada a CKA1 en este
hardware). `m_tra_clock` **sólo se arma en `tdr_w()`**, es decir, sólo
cuando el firmware escribe un byte de datos real en TDR1.

Pero el Hallazgo 6 ya demostró, por auditoría exhaustiva del desensamblado,
que **el firmware nunca ejecuta `OUT0 (0x08),r` (TDR1)**. Por tanto
`m_tra_clock` nunca se arma, `transmit_edge()` nunca se ejecuta por esa vía,
y cuando el firmware escribe ASEXT1 con `BRK_SEND=1` (en `0x046b`), la
llamada a `asext_w()` SÍ fija `m_tx_state = STATE_BREAK` correctamente,
pero inmediatamente después `device_clock_changed()` **cancela
explícitamente** el único timer que podría ejecutar `output_txa(0)`. El
resultado: `m_asci_ext` (legible por software) refleja `BRK_SEND=1`
correctamente, pero el pin físico `TXA1` **nunca cambia**, porque
`output_txa()` nunca vuelve a ejecutarse para ese canal.

Se confirmó además (lectura de `writer.cpp`) que el cableado del callback
es correcto: `output_txa()` → `m_txa_handler` → `writer_state::z180_txa1_w()`
→ `m_z180_txa1` → bit `0x80` (RB7) leído por `pic_portb_r()`. Si
`output_txa(0)` se ejecutara, el PIC lo vería correctamente. El problema
está enteramente en que esa llamada nunca ocurre.

### Por qué esto NO es (sólo) una carrera de tiempos

Esto reemplaza la lectura del Hallazgo 3: no es que el Z180 tarde "demasiado"
en cumplir su parte del protocolo -- **el Z180 emulado es estructuralmente
incapaz de cumplir su parte del protocolo de break tal como está implementado
el core ASCI de MAME**, sin importar cuánto tiempo se le dé. El PIC puede
esperar para siempre: TXA1 nunca bajará. La condición de carrera real
(Hallazgo 3) sigue siendo un fenómeno observable (el PIC se rinde en ~2.016s
contra un presupuesto de ~2.79s), pero es secundaria: aunque el PIC tuviera
un timeout infinito, el arranque normal igual se quedaría colgado para
siempre con el modelo actual, porque el break nunca se refleja en el pin.

### Parche candidato (NO aplicado todavía -- pendiente de build+test)

En `asext_w()`, reflejar el cambio de estado de break inmediatamente en el
pin físico, en vez de depender exclusivamente del timer de transmisión de
datos (que es semánticamente una señal distinta: bit-clock de datos, no
"forzar nivel de línea"). Algo equivalente a:

```cpp
void z180asci_channel_base::asext_w(uint8_t data)
{
    if (m_asci_ext & Z180_ASEXT_BRK_EN)
    {
        m_tx_state = (m_asci_ext & Z180_ASEXT_BRK_SEND) ? STATE_BREAK : STATE_WAIT;
        if (m_tx_state == STATE_BREAK)
            output_txa(0);
        else if (m_asci_stat & Z180_STAT_TDRE)   // sólo si no hay transmisión de datos en curso
            output_txa(1);
    }
    device_clock_changed();
}
```

Esto modela el comportamiento real del Z8018x: BRK_SEND fuerza la línea de
forma inmediata y asíncrona (no depende del baud-rate generator ni de haber
cargado antes un byte en TDR1), consistente con el uso de "break" como señal
de protocolo fuera de banda, que es exactamente para lo que este firmware
lo está usando.

**No se modificó ningún archivo de código en esta intervención.** El cambio
propuesto es en `mame-as2k-stable-arm64/src/devices/cpu/z180/z180asci.cpp`
(árbol compartido, fuera de este repositorio público) y requiere
recompilar `writerdiag` y verificar con `WRITER_TRACE_PIC_PORTB=1` que RB7
baja a 0 tras `0x046b`, antes de aplicarlo como comportamiento por defecto.

## Hipótesis actualizadas (estado al 2026-11-28)

1. ~~Comportamiento esperado para SRAM en frío.~~ **DESCARTADA** (Hallazgo 2).
2. ~~Carrera de tiempos PIC/Z180 pura (el Z180 simplemente tarda más de lo
   que el PIC espera).~~ **SUPERADA/REFINADA por el Hallazgo 7**: el Z180
   nunca podría ganar esa carrera con el modelo actual, independientemente
   del tiempo disponible, porque el mecanismo de break que debería bajar
   TXA1 está roto en el core ASCI de MAME.
3. ~~Una condición de handshake por el enlace serial PIC<->Z180 (bit-bang,
   baud rate/polaridad) que nunca se satisface.~~ **Descartada como causa
   de la carrera de tiempos** (Hallazgo 4). El baud rate/polaridad exacto
   del enlace bit-bang sigue sin medición física pero no es la causa del
   bloqueo de arranque.
4. ~~Wait-states de memoria/I/O emulados de más (DCNTL=0xF0, máximo) en el
   fetch normal de opcodes.~~ **DESCARTADA** (Hallazgo 5).
5. **Mutual-wait TXA1/RXA1 entre PIC y Z180** (Hallazgo 6): confirmado por
   trazas reales, no sólo inferido.
6. **Causa raíz del mutual-wait: bug en `z180asci.cpp` (core MAME, no
   firmware)** -- `STATE_BREAK` nunca ejecuta `output_txa(0)` porque el
   único disparador de `transmit_edge()` (el timer `m_tra_clock`) sólo se
   arma en `tdr_w()`, y el firmware de este producto nunca escribe TDR1.
   **Esta es, a la fecha, la hipótesis más fuerte, fundamentada en lectura
   directa de código fuente (no inferencia ni medición física), y es
   corregible por software.**

## Próximos pasos posibles

1. Aplicar el parche candidato del Hallazgo 7 a `z180asci.cpp` en una rama
   de pruebas del árbol compartido `mame-as2k-stable-arm64` (fuera de este
   repo), recompilar `writerdiag` (`make -j2 SUBTARGET=writerdiag`) y
   verificar con `WRITER_TRACE_PIC_PORTB=1` que RB7 baja a 0 después de que
   el Z180 ejecuta `0x046b`.
2. Si RB7 baja correctamente, verificar si el PIC progresa a su rama de
   transmisión (`0x0B4-0x0BB`) y si el bucle de `/RESET` finalmente se
   rompe en una corrida de arranque normal (sin ningún `WRITER_TEST_*`
   bypass).
3. Confirmar que el parche no rompe ningún otro driver de MAME que use
   `Z180ASCI_CHANNEL` con break (buscar otros usos de `ASEXT_BRK_SEND` en
   el árbol de MAME antes de proponerlo como cambio general del core).
4. Mantener pendiente, como antes, la medición física del periodo real de
   reintento del PIC y del enlace bit-bang RB0→RXA1 -- pausada por el
   usuario, y en principio ya no necesaria para cerrar ESTE bloqueo si el
   parche de break confirma la causa raíz.

## Nota de sincronización (2026-11-27, intervención anterior)

El repo en VONTAR tiene su rama `fix/irq1-ra2-wakeup` en el commit local
`0fa0944` (de Samuel, 2026-11-27 09:42), que **no se ha sincronizado** con
los commits posteriores empujados directamente a GitHub (`aea307e`,
`595d038`, y este mismo commit) -- todos parten del mismo ancestro común
(`cda102c`) pero han divergido. Esta intervención no tuvo presupuesto de
Desktop Commander disponible para resolver esto en VONTAR directamente
(los 5 llamadas se usaron en el análisis de DCNTL). **Pendiente:** la
próxima intervención con acceso a VONTAR debe hacer `git fetch && git merge
origin/fix/irq1-ra2-wakeup` (o equivalente) antes de continuar, igual que
se hizo antes con `cda102c`.

**Actualización 2026-11-28:** verificado al inicio de esta intervención que
VONTAR ya estaba sincronizado (`HEAD` local = `origin/fix/irq1-ra2-wakeup`
= `75eef5f`, sin divergencia). La nota anterior queda resuelta.

Sin medición física. Estado del bloqueo: SIGUE ABIERTO, pero con causa raíz
identificada y no confirmada todavía por una corrida real del parche
(Hallazgo 7). Próxima intervención: aplicar y probar el parche candidato.

## Nota de sincronización e intento de build (2026-11-28, intervención horaria)

Verificado al inicio: VONTAR (`/mnt/sd/projects/WRITER_INSTRUCTOR/writer-emu`)
tiene `HEAD` en `75eef5f`, es decir **un commit detrás** de
`origin/fix/irq1-ra2-wakeup` (`fee577e`, el commit de esta misma
intervención anterior que documentó el Hallazgo 7). Mismo patrón que en
intervenciones previas: el commit de documentación se empujó directo a
GitHub sin pasar por VONTAR. **Pendiente:** la próxima intervención con
presupuesto de Desktop Commander debe hacer `git -C
/mnt/sd/projects/WRITER_INSTRUCTOR/writer-emu pull` antes de tocar nada
más. También hay archivos sin trackear en ese working tree (`cfg/`,
`error.log`, `snap/`, `tools/__pycache__/`, `tools/dis_driver.py`) --
`tools/dis_driver.py` en particular parece una herramienta nueva (no
documentada todavía) que probablemente merezca revisarse y, si es
reutilizable, commitearse.

Esta intervención intentó localizar `z180asci.cpp` en el árbol de VONTAR
para poder aplicar y compilar el parche candidato del Hallazgo 7, pero
**se agotó el presupuesto de 5 llamadas a Desktop Commander durante el
reconocimiento** (las primeras 3 llamadas se gastaron en búsquedas `find`
de filesystem completo que fueron lentas). Resultado del reconocimiento,
antes de agotar el presupuesto:

- Carpeta de diagnóstico confirmada: `/home/spc/projects/WRITER_INSTRUCTOR/diag/`
  (contiene `writerdiag` de 2026-11-27 08:44, `build.log`, y varias
  carpetas de sesiones de traza del 2026-11-26/27: `asci1_ab_20261127`,
  `asci1_handshake_20261127`, `asci1_rx_trace_20261127`,
  `pic_portb_trace_20261127`, `pic_portb_pc_trace_20261127`,
  `pic_portb_pc_trace2_20261127` -- todas de la intervención que generó el
  Hallazgo 6/7, ningún archivo nuevo desde entonces).
- `build.log` confirma el último build exitoso (2026-11-27 08:44:22,
  velocidad media 935%, `RUN_EXIT=0`) pero **no contiene** una ruta de
  fuente (`SRC_DIR`/`mame-as2k`) grepeable con el patrón usado.
- `find` con profundidad 8 bajo `/mnt/sd/projects/WRITER_INSTRUCTOR` y
  `/home/spc/projects/WRITER_INSTRUCTOR` **no encontró** `z180asci.cpp`.
  Es decir, el árbol compartido `mame-as2k-stable-arm64` referenciado en
  el Hallazgo 7 **no vive bajo ninguna de esas dos rutas** (al menos no a
  profundidad ≤8), o vive en un punto de montaje/ruta distinto (p. ej.
  directamente bajo `/mnt/sd/` o `~spc/` sin el prefijo
  `WRITER_INSTRUCTOR`).

**Ningún archivo de código tocado, ningún build lanzado, ningún comando
destructivo ejecutado.** Solo lectura/reconocimiento. Esta sección es el
único cambio de esta intervención (más la nota equivalente en el doc de
Drive).

**Próxima intervención (orden sugerido para no repetir el desperdicio de
presupuesto):**

1. `git -C /mnt/sd/projects/WRITER_INSTRUCTOR/writer-emu pull` (resolver
   el commit de atraso).
2. Para localizar `z180asci.cpp` sin un `find` global lento: revisar
   `build_writerdiag.sh` (raíz de `WRITER_INSTRUCTOR`) y/o `build.log`
   completo (no solo grep del patrón usado hoy) para extraer la ruta
   exacta del árbol MAME que usa el build; alternativamente
   `find ~spc -maxdepth 3 -maxdepth 3 -iname "mame-as2k*"` o
   `find / -maxdepth 3 -iname "mame-as2k*" 2>/dev/null` (profundidad baja,
   rápido) en vez de profundidad alta bajo rutas ya descartadas.
3. Una vez localizado: aplicar el parche candidato del Hallazgo 7 a
   `asext_w()` en la copia usada por el build de `writerdiag` (la propia
   carpeta de proyecto, no un árbol compartido con otros fines),
   recompilar (`make -j2 SUBTARGET=writerdiag`, o el script
   `build_writerdiag.sh`), y verificar con `WRITER_TRACE_PIC_PORTB=1` si
   RB7 baja a 0 tras `0x046b` y si el bucle de `/RESET` se rompe en un
   arranque normal sin bypass.
4. Antes de generalizarlo como cambio de core: grep de
   `ASEXT_BRK_SEND`/`STATE_BREAK` en otros drivers del árbol MAME que usen
   `Z180ASCI_CHANNEL` (sigue pendiente, no se hizo esta vez tampoco).

Estado del bloqueo: **SIGUE ABIERTO**, sin cambios respecto a la
intervención anterior (causa raíz identificada por lectura de código,
Hallazgo 7, parche candidato redactado pero todavía sin aplicar/compilar/
probar).

## Intervención 2026-11-28 (horaria): parche confirmado carácter-por-carácter, aplicación bloqueada por el clasificador de seguridad -- requiere decisión de Samuel

Verificado al inicio: VONTAR seguía un commit atrás (`75eef5f` vs
`origin` en `ff37870`). Se hizo `git pull` en VONTAR sin incidentes
(fast-forward limpio); VONTAR queda sincronizado en `ff37870`.

Se localizó el árbol MAME real que usa `build_writerdiag.sh`
(`SRC=/home/spc/projects/mame-as2k-stable-arm64`, confirmado leyendo el
script, no por `find` global) y se leyó directamente
`src/devices/cpu/z180/z180asci.cpp` en esa ruta exacta. El código real de
`asext_w()`, `device_clock_changed()`, `tdr_w()` y `transmit_edge()`
coincide carácter por carácter con lo citado en el Hallazgo 7 -- el bug
queda confirmado por lectura directa de la copia que realmente se
compila, no solo inferido de una copia distinta. (Nota al margen: existe
una segunda copia idéntica de ese árbol en `/mnt/sd/projects/mame-as2k-stable-arm64`
-- `diff` confirma contenido idéntico -- pero `build_writerdiag.sh` usa la
de `/home/spc/projects/`, que es la que importa.)

**Se intentó aplicar el parche candidato del Hallazgo 7** directamente
sobre ese archivo vía Desktop Commander (`edit_block`). La acción fue
**bloqueada por el clasificador de seguridad de la sesión** ("Destructive
or Unwanted Actions"), porque modifica un archivo fuera de este
repositorio, en un árbol compartido con otros proyectos hermanos
(`as2k`/`as2kdiag`/`aspro`, según el propio comentario de
`build_writerdiag.sh`). Esta intervención, al ser automática y sin
usuario presente, **no intentó rodear el bloqueo** (p. ej. vía `sed`/
`python` por `start_process`) -- eso habría sido burlar deliberadamente
la misma restricción de seguridad bajo otro mecanismo, algo que las
reglas de esta tarea prohíben explícitamente.

**Resultado: ningún archivo de código fue modificado, ningún build se
lanzó, `writerdiag` sigue siendo el binario de 2026-11-27 08:44 sin
cambios.** Huella del archivo sin parchear en VONTAR al momento de esta
intervención, para detectar si cambia por otra vía:
`z180asci.cpp`, 20420 bytes, mtime `2026-11-27 06:53` (ambas copias).

### Esto requiere una decisión explícita de Samuel, no de una ejecución automática

El parche en sí está completamente redactado y verificado contra el
código real (ver Hallazgo 7 arriba). Lo que falta no es análisis: es que
alguien con permiso explícito lo aplique. Dos caminos, a elección de
Samuel:

1. **Aplicarlo él mismo**, interactivamente, en VONTAR (o donde prefiera)
   sobre `/home/spc/projects/mame-as2k-stable-arm64/src/devices/cpu/z180/z180asci.cpp`
   (y, si quiere mantener la segunda copia en sync, también en
   `/mnt/sd/projects/mame-as2k-stable-arm64/...`). El diff exacto es el
   del parche candidato del Hallazgo 7, arriba. Una vez aplicado, la
   siguiente ejecución horaria puede recompilar (`bash
   /home/spc/projects/WRITER_INSTRUCTOR/build_writerdiag.sh`, o el mismo
   comando con `WRITER_TRACE_PIC_PORTB=1 WRITER_TRACE_ASCI1_RX=1
   WRITER_TRACE_PIC_RAM=1` exportadas) y verificar si RB7 baja a 0 tras
   `0x046b` y si el bucle de `/RESET` se rompe en un arranque normal.
2. **Autorizar explícitamente**, en una sesión interactiva con Samuel
   presente, que una futura ejecución automática aplique este parche
   específico sobre este archivo específico -- esa autorización puede
   entonces quedar registrada aquí para que las siguientes ejecuciones
   horarias no vuelvan a toparse con el mismo bloqueo.

Sin una de las dos, **las próximas ejecuciones horarias no deben volver a
intentar `edit_block` sobre `z180asci.cpp`** (se bloqueará igual) --
deben limitarse a comprobar si el archivo cambió (huella de arriba) y,
si cambió, proceder directo a recompilar y verificar.

Estado del bloqueo: **SIGUE ABIERTO**. Causa raíz confirmada por lectura
directa del árbol de compilación real; parche listo; aplicación
pendiente de decisión/acción humana explícita, no de más análisis
automático.

## Intervención 2026-11-28 (horaria, continuación): sin cambios, sigue pendiente decisión de Samuel

Verificado al inicio: la huella de `z180asci.cpp` (ambas copias,
`/home/spc/projects/mame-as2k-stable-arm64/...` y
`/mnt/sd/projects/mame-as2k-stable-arm64/...`) es **idéntica** a la
registrada en la intervención anterior -- 20420 bytes, mtime
`2026-11-27 06:53`, SHA256
`96b23fd1435b26da8af28aa390d48d579c899c7399f24ee6bf18fa51039c1cea`.
Samuel todavía no ha aplicado el parche del Hallazgo 7.

Conforme a lo que la propia intervención anterior dejó indicado, esta
ejecución **no reintentó `edit_block`** sobre ese archivo (se bloquearía
igual por el clasificador de seguridad) y no hizo más análisis nuevo: no
hay nada más que investigar hasta que el parche se aplique.

Única acción realizada: `git pull` en VONTAR (fast-forward limpio
`ff37870` → `116f0ea`, sin divergencia). Se usaron 2 de las 5 llamadas a
Desktop Commander disponibles (huella de archivo + pull); las 3 restantes
no se usaron, ya que no hay trabajo adicional posible sin el parche.

**Pendiente, sin cambios:** decisión explícita de Samuel -- aplicar él
mismo el parche candidato del Hallazgo 7 sobre `z180asci.cpp` (ruta real
de build: `/home/spc/projects/mame-as2k-stable-arm64/src/devices/cpu/z180/z180asci.cpp`),
o autorizar explícitamente que una ejecución automática futura lo haga.
Mientras eso no ocurra, las próximas intervenciones horarias deben
limitarse a comprobar la huella del archivo.

Estado del bloqueo: **SIGUE ABIERTO**, sin novedad desde la intervención
anterior.

## Intervención 2026-11-28 (horaria, continuación): fingerprint sin cambios; limpieza de housekeeping pendiente desde hacía varias intervenciones

Verificado al inicio: huella de `z180asci.cpp` (ambas copias) **idéntica**
a la registrada en las dos intervenciones anteriores -- 20420 bytes, mtime
`2026-11-27 06:53`, SHA256
`96b23fd1435b26da8af28aa390d48d579c899c7399f24ee6bf18fa51039c1cea`. Samuel
todavía no ha aplicado el parche del Hallazgo 7 ni ha autorizado
explícitamente que una ejecución automática lo haga. Se revisó también el
doc de Drive completo (secciones 1-47) buscando cualquier nota manual de
Samuel ("Samuel:", "decisión:", etc.) -- no hay ninguna; la última entrada
(Sección 47) es la misma intervención ya registrada aquí como
"sin cambios". Conforme a lo indicado, esta ejecución **no reintentó
`edit_block`** sobre `z180asci.cpp`.

Con las 5 llamadas a Desktop Commander disponibles y sin trabajo posible
sobre el bloqueo principal, se atendió un pendiente de housekeeping que
varias intervenciones venían señalando sin actuar (`git status` en VONTAR
mostraba `tools/dis_driver.py` sin trackear desde al menos la Sección 45):

1. Se revisó `tools/dis_driver.py` (27 líneas): wrapper CLI sobre
   `tools/z180dis.decode()` que mapea un PC lógico a offset físico de ROM
   (`0xC0000 + PC`, según CBAR/CBR/BBR de `writer.cpp`) y vuelca 40
   instrucciones decodificadas desde las direcciones hex dadas en
   `argv`. Es una herramienta de análisis reutilizable (no exploratoria/de
   prueba), útil para el próximo paso pendiente del WRITER_RE_08 (revisar
   regiones bancadas fuera de la ventana de 64 KiB ya analizada por
   Ghidra, buscando `OUT0 (0x33),A` o escrituras a `0xD700/0xD701`) --
   conforme a la regla fija del proyecto ("commit y push automáticos al
   cerrar cada tarea legítima"), se comiteó y subió en VONTAR
   (`e2ae945`).
2. `cfg/`, `snap/`, `error.log` y `tools/__pycache__/` también aparecían
   sin trackear en cada ejecución horaria -- son artefactos regenerados
   por cada corrida de `writerdiag`, no código fuente. Se añadieron a
   `.gitignore` en VONTAR (`b650abc`) para que dejen de aparecer como
   pendientes en los próximos check-ins.

Ningún archivo del árbol MAME compartido (`z180asci.cpp`) fue tocado.
`writerdiag` sigue siendo el binario de 2026-11-27 08:44, sin recompilar
(no hay motivo para recompilar sin el parche aplicado). El trabajo
físico/hardware (pines J1/RB2, continuidad RAM/U21, timing del PIC) sigue
pausado, sin tocar, como indica la regla fija del proyecto.

**Pendiente, sin cambios respecto a las intervenciones anteriores:**
decisión explícita de Samuel sobre el parche del Hallazgo 7 en
`z180asci.cpp` (ruta real de build:
`/home/spc/projects/mame-as2k-stable-arm64/src/devices/cpu/z180/z180asci.cpp`).
Mientras eso no ocurra, las próximas intervenciones horarias deben seguir
limitándose a comprobar la huella del archivo.

Estado del bloqueo: **SIGUE ABIERTO**. Sin novedad sobre la causa raíz
(Hallazgo 7, ya confirmado contra el árbol real de compilación); esta
intervención solo hizo housekeeping del repo, no avanzó el bloqueo en sí.
