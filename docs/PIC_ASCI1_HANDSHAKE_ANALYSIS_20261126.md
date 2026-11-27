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


## Segunda etapa: cadena previa y comparación de temporización

### Inicialización previa a ASCI1

La llamada de arranque `0x214b` invoca, en este orden:

- `0x0e9b`: inicializa variables de estado/buffers en RAM (`D0AD`, `D0AF`, `D08A`, `D088`, `D089`, `D0B1`, `D0B2`) y llama a `0x0e77`.
- `0x0938`: inicializa un periférico externo mediante dos llamadas a `0x090e` con puertos `0xC0` y `0xC2`; la secuencia escrita es `30, 30, 38, 0C, 06`. Es compatible con inicialización de un controlador LCD tipo HD44780, pero no se declara confirmado sin identificar el circuito conectado.
- `0x0615`: prepara punteros/estado de buffers en RAM (`D071`, `D045`, `D047`) y llama a `0x0610`.
- `0x064e`: vacía dos lecturas de RDR1 y programa CNTLB1/CNTLA1/STAT1 como se documentó arriba.

Por tanto, `0x0938` parece ser inicialización de display y no una configuración ASCI1. La configuración ASCI1 está aislada en `0x064e`.

### Cálculo de baud rate en el modelo MAME

La implementación local `z180asci.cpp` calcula:

- `CNTLB1=0x06`: SS=6, DR=0, CTS/PS=0.
- Prescaler BRG regular: `(1 << 6) * 10 = 640`.
- Divisor adicional de canal: 16 cuando DR=0.
- El canal 1 se instancia en `writer.cpp` con `DERIVED_CLOCK(1,2)`.
- CPU Z8S180: 3.072 MHz (PHI documentado como cristal de 6.144 MHz dividido entre 2).

Con la configuración actual del modelo, el reloj entregado al ASCI es 1.536 MHz y la tasa calculada por la fórmula del núcleo es:

`1,536,000 / (640 * 16) = 150 bit/s`.

### Temporización PIC estimada por conteo de instrucciones

La rutina PIC `0x0027` llama a `0x0006` en cada iteración. En el dump, `0x0006–0x0014` contiene trece NOP, CLRWDT y RETLW. Contando el CALL/retorno, el retardo es del orden de 18 ciclos de instrucción. La ruta principal de cada bit añade aproximadamente 9 ciclos (lectura/modificación de PORTB, RRF, DECFSZ y GOTO), resultando cerca de 27 ciclos por bit.

Con Fosc=32.768 kHz y ciclo de instrucción Fosc/4=8.192 kHz:

`8192 / 27 ≈ 303 bit/s`.

Esta es una estimación estática; hay que confirmar el conteo exacto de ciclos en ambas ramas de BTFSS/DECFSZ y el primer/último intervalo. No obstante, el resultado apunta a aproximadamente 300 baudios.

### Discrepancia candidata

La estimación PIC (~300 bit/s) no coincide con los 150 bit/s que calcula la configuración ASCI1 actual del modelo MAME. Es una discrepancia de factor 2 que merece investigación prioritaria.

No se concluye todavía que el hardware real tenga esa discrepancia ni que MAME esté equivocado. Antes hay que confirmar con documentación primaria del Z180:
- si el reloj base del BRG ASCI1 es PHI o PHI/2;
- cómo debe aplicarse el divisor de sobremuestreo;
- si la conexión física de RXA1 recibe exactamente el reloj y formato esperados.

La configuración `DERIVED_CLOCK(1,2)` puede ser correcta o puede estar introduciendo un factor 2 indebido; no modificarla sin cotejar la hoja de datos y el diseño de placa.

## Siguiente acción técnica

Consultar la documentación primaria del Z180/Z8S180 para validar la ecuación de baud rate y el reloj de entrada del BRG. En paralelo, cerrar el conteo exacto de ciclos de PIC `0x0027` y verificar la ruta de señal RB0→RXA1. La diferencia 300/150 es ahora la pista cuantitativa principal, no una causa confirmada.
