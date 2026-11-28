# Hallazgo 7: parche aplicado y verificado (2026-11-28)

**Autorización:** Samuel, explícitamente, en sesión interactiva (2026-11-28,
continuación de la intervención horaria programada). Texto de la
autorización: "Autorizo, documenta los cambios de código fuente para dar
trazabilidad".

**Estado del bloqueo tras este cambio: RESUELTO Y VERIFICADO** (ver sección
"Verificación" abajo). El bucle infinito de `/RESET` descrito en
`docs/BOOT_STALL_PIC_RESET_LOOP_20261127.md` ya no ocurre en un arranque
normal, sin ningún bypass de diagnóstico.

## Dónde vive el archivo parcheado

El archivo modificado **no pertenece a este repositorio público** (regla
fija del proyecto: nunca subir binarios de ROM/firmware al repo público;
este archivo tampoco es ROM/firmware, es el core Z180 ASCI compartido de
MAME, pero igualmente vive fuera de este árbol). Vive en el árbol de build
compartido con los proyectos hermanos (as2k/as2kdiag/aspro), en VONTAR:

- `/home/spc/projects/mame-as2k-stable-arm64/src/devices/cpu/z180/z180asci.cpp`
- `/mnt/sd/projects/mame-as2k-stable-arm64/src/devices/cpu/z180/z180asci.cpp`
  (mismo inodo que la ruta anterior, confirmado con `ls -li`: **es el mismo
  archivo físico montado en dos rutas**, no una copia separada como se
  documentó por error en intervenciones anteriores — ambas rutas quedan
  parcheadas con una sola edición).

Esta sección documenta el cambio de código fuente para trazabilidad, tal
como pidió Samuel, aunque el archivo en sí no se sube aquí.

## Huella del archivo

| | Antes del parche | Después del parche |
|---|---|---|
| SHA256 | `96b23fd1435b26da8af28aa390d48d579c899c7399f24ee6bf18fa51039c1cea` | `4311715a95516f662f6acfccd292eeeaa9d56bfceaf5874d203de941be47dc8b` |
| Tamaño | 20420 bytes | 21187 bytes |
| mtime | 2026-11-27 06:53 | 2026-11-28 08:32 |

(El SHA256 "antes" es el mismo verificado sin cambios en todas las
intervenciones horarias desde la Sección 46 del doc confidencial de Drive.)

## Diff exacto aplicado

Función `z180asci_channel_base::asext_w()`, única función modificada:

```diff
 void z180asci_channel_base::asext_w(uint8_t data)
 {
 	if (m_asci_ext & Z180_ASEXT_BRK_EN)
-		m_tx_state = (m_asci_ext & Z180_ASEXT_BRK_SEND) ? STATE_BREAK : STATE_WAIT;
+	{
+		m_tx_state = (m_asci_ext & Z180_ASEXT_BRK_SEND) ? STATE_BREAK : STATE_WAIT;
+
+		// WRITER_RE_08 Hallazgo 7 (autorizado por Samuel, 2026-11-28):
+		// BRK_SEND es un forzado asincrono de la linea en el Z8018x real,
+		// independiente del generador de baud rate / TDR. device_clock_changed()
+		// (abajo) cancela incondicionalmente m_tra_clock, y el unico otro sitio
+		// que mueve TXA para STATE_BREAK es transmit_edge(), que solo se alcanza
+		// desde ese mismo timer (o un reloj externo por CKA, no cableado en este
+		// hardware). Firmware que usa BRK_SEND como senal fuera de banda sin
+		// escribir nunca TDR podia pedir un break que jamas llegaba al pin fisico.
+		// Aplicar/liberar el break sobre el pin de inmediato aqui.
+		if (m_tx_state == STATE_BREAK)
+			output_txa(0);
+		else if (m_asci_stat & Z180_STAT_TDRE)
+			output_txa(1);
+	}
 	device_clock_changed();
 }
```

(Aplicado con `edit_block` de Desktop Commander, autorizado explícitamente
para este archivo en esta intervención; no se intentó rodear el bloqueo del
clasificador de seguridad en ninguna intervención anterior sin esta
autorización.)

## Razonamiento (resumen; detalle completo en Hallazgo 7, doc confidencial
WRITER_RE_08)

