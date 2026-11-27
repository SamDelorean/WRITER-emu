# PIC–Z180 ASCI1 handshake: análisis estático (2026-11-26)

## Alcance

Análisis del firmware PIC16C54C y del arranque Z180 en The Writer. No se modificó código ni firmware y no se ejecutó una nueva prueba del emulador.

Fuentes inspeccionadas:
- `willem/ghidra_writer/analysis_20261121/out/pic_disassembly.txt`
- `willem/ghidra_writer/analysis_20261121/out/main_disassembly.txt`
- `mame-as2k-stable-arm64/src/devices/cpu/z180/z180asci.cpp`
- `writer-emu/src/mame/akt/writer.cpp`

## Corrección importante a la conclusión anterior

La afirmación de que la ROM no escribe CNTLA1 ni habilita el receptor ASCI1 era incorrecta. La búsqueda anterior fue incompleta.

El desensamblado contiene una inicialización explícita en RAM:

```asm
ram:064e  IN0 A,(0x9)
ram:0651  IN0 A,(0x9)
ram:0654  LD A,0x6
ram:0656  OUT0 (0x3),A
ram:0659  LD A,0x64
ram:065b  OUT0 (0x1),A
ram:065e  LD A,0x0
ram:0660  OUT0 (0x5),A
ram:0663  RET
```

Y su cadena de llamada está confirmada:

```asm
ram:214b  PUSH HL
ram:214c  PUSH DE
ram:214d  CALL 0x0e9b
ram:2150  CALL 0x0938
ram:2153  CALL 0x0615
ram:2156  CALL 0x064e
...
ram:243c  CALL 0x214b
ram:243f  CALL 0x046b
ram:2442  CALL 0x062f ; espera/lee byte 1
ram:2446  CALL 0x062f ; byte 2
ram:244e  CALL 0x062f ; byte 3
ram:2452  CALL 0x062f ; byte 4
```

Por tanto, el receptor ASCI1 se configura antes de la secuencia de cuatro lecturas.

## Interpretación de registros

Según el mapa de registros Z180 y el núcleo MAME inspeccionado:

- Puerto interno `0x03`: CNTLB1.
- Puerto interno `0x01`: CNTLA1.
- Puerto interno `0x05`: STAT1.
- Puerto interno `0x09`: RDR1.

La ROM escribe:
- `CNTLB1 = 0x06`
- `CNTLA1 = 0x64`
- `STAT1 = 0x00`

En CNTLA1, `0x64` establece RE=1 y TE=1; el campo MODE=4 selecciona 8 bits de datos, sin paridad y un bit de parada (8N1). Así queda habilitada la recepción ASCI1 antes de esperar RDRF.

La rutina `0x062F` consulta STAT1 bit 7 (RDRF) y, cuando está activo, lee RDR1. Es consistente con la recepción de los cuatro bytes enviados por el PIC.

## PIC: rutina de transmisión

El PIC ejecuta cuatro llamadas a `0x0027` con los contenidos de RAM `0x08–0x0B`. Antes, la secuencia de inicialización en `0x0074–0x007B` limpia `0x08–0x0B` y el contador `0x0C`. La rutina `0x0015` incrementa el contador y propaga el acarreo a los bytes.

La rutina `0x0027`:
- copia W a `0x0F`;
- carga `0x10` con 9;
- consulta PORTB y manipula RB0;
- llama a una rutina de temporización en cada iteración;
- desplaza `0x0F` mediante RRF;
- termina llevando RB0 a nivel alto.

La estructura es compatible con transmisión serie asíncrona por RB0, con bit de inicio bajo y bits desplazados LSB-first. El formato 8N1 y la velocidad exacta deben confirmarse mediante reconstrucción ciclo-a-ciclo de las instrucciones y temporización de la rutina llamada en `0x0030`; no fijar todavía una cifra de baud rate. La estimación previa de ~300 baudios queda como no verificada.

**No se debe dar por confirmado todavía que los cuatro bytes valen 00 00 00 00.** La inicialización pone los cuatro bytes a cero, pero la rutina `0x0015` modifica ese conjunto propagando el contador. Hay que reconstruir los valores inmediatamente antes de cada llamada a `0x0027` para afirmar el contenido transmitido.

## Estado del modelo de hardware

En `writer.cpp` el comentario de configuración PIC documenta RB0 → Z180 RXA1 y el callback de PORTB está configurado para leer entradas del Z180 en RB1/RB7. La ruta PIC RB0 hacia el receptor ASCI1 debe comprobarse en el callback de escritura PORTB y en el enlace real al pin RXA1; la mera declaración del comentario no sustituye esa verificación.

## Conclusiones

1. La ROM sí inicializa ASCI1 antes de la espera de los cuatro bytes.
2. CNTLA1=0x64 habilita RE y TE y selecciona formato 8N1.
3. La hipótesis anterior de “ASCI1 permanece deshabilitado por falta de inicialización” queda descartada para esta secuencia de arranque.
4. El PIC tiene una rutina de salida serie por RB0, pero faltan la reconstrucción exacta de tiempos y los valores efectivos de los cuatro bytes.
5. La causa del bloqueo sigue abierta. No se debe atribuir todavía a un fallo de receptor ni a una UART mal configurada.

## Siguiente etapa de análisis

1. Analizar `0x0e9b`, `0x0938` y `0x0615`, llamados inmediatamente antes de configurar ASCI1, para identificar configuración de reloj, multiplexado de pines y condiciones previas.
2. Interpretar CNTLB1=0x06 con el reloj de entrada real de ASCI1 y los registros de divisor de baud rate del Z180.
3. Reconstruir el timing PIC de `0x0027` y la subrutina llamada en `0x0030`; determinar baud rate y duración de start/data/stop.
4. Seguir las escrituras a `0x08–0x0B` desde `0x0074` hasta las cuatro llamadas de transmisión para obtener los cuatro bytes reales.
5. Verificar en el modelo el trayecto RB0 → RXA1 y comparar la forma de onda lógica con el timing esperado, sin cambiar la lógica del emulador.
