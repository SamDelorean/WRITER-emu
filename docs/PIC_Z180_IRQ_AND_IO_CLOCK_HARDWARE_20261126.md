# The Writer — PIC/Z180: interrupciones, reloj de actividad y matriz de teclado

**Fecha:** 2026-11-26
**Repositorio:** `SamDelorean/WRITER-emu`
**Rama de trabajo:** `fix/irq1-ra2-wakeup`
**Alcance:** consolidación de continuidad física, análisis estático de ROM/PIC y correlación con el decodificador U19. Este informe no modifica el firmware ni añade comportamiento al emulador.

## 1. Resumen de resultados

1. **IRQ1 está conectado físicamente:** U11 PIC16C54C, RA2 pin 1 → Z180 `/INT1`. El PIC afirma la interrupción al llevar RA2 a bajo y la libera al llevarlo a alto.
2. **El vector INT1 es D700:** la ROM instala indirectamente el puntero `0x182A` en `D700-D701`. El ISR `0x182A` procesa contadores y llama a la ruta de debounce/repeat del teclado.
3. **T0CKI ya tiene origen identificado:** U11 RA4/T0CKI pin 3 → U19 pin 12 (`/Y3`) del 74x138. U19 decodifica determinadas operaciones de E/S del Z180.
4. **El PIC usa Timer0 como detector de actividad del bus:** `OPTION=0x20` selecciona reloj externo por T0CKI, flanco ascendente y prescaler 1:2. La ROM llama a un helper que ejecuta `OUT (0x42),A`; el final del ciclo de E/S genera el flanco ascendente en T0CKI. No es un temporizador independiente de periodo fijo.
5. **La interfaz lógica del teclado queda mejor delimitada:** U19 `/Y2` controla el reloj CP de U10 (74374), `/Y4` habilita U8 (74244) y `/Y5` habilita U9 (74244). Los buses de datos y la asignación de cada pin a una fila/columna física aún no están completos.
6. **RB3 del PIC está trazado hasta U9 pin 11 (2A1):** el destino físico del net queda confirmado; qué línea/columna de teclado alimenta ese canal continúa pendiente.

## 2. Convención de evidencia

- **CONFIRMADO FÍSICAMENTE:** continuidad de placa reportada en los levantamientos U11/U19/U16/U17.
- **CONFIRMADO POR FIRMWARE:** instrucción, registro, vector o flujo observado en el desensamblado.
- **CORRELACIÓN:** coincidencia entre comportamiento de PIC y Z180 o entre registros de firmware y hardware.
- **PENDIENTE:** no existe todavía continuidad, observación eléctrica o trazado suficiente para asignar la función.

Una continuidad hacia un pin de un integrado no demuestra por sí sola su función dinámica. Del mismo modo, un acceso a un puerto de E/S no demuestra una conexión a una fila/columna hasta cerrar el cableado externo.

## 3. Interconexiones físicas PIC–Z180

| Origen | Destino | Estado | Función conocida / límite |
|---|---|---|---|
| U11 RA2, pin 1 | Z180 `/INT1` | **Confirmado físicamente** | IRQ1 activa en bajo. |
| U11 RB0, pin 6 | Z180 RXA1 | **Confirmado físicamente** | Transmisión bit-bang PIC → receptor ASCI1. |
| Z180 TXA1 | U11 RB7, pin 13 | **Confirmado físicamente** | El PIC lee esta entrada durante su handshake. |
| Z180 `/HALT` | U11 RB1, pin 7 | **Confirmado físicamente** | El PIC evalúa el estado del Z180; la lógica de emulación distingue HALT y SLP. |
| U11 RA3, pin 2 | CS2 de U16 y U17 | **Confirmado físicamente** | Entrada CS2 activa-alta, compartida por ambas SRAM. No discrimina por sí sola U16/U17. |
| U11 RA4/T0CKI, pin 3 | U19 pin 12, `/Y3` | **Confirmado físicamente** | Salida del decodificador conectada a la entrada externa de Timer0. |
| U11 RB2, pin 8 | Contacto J1 mediante R28 (22 kΩ) | **Ruta documentada; contacto sin numerar aquí** | No asignar número de contacto hasta reconciliar el levantamiento físico de J1. |
| U11 RB3, pin 9 | U9 pin 11, 2A1 | **Confirmado físicamente** | Entrada de uno de los canales del buffer U9; índice físico de columna aún no identificado. |
| U11 RB4, pin 10 | Pad KB1 y trayectoria por D3 hacia U10 Q1 | **Ruta documentada** | No equivale todavía a identificar una tecla concreta. |
| U11 RB5, pin 11 | D14 / trayectoria del supercapacitor | **Ruta documentada** | Función completa pendiente. |
| U11 RB6, pin 12 | Z180 EXTAL | **Conexión documentada** | No tratar como señal GPIO funcional del PIC. |

