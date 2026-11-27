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
3. Una condición de handshake por el enlace serial PIC<->Z180 (bit-bang,
   baud rate/polaridad) que nunca se satisface, independiente de los tiempos
   de HALT (sigue sin medición física, ver `PIC16C54C_U11_ANALYSIS.md` sec.3).

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

## Hallazgo 4 (2026-11-27): bug de reloj ASCI1 (factor 2) confirmado contra la
hoja de datos Zilog; prueba decisiva de la carrera aún pendiente

`docs/ASCI1_FULL_PHI_AB_TEST_20261127.md` y
`docs/PIC_ASCI1_HANDSHAKE_ANALYSIS_20261126.md` establecen, citando la hoja de
datos oficial Zilog Z8018x UM005004-0918 (tabla 19/20, pág. 142-144), que el
ASCI1 real corre a 300 bps con PHI=3.072MHz, PS=0, SS=110, DR=0 -- y que el
modelo MAME actual instancia el canal 1 con `DERIVED_CLOCK(1,2)` (1.536MHz),
dando 150 bps: la mitad de lo correcto. Un flag opt-in
(`WRITER_TEST_ASCI1_FULL_PHI`, `z180.cpp` línea 1787) confirma por A/B el
cambio de reloj (1,536,000Hz -> 3,072,000Hz, mismo binario/ROM). Esto encaja
con la Sección 33 del doc de Drive: el Z180 pasa ~2.0s de sus ~2.79s hasta
SLP@0x0E27 esperando un byte serial del PIC; al doble de velocidad ese tramo
bajaría a ~1.0s, llevando el total a ~1.79s -- por debajo del periodo de
reintento del PIC (~2.016s), lo que rompería la carrera del Hallazgo 3 sin
tocar la lógica del PIC.

**Esto todavía NO está verificado.** Se encontró en VONTAR un resultado sin
commitear (`diag/error.log`, `diag/normalboot_fullphi.ppm`, timestamps
2026-11-27 00:37-00:39) cuyo encabezado es `TEST: PIC-driven Z180 /RESET
suppressed; measuring one uninterrupted boot-to-HALT/SLP attempt`: se corrió
con `WRITER_TEST_DISABLE_PIC_RESET` (el bypass ya conocido), NO con el reset
real del PIC activo. Ese log sí confirma que, con el reset suprimido, el Z180
llega a SLP@0x0E27 en t=2.788818s (igual que mediciones previas) y continúa
arrancando hasta imprimir texto de aplicación real en el LCD (120 escrituras
`rs=1`, incluye "...backup data as possible..." -- primera vez documentado
boot real más allá del init del LCD). El encabezado no indica si
`WRITER_TEST_ASCI1_FULL_PHI=1` estaba activo en esa corrida, así que **no
sirve como evidencia del fix de reloj** (el nombre del archivo lo sugiere,
pero el log no lo confirma).

**Prueba decisiva pendiente (siguiente paso, sin bypass de reset):** correr
`writerdiag` con el reset del PIC ACTIVO (sin `WRITER_TEST_DISABLE_PIC_RESET`)
y `WRITER_TEST_ASCI1_FULL_PHI=1`, >=15s reales con `-nothrottle`, y verificar:
(a) si el conteo de `PIC RA1=0 ... /RESET ASSERT` baja a 0 o se reduce
drásticamente frente a los ~14 ciclos/30s medidos sin el flag, y (b) si
aparecen escrituras `rs=1` (texto real) antes de cualquier reset. Si ambos se
cumplen, el Hallazgo 3 queda confirmado y resuelto por el fix de reloj ASCI1;
si el reset sigue ocurriendo igual, el factor 2 de reloj no es (o no es el
único) causante de la carrera y hay que medir el margen real con más
precisión.
