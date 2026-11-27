# ¿Qué bytes transmite realmente el PIC? — verificado 2026-11-27

## Pregunta

A partir del desensamblado (`PIC_ASCI1_HANDSHAKE_ANALYSIS_20261126.md`): el PIC
transmite 4 bytes desde RAM `0x08-0x0B`, inicializados en cero, pero
incrementados por la rutina `0x015` antes de cada llamada a `0x027`. ¿Cuáles
son los valores reales en el momento de transmitir (o, dado el mutual-wait,
en cualquier momento del arranque)?

## Instrumentación

Nuevo tap opt-in `WRITER_TRACE_PIC_RAM` en `src/mame/akt/writer.cpp`
(`pic_ram_trace_tick()`, timer a 10us, oversample seguro frente a
Tcy=~122us del PIC). Loggea PC + RAM `0x08-0x0C` cada vez que el PIC entra a
PC `0x015` (incremento), `0x027` (llamada a transmitir) o `0x0B4` (rama real
de transmisión). Dedup: solo loggea en transición de PC.

Corrida: 4s emulados, arranque normal (sin bypass), binario
`/home/spc/projects/WRITER_INSTRUCTOR/diag/writerdiag`, commit de esta
instrumentación. 253 entradas de traza, todas en `pc=015`.

## Resultado verificado

1. **`pc=027` y `pc=0B4`: CERO entradas en 4s.** Consistente con el hallazgo
   de `PIC_Z180_TXA1_MUTUAL_WAIT_20261127.md`: TXA1/RB7 nunca baja, así que
   el PIC jamás toma la rama de transmisión ni llama a `0x027`. **El PIC no
   transmite ningún byte en este modelo, en ningún momento del arranque
   normal.**
2. **El contador `0x0C` incrementa continuamente**, en pasos de 4 por cada
   visita a `0x015` (~15.6ms entre visitas), sin importar los ciclos de
   `/RESET` del PIC hacia el Z180 — es decir, **el contador NO se reinicia
   en cada retry**, solo en el reset maestro del propio PIC.
3. **El acarreo a `0x08` sí ocurre**: en los 4s de la corrida, `0x08` pasó de
   `00` a `03` (tres desbordamientos de `0x0C`). Patrones observados:
   `00 00 00 00` → `01 00 00 00` → `02 00 00 00` → `03 00 00 00`.
   `0x09-0x0B` no cambiaron (tomarían muchísimo más tiempo en desbordar).

## Conclusión

**No hay "los 4 bytes que envía el PIC" que reportar como payload real**,
porque en el modelo actual el PIC nunca llega a enviarlos: queda atrapado
indefinidamente en el mutual-wait antes de alcanzar `0x027`/`0x0B4`. Si
algún día se rompe el mutual-wait (p.ej. si la medición física revela que
TXA1 sí baja por una condición no modelada), el valor que se transmitiría
en ESE instante sería `(contador_de_ciclos_de_0x015_transcurridos / 64) % 256`
en el byte `0x08`, con `0x09-0x0B` en `00` salvo arranques extremadamente
largos — NO los `00 00 00 00` fijos que la inicialización sugería a
primera vista.

Esto es dato duro verificado por emulación, no requiere medición física.