No se asigna número al contacto J1 conectado a RB2. La numeración física de J1 se mantiene como una cuestión separada y pendiente de cierre.

## 4. Vector e interrupciones del Z180

### 4.1 Instalación de vectores

La ROM establece `I=0xD7`. La rutina auxiliar en `0x040C` instala un puntero utilizando la dirección `(I << 8) + HL`; por ello las instalaciones no aparecen como escrituras literales a `D700` en el desensamblado.

| Dirección vector | Fuente Z180 | Handler instalado | Evidencia |
|---|---|---:|---|
| `D700-D701` | INT1 externa | `0x182A` | Llamada en `0x2186`, HL=0, DE=0x182A. |
| `D702-D703` | INT2 externa | `0x0418` | Llamada en `0x0433`, HL=2, DE=0x0418. |
| `D70E-D70F` | ASCI0 | `0x070C` | Llamada en `0x07AE`, HL=0x0E, DE=0x070C. |

El modo `IM 1` observado en `0x242D` corresponde al vector fijo de INT0 en `0x0038`; no debe usarse para descartar la tabla programable de INT1/INT2. La ROM no contiene escrituras directas al puerto IL (`0x33`) en el análisis realizado; la interpretación de los vectores D7xx considera IL en su estado de reset.

### 4.2 Habilitación y handlers

- `0x218E` establece ITE1 (bit 1 de ITC), habilitando INT1; la ROM habilita interrupciones globales posteriormente con `EI`.
- El handler INT1 `0x182A` actualiza contadores/estado y llama a `0x0C40` desde `0x1879`. La rutina `0x0C40` realiza debounce/repeat, llama al escáner `0x0BE2` y al lector de modificadores `0x0B64`.
- El handler INT2 `0x0418` establece la marca RAM `D075=1`. La rutina `0x042C` prepara una ventana de prueba habilitando INT2 y luego comprueba esa marca. Su origen físico no está identificado.
- El vector fijo INT0 `0x0038` escribe `D073=1`; su prueba/uso aparece en la rutina alrededor de `0x0450`. No confundir INT0 con la línea PIC→INT1.
- ASCI0 tiene su vector en `D70E`; la ROM configura la recepción RIE en STAT0.

**Conclusión:** PIC RA2 es la fuente física conocida de INT1. El código de teclado está ligado al ISR INT1 a través de `0x182A → 0x0C40`; no se trata de una llamada directa del PIC a una rutina de teclado.

## 5. T0CKI: reloj externo derivado de ciclos de E/S del Z180

### 5.1 Configuración PIC

El PIC escribe `OPTION=0x20` en las palabras `0x075–0x076` y vuelve a programarlo en `0x0BC–0x0BD`. Para el PIC16C54C, esta configuración significa:

- T0CS=1: Timer0 toma su reloj de la entrada externa T0CKI.
- T0SE=0: cuenta flancos ascendentes.
- PSA=0: prescaler asignado a Timer0.
- PS2:PS0=000: relación de prescaler 1:2.

T0CKI es RA4, pin 3 del PIC. No es RA2 ni comparte el pin de IRQ1.

### 5.2 Ruta física del pulso

La continuidad de placa establece:

`U11 RA4/T0CKI (pin 3) <-> U19 74x138 (pin 12, /Y3)`

El mapa físico U19 establece las entradas del decodificador:

