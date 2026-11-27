# The Writer: bucle infinito de /RESET del PIC en arranque normal (sin bypass)

**Fecha:** 2026-11-27. **Fuente:** trazas reales de `writerdiag` (instrumentación
existente `PIC RA1=... -> Z180 /RESET ASSERT/CLEAR`, ya presente en writer.cpp),
corridas en VONTAR. No es inferencia: son eventos reales logueados por la
emulación del PIC real (pic16c54_device ejecutando el dump físico de U11).

## Hallazgo

En una corrida normal (SIN `WRITER_TEST_DISABLE_PIC_RESET`, SIN ningún otro
bypass de diagnóstico), el PIC pulsa `/RESET` del Z180 de forma **periódica e
indefinida**, cada ~2.016s de tiempo real/emulado, durante toda la duración de
la corrida (probado hasta 30s reales con `-nothrottle`, 14+ ciclos completos
sin romperse nunca):

```
PIC RA1=0 (TRISA output) -> Z180 /RESET ASSERT t=2.599121s
PIC RA1=1 (TRISA input/floating+pullup) -> Z180 /RESET CLEAR t=2.743652s
PIC RA1=0 (TRISA output) -> Z180 /RESET ASSERT t=4.759399s
PIC RA1=1 (TRISA input/floating+pullup) -> Z180 /RESET CLEAR t=4.903931s
... (se repite cada ~2.016s, sin fin, hasta el final de la corrida)
```

Cada vez que el Z180 se reinicia, vuelve a correr su secuencia de inicialización
de LCD desde cero (de ahí los mensajes repetidos `LCDWR ... data=30/30/30/38/
0C/06/01/80/0C`, y las advertencias benignas del device KS0066 `"function set
cannot be executed after other instructions..."`, que son resultado esperado
de reintentar la secuencia de "despertar" del HD44780 sin un reset limpio de
sus variables internas entre corridas).

**Consecuencia directa:** el firmware del Z180 NUNCA llega a ejecutar el código
que imprime texto real en el LCD (confirmado: 0 escrituras `rs=1` en 30s reales
de corrida normal), porque se reinicia antes de llegar ahí. El único modo en
que SÍ llega a imprimir texto real (confirmado: mensaje real `"MEMORY ERROR
WARNING!!! Data may be corrupted! ..."`) es forzando
`WRITER_TEST_DISABLE_PIC_RESET=1`, que suprime este reinicio.

## Relación con hallazgos previos (ver doc confidencial de Drive, secciones 30-34)

Esto es consistente con el mecanismo ya documentado del lado del PIC: el
bucle de reintento de hasta 256 iteraciones (pulso RA2 + sondeo TMR0/PORTB,
ver `PIC16C54C_U11_ANALYSIS.md`), que al agotarse sin encontrar la condición
esperada, actúa sobre RA1 para reiniciar el Z180. El periodo medido (~2.016s)
es coherente con el periodo de ~2.0-2.09s ya encontrado en el spin-loop
Z180 (Sección 33 del doc confidencial).

## Hipótesis a evaluar (ninguna confirmada todavía)

1. **Comportamiento esperado en frío.** En hardware real, el PIC + Z180
   normalmente arrancan con la SRAM (con supercap C38) ya conteniendo un
   estado válido de una sesión anterior. Una emulación "en frío" (SRAM
   completamente vacía/sin firma de Master Reset) podría estar replicando
   fielmente un estado que en la práctica casi nunca ocurre en el equipo real
   (solo tras cambiar la pila/supercap). Si es así, esto NO sería un bug de
   emulación sino el comportamiento correcto para ese caso límite.
2. **Condición de handshake nunca satisfecha en emulación.** El PIC espera
   alguna respuesta del Z180 (vía el enlace serial RB0->RXA1 o el pulso
   T0CKI) dentro de la ventana de reintento, y esa respuesta podría no estar
   llegando por una razón distinta a "SRAM vacía" -- posible desajuste de
   timing/polaridad en el enlace serial bit-bang (ya marcado como pendiente
   de medición física en `PIC16C54C_U11_ANALYSIS.md`, sección 3).
3. **Combinación de ambas.**

## Siguiente paso sugerido

Para distinguir (1) de (2) sin medición física: probar con
`WRITER_TEST_VALID_RAM_SIGNATURE=1` (bypass ya existente en writer.cpp que
inyecta la firma de Master Reset D000/D002) en una corrida SIN
`WRITER_TEST_DISABLE_PIC_RESET`, y ver si el ciclo de reset se rompe solo con
eso. Si se rompe, apunta a (1). Si sigue reiniciando igual, apunta a (2) y
el bit-bang serial real sí necesita medición física (ya pospuesta por el
usuario).