El firmware del Writer activa `BRK_EN`+`BRK_SEND` en ASEXT1 (vía la rutina
`0x046b`) para forzar la línea TXA1 a bajo como señal de protocolo fuera de
banda hacia el PIC — sin transmitir nunca un byte de datos real por TDR1.
En el modelo original del core, la única ruta que ejecuta `output_txa(0)`
para `STATE_BREAK` es `transmit_edge()`, alcanzable solo desde el timer de
transmisión `m_tra_clock`, que `device_clock_changed()` cancela
incondicionalmente cada vez que se escribe cualquier registro de
reloj/control ASCI (incluido el propio ASEXT). Como el firmware nunca
escribe TDR1, ese timer nunca se rearma, y `output_txa(0)` nunca se
ejecuta: el pin físico se queda alto para siempre pese a que el firmware
pidió lo contrario. El parche aplica/libera el break de forma inmediata en
`asext_w()`, modelando el comportamiento asíncrono real de BRK_SEND descrito
por Zilog para el Z8018x.

## Verificación (VONTAR, 2026-11-28, post-parche)

1. **Build:** `build_writerdiag.sh` (make -j1, incremental — solo recompiló
   `writer.cpp`, `z180asci.cpp` y relinkeo). `RUN_EXIT=0`, binario generado
   en `/home/spc/projects/WRITER_INSTRUCTOR/diag/writerdiag`.
2. **Prueba dirigida:** arranque normal, SIN ningún bypass de diagnóstico
   (`WRITER_TEST_DISABLE_PIC_RESET` no exportado, `WRITER_TEST_VALID_RAM_SIGNATURE`
   no exportado), 30s emulados reales, con
   `WRITER_TRACE_PIC_PORTB=1 WRITER_TRACE_ASCI1_RX=1 WRITER_TRACE_PIC_RAM=1`.
3. **Resultado — antes del parche (línea base, todas las intervenciones
   anteriores):** el PIC pulsaba `/RESET` cada ~2.016s, indefinidamente
   (14+ ciclos en 30s), 0 escrituras `rs=1` (ningún texto real en el LCD).
4. **Resultado — después del parche (esta intervención):**
   - **1 solo** `/RESET ASSERT` + `/RESET CLEAR` en los 30s completos (el
     pulso de power-on inicial, en t=0.002s-0.583s) — **el PIC nunca vuelve
     a reiniciar el Z180 en el resto de la corrida.**
   - `RB7_TXA1` cae de `1` a `0` en t=0.816040s (visible en la traza
     `WRITER_TRACE_PIC_PORTB`), confirmando que el PIC ahora sí ve el break
     en el pin.
   - **121 escrituras `rs=1`** (texto real en el LCD, antes 0).
   - El texto decodificado de los bytes `rs=1` es legible:
     `"MEMORY ERROR WARNING!!!"` (con espacios de relleno) — un mensaje de
     autodiagnóstico real del firmware, consistente con que el Z180 ya
     ejecuta código de aplicación real más allá del punto de bloqueo (el
     mensaje en sí es esperable en un arranque emulado sin estado previo de
     NVRAM/SRAM con batería; no es un bug nuevo, es la propia lógica del
     firmware funcionando).
   - El log completo (15356 líneas) no muestra ningún crash ni excepción;
     la corrida completa los 30s con heartbeat normal de IRQ1/T0CKI hasta
     el final (`FINAL PC=0E27`, estado de CPU consistente con ejecución
     normal en curso, no con un cuelgue).

**Conclusión: el bloqueo de arranque (boot-stall) documentado en
`BOOT_STALL_PIC_RESET_LOOP_20261127.md` queda RESUELTO** con este parche,
verificado en un arranque normal sin ningún bypass de diagnóstico.

## Pendientes a futuro (no bloqueantes para este cierre)

1. Revisar si el mensaje `"MEMORY ERROR WARNING!!!"` es esperado en este
   punto del arranque emulado (probablemente sí, por falta de NVRAM/SRAM
   con estado previo real) o si merece su propia investigación aparte —
   **no es parte del bloqueo de arranque ya resuelto**, es la siguiente capa
   de comportamiento del firmware ahora visible por primera vez.
2. Riesgo de generalización a otros 14 drivers de MAME que instancian
   `z180_device` (listado completo en la Sección 51 del doc confidencial y
   en el commit `d981649` de este repo) — evaluar antes de proponer este
   parche como cambio de core upstream en MAME, no antes de usarlo en este
   proyecto.
3. El trabajo físico/hardware (pines J1/RB2, continuidad RAM/U21, timing
   del PIC) sigue pausado por indicación de Samuel; este hallazgo no lo
   reactiva por sí solo.