| Pin U19 | Señal | Conexión Z180 |
|---:|---|---|
| 1 | A | A1 |
| 2 | B | A6 |
| 3 | C | A7 |
| 4 | /G2A | GND |
| 5 | /G2B | /IORQ |
| 6 | G1 | E |
| 12 | /Y3 | U11 RA4/T0CKI, pin 3 |

El 74x138 está habilitado durante un ciclo de E/S cuando `/IORQ=0` y `E=1`; /G2A está fijado a bajo. Para seleccionar /Y3 deben cumplirse A7=0, A6=1 y A1=1. En el puerto `0x42` esos tres bits tienen exactamente esos valores. A0 y A2–A5 no intervienen en esta selección del 74x138; no se ha cerrado aquí el comportamiento de posibles alias de dirección más altos.

Al seleccionar Y3, la salida activa en bajo desciende durante el ciclo. Al terminar el ciclo y deseleccionarse la salida, /Y3 vuelve a alto. Ese flanco ascendente llega a T0CKI.

### 5.3 Generación de pulsos en la ROM

El helper Z180 `0x0400` realiza:

```asm
0400 PUSH BC
0401 LD BC,0x42
0404 OUT (C),A
0406 POP BC
0407 RET
```

Se encontraron once llamadas al helper en la ROM:

`0x04AE, 0x052B, 0x0542, 0x0682, 0x0746, 0x0769, 0x0E0F, 0x1756, 0x1775, 0x19F3, 0x1A55`.

Cada ejecución produce un ciclo OUT al puerto `0x42`, que selecciona U19 /Y3 y genera el flanco de retorno hacia T0CKI. Con prescaler 1:2, cada segundo flanco ascendente válido incrementa TMR0.

### 5.4 Consecuencia para el comportamiento del PIC

En el ciclo PIC `0x0BC–0x0D6`:

1. Programa OPTION, limpia TMR0 y limpia el contador RAM `0x0E`.
2. Pulsa RA2 para afirmar/liberar INT1.
3. Espera y evalúa `(PORTB & 0x82) == 0x80` (RB7 alto y RB1 bajo) para determinar si sale por la rama `0x0D6`.
4. Si no sale, consulta TMR0. Si ya recibió pulsos externos, vuelve al inicio del ciclo y limpia TMR0.
5. Si TMR0 permanece en cero, decrementa RAM `0x0E` y repite la espera; el contador de 8 bits puede recorrer 256 iteraciones antes de salir por `0x0D7`.

Por tanto, los pulsos del Z180 reinician la ventana de espera del PIC. La cadencia de los pulsos RA2/INT1 depende de que el Z180 ejecute las escrituras decodificadas a `0x42`, además de las condiciones de RB1/RB7. **No se debe modelar esta ruta como un timer periódico fijo del PIC ni como una interrupción TMR0 independiente.** El periodo eléctrico real de las señales debe medirse pasivamente en operación normal antes de fijar temporizaciones del emulador.

## 6. Interfaz de la matriz de teclado: U19, U10, U8 y U9

El levantamiento físico U19 y el firmware Z180 concuerdan en estas funciones:

| Puerto Z180 | Bits de dirección relevantes | Salida U19 | Conexión física | Uso lógico |
|---:|---|---|---|---|
| `0x40` | A7=0, A6=1, A1=0 | /Y2, pin 13 | U10 74374 pin 11 (CP) | Actualiza el latch de selección de fila. |
| `0x42` | A7=0, A6=1, A1=1 | /Y3, pin 12 | U11 RA4/T0CKI pin 3 | Pulso de actividad hacia Timer0 del PIC. |
| `0x80` | A7=1, A6=0, A1=0 | /Y4, pin 11 | U8 74244 pines 1 y 19 (/OE) | Habilita los dos bancos de buffers U8 para lectura. |
| `0x82` | A7=1, A6=0, A1=1 | /Y5, pin 10 | U9 74244 pines 1 y 19 (/OE) | Habilita los dos bancos de buffers U9 para lectura. |

### 6.1 Secuencia lógica observada

La rutina de escaneo `0x0BE2`:

1. Escribe `0xFF` al puerto `0x40`.
2. Carga el estrobo de fila inicial `0x80`.
3. Escribe cada estrobo al puerto `0x40`, lee los bancos `0x82` y `0x80`, y rota el estrobo a la derecha para recorrer ocho filas.
4. El resolutor `0x0BD3` determina el índice lógico a partir del estado leído.

La rutina `0x0B64` muestrea cinco intersecciones seleccionadas de la matriz y forma la máscara de modificadores/estado del evento. La rutina `0x0C40` aplica debounce/repeat; el productor `0x0C22` agrega eventos de dos bytes a la FIFO.

### 6.2 Qué está y qué no está cerrado físicamente

**Confirmado:**
- U10 es un latch octal 74374; U19 /Y2 llega a su CP.
- U8 y U9 son buffers 74244; /Y4 y /Y5 llegan a ambos pines /OE de cada dispositivo.
- PIC RB3 llega a U9 pin 11 (2A1).
- PIC RB4 llega a pad KB1 y una trayectoria por D3 hacia U10 Q1.

**Pendiente:**
- Pines D0–D7 y Q0–Q7 de U10, y su continuidad a las ocho líneas físicas de fila.
- Pines de datos de U8/U9 y su continuidad a las columnas físicas.
- Canal exacto de U9 que corresponde a RB3 en el contexto de la matriz.
- Asociación de los índices lógicos R0–R7/C0–C15 de la ROM con posiciones físicas del teclado.
- Polaridad eléctrica real de cada línea, resistencias de polarización y cualquier inversión externa.

La tabla de traducción ROM y las coordenadas lógicas no sustituyen un netlist físico. El emulador puede seguir usando temporalmente una agregación de “cualquier tecla” para RB3, pero debe marcarse como aproximación; no es una reproducción de la columna física exacta.

## 7. Implicaciones para el modelo MAME

1. Mantener PIC RA2 → Z180 IRQ1 activa-baja.
2. Mantener PIC RB0 → ASCI1 RXA1 y Z180 TXA1 → PIC RB7.
3. Representar T0CKI desde la salida U19 /Y3 o desde una abstracción equivalente de ciclos OUT decodificados a `0x42`; no reemplazarlo por un temporizador autónomo de periodo fijo.
4. Mantener explícito que el prescaler 1:2 pertenece al Timer0 PIC.
5. Conservar RB3 como aproximación mientras no se conozca el canal físico/columna exacta; actualizarlo cuando el trazado de U9 esté cerrado.
6. No modelar aún pines U10 Q, U8/U9 data, ni columnas físicas por inferencia de la tabla de traducción.
7. No atribuir al contacto J1 conectado a RB2 un número físico hasta resolver el levantamiento del conector.
8. No alterar ROM, PIC ni cableado real durante estas tareas de análisis.

## 8. Referencias internas de evidencia

- `docs/hardware/PIC16C54C_U11_ANALYSIS.md` — desensamblado PIC, perfiles de puertos y continuidad U11.
- `docs/U19_74138_PIN_MAP_20261125.md` — pinout físico U19, entradas Z180 y salidas U8/U9/U10.
- `docs/RA3_SRAM_CS2_TRACE_20261125.md` — conmutación de RA3 y su correlación con CS2.
- `docs/SRAM_K6X1008C2D_HARDWARE_EVIDENCE_20261125.md` — identificación de U16/U17, pinout SRAM y polaridades CS.
- `docs/KEYBOARD_PHASE1_GHIDRA_20261126.md` — análisis de escáner, modificadores y patrones de matriz.
- `docs/KEYBOARD_MATRIX_AND_TRANSLATION_TABLE_20261126.md` — tabla de 128 entradas y coordenadas lógicas.
- `docs/KEYBOARD_PHASE2_HANDLER_ANALYSIS_20261126.md` — handlers de eventos de teclado.

**Estado final:** la ruta física de INT1 y la fuente física de T0CKI quedan identificadas. El decodificador de puertos y su relación con el latch/buffers de teclado están documentados. El pinout J1, el netlist de datos de la matriz, la columna RB3 y el mapa de selección individual de SRAM siguen abiertos.
