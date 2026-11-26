# The Writer — PIC16C54C U11: firmware e interconexión

**Estado:** análisis estático y continuidad física documentados; integración MAME parcial.
**Fecha inicial:** 2026-11-21. **Actualización:** 2026-11-25.

> Las secciones 5–8 conservan el estado histórico del análisis inicial. La sección 10 incorpora las mediciones físicas posteriores y corrige las conclusiones pendientes; debe prevalecer para el estado actual.
**Fuente primaria:** `willem/ghidra_writer/analysis_20261121/out/pic_disassembly.txt` (listado Ghidra de las 512 palabras).
**Volcado:** `roms/akwriter/writer_instructor_pic16c54c_u11.bin` (1 KiB; 512 palabras de 12 bits en contenedores little-endian de 16 bits).

## 1. Identificación y límites

- Dispositivo identificado: Microchip PIC16C54C-04I/P, encapsulado PDIP-18.
- Vector de reset: palabra `0x1FF`, salto a `0x074`.
- Código ejecutable observado: palabras `0x000–0x0E2`; resto mayoritariamente relleno `0xFFF`.
- Las direcciones de este documento son **direcciones de palabra PIC**, no direcciones de byte.
- El análisis describe el firmware del volcado. No demuestra por sí solo continuidad de pistas ni la función de los circuitos externos.

## 2. Secuencia principal

La inicialización en `0x074–0x081` hace lo siguiente:

1. Escribe `OPTION=0x20`.
2. Limpia `TMR0` y las variables RAM `0x08–0x0C`.
3. Configura puertos mediante la rutina `0x03E`.
4. Llama a las rutinas de actualización/espera y vuelve al inicio del ciclo mientras el bit 7 de RAM `0x0C` no esté activo.

La máquina de estados principal ocupa `0x082–0x0E2`:

- `0x082–0x09F`: consulta RB2 y RB3 y ejecuta distintas ramas de servicio. Las condiciones de RB2/RB3 controlan qué rutinas se llaman y cuándo se repite la evaluación.
- `0x0A0–0x0AE`: configura estados de puertos, ejecuta rutinas auxiliares y prepara la fase de espera de RB7.
- `0x0AF–0x0B3`: consulta RB7. La instrucción `BTFSS PORTB,7` seguida de `GOTO 0x0B4` hace que la transmisión comience cuando RB7 está bajo. Si RB7 permanece alto, decrementa un contador inicializado en `0x80`; al agotarlo salta a `0x0D7`.
- `0x0B4–0x0BB`: toma sucesivamente RAM `0x08`, `0x09`, `0x0A` y `0x0B`, y llama a `0x027` para transmitir cada byte por RB0.
- `0x0BC–0x0D6`: reinicia la base de tiempo, pulsa RA2, espera y comprueba una combinación de entradas.
- `0x0D7–0x0E2`: ejecuta rutinas auxiliares y vuelve a evaluar RB3 para decidir si repite la rama o regresa a `0x082`.

Los nombres funcionales de las rutinas auxiliares (`0x000`, `0x00A`, `0x010`, `0x011`, `0x015`, `0x025`, `0x059`, `0x062`, `0x06B`) siguen pendientes de reconstrucción completa. No asignarles una función de hardware sin seguir sus efectos.

## 3. Contador y comunicación serial

### Contador

La rutina en `0x015–0x021` suma 4 a la variable `0x0C`; cuando se produce acarreo, incrementa sucesivamente `0x08`, `0x09`, `0x0A` y `0x0B`. Por tanto, `0x0C` funciona como acumulador/divisor de fase de 8 bits y `0x08–0x0B` forman el contador principal de 32 bits que se transmite. No es correcto describir `0x08–0x0C` como un contador binario simple de 40 bits.

### Transmisor RB0

La rutina `0x027–0x03D` transmite por RB0 mediante manipulación de bits de PORTB. El contador de iteraciones se carga con 9 y el dato se desplaza mediante `RRF`; al terminar se fuerza RB0 alto y se ejecutan esperas.

El flujo principal transmite cuatro bytes en orden `0x08`, `0x09`, `0x0A`, `0x0B`.

**Interconexión lógica de alta confianza:** PIC RB0 (pin 6) → Z180 RXA1 (ASCI1). La ROM Z180 configura ASCI1 y lee RDR1; en el arranque recibe cuatro bytes. Esta correlación de ambos firmwares es fuerte, pero la continuidad eléctrica y los parámetros exactos de línea deben medirse en la placa.

No fijar todavía baud rate, polaridad eléctrica ni formato UART completo como parámetros definitivos del emulador: la rutina bit-bang debe contrastarse con la frecuencia del oscilador real y una captura lógica.

## 4. Timer0 y señales de control

El firmware escribe `OPTION=0x20` tanto en la inicialización (`0x075–0x076`) como después de transmitir (`0x0BC–0x0BD`). Para el PIC16C54C esto selecciona reloj externo de Timer0 por T0CKI, flanco ascendente, prescaler asignado a Timer0 y relación 1:2.

En `0x0BE–0x0D5` limpia TMR0 y RAM `0x0E`, ejecuta el pulso de RA2 y comprueba PORTB con la máscara `0x82`. La expresión observada es:

```c
(PORTB & 0x82) == 0x80
```

Es decir, RB7 debe estar alto y RB1 bajo para tomar la rama de salida `0x0D6`. Si no se cumple, el programa consulta TMR0 y utiliza RAM `0x0E` como contador adicional de espera antes de repetir la secuencia. La fuente externa de T0CKI no está identificada.

**Corrección de pinout:** RA2 = pin 1; T0CKI = pin 3. Son pines diferentes. No usar la etiqueta combinada `T0CKI/RA2`.

## 5. Pines observados

Pinout para PIC16C54C PDIP-18:

| Pin | Señal | Evidencia de firmware | Interpretación provisional |
|---:|---|---|---|
| 1 | RA2 | Se limpia y activa en `0x0C1–0x0C2` | Pulso de salida; destino externo sin identificar |
| 2 | RA3 | PORTA se carga con `0x0C` en ciertas configuraciones | Función/destino por confirmar con pinout y placa; no inferir función solo por el valor de PORTA |
| 3 | T0CKI | OPTION selecciona reloj externo de Timer0 | Entrada de pulsos; origen sin identificar |
| 6 | RB0 | Transmisor bit-bang en `0x027` | Salida serial; correlación lógica con Z180 RXA1 |
| 7 | RB1 | Participa en prueba `(PORTB & 0x82)==0x80` | Entrada consultada en la fase posterior al pulso |
| 8 | RB2 | Se lee en la máquina de estados y cambia de dirección | Señal de estado/handshake probable |
| 9 | RB3 | Se lee en las transiciones de `0x082–0x0E2` | Señal de estado/handshake probable |
| 10 | RB4 | Dirección cambia según TRISB | Función externa pendiente |
| 11 | RB5 | Salida en las máscaras TRIS observadas | Función externa pendiente |
| 12 | RB6 | Dirección cambia según TRISB | Función externa pendiente |
| 13 | RB7 | Se consulta antes de transmitir: bajo permite transmitir; alto mantiene la espera. También participa en la prueba posterior | Handshake/estado; polaridad funcional observada en el firmware |

### Máscaras TRISB observadas

En TRIS, 1 = entrada y 0 = salida:

| TRISB | Salidas | Entradas |
|---|---|---|
| `0x4E` | RB0, RB4, RB5, RB7 | RB1, RB2, RB3, RB6 |
| `0xDE` | RB0, RB5 | RB1, RB2, RB3, RB4, RB6, RB7 |
| `0xDA` | RB0, RB2, RB5 | RB1, RB3, RB4, RB6, RB7 |

Las escrituras de PORTA/PORTB y los cambios de TRIS deben interpretarse conjuntamente; no basta con una única máscara para asignar una función fija a cada pin.

## 6. Relación con el Z180

La ROM principal presenta configuración de ASCI1 y lectura de su registro receptor. En el arranque solicita cuatro bytes y los almacena en RAM. El PIC transmite cuatro bytes consecutivos desde `0x08–0x0B` por RB0.

- PIC RB0: pin 6 del PDIP-18.
- Z180 RXA1: pin 53 en el encapsulado Z8S180 de 68 pines PLCC considerado para la placa.
- Estado: **enlace lógico firmware-a-firmware, pendiente de continuidad física**.

## 7. Pendientes para cerrar el PIC

1. Reconstruir las subrutinas auxiliares y documentar sus efectos concretos sobre PORTA, PORTB, TRIS, RAM y Timer0.
2. Calcular el baud rate real de RB0 con el oscilador de la placa y verificarlo con captura lógica.
3. Identificar qué representa eléctricamente RB7; el firmware establece que nivel bajo habilita la transmisión y nivel alto mantiene la espera.
4. Identificar eléctricamente el origen de T0CKI (pin 3).
5. Seguir RA2 (pin 1) y RA3 (pin 2) hasta su destino.
6. Seguir RB1, RB2, RB3, RB5 y RB7 hasta sus componentes.
7. Verificar continuidad RB0 → Z180 RXA1 y niveles eléctricos.
8. No atribuir a RA2 la exploración de teclado: el escaneo 8×16 encontrado hasta ahora está implementado por accesos del Z180 a los puertos `0x40`, `0x80` y `0x82`.

## 8. Reglas para el trabajo posterior

- Mantener separados los hechos del desensamblado, las inferencias funcionales y las mediciones físicas.
- No modificar el modelo PIC ni el cableado en `writer.cpp` hasta tener evidencia suficiente.
- Registrar nuevos hallazgos con dirección de palabra PIC, instrucción, condición y efecto observado.
- Conservar el volcado y los listados Ghidra como evidencia reproducible.


## 9. Reconstrucción de perfiles de puertos

Las rutinas cortas de configuración permiten reconstruir los estados eléctricos que el PIC programa en sus pines. Los valores siguientes son los latches de PORTA/PORTB y las direcciones TRIS inmediatamente después de cada rutina; no equivalen todavía a continuidad hacia un componente concreto.

### Perfiles identificados

| Rutina | PORTB | TRISB | PORTA | TRISA | Efecto de dirección |
|---|---:|---:|---:|---:|---|
| `0x03E` | `0x30` | `0x4E` | `0x00` | `0x01` | RB0/RB4/RB5/RB7 salidas; RA0 entrada |
| `0x047` | `0x00` | `0xDE` | `0x00` | `0x00` | RB0/RB5 salidas; RA0–RA3 salidas |
| `0x050` | `0x01` | `0xDA` | `0x0C` | `0x00` | RB0/RB2/RB5 salidas; RA0–RA3 salidas |
| `0x059` | `0x01` | `0xDA` | `0x0C` | `0x02` | Igual que anterior, salvo RA1 como entrada |
| `0x062` | `0x01` | `0xDA` | `0x0C` | `0x00` | Igual que `0x050` |
| `0x06B` | `0x00` | `0xDE` | `0x00` | `0x00` | Igual que `0x047` |

### Consecuencias por señal

- **RA2 (pin 1):** en los perfiles `0x050`, `0x059` y `0x062`, el latch de PORTA vale `0x0C`; por tanto RA2 y RA3 se dejan inicialmente altos. La secuencia `0x0C1–0x0C2` baja y vuelve a subir RA2, mientras RA3 permanece alto. Es un pulso sobre RA2 con RA3 alto, no una señal compartida con T0CKI.
- **RA3 (pin 2):** es una línea real del PIC16C54C. El bit 3 de PORTA se establece con el valor `0x0C`; en los perfiles indicados queda como salida alta. Su destino físico aún no está identificado.
- **T0CKI (pin 3):** es independiente de PORTA y RA2. OPTION=`0x20` selecciona reloj externo para Timer0; falta localizar la fuente que alimenta este pin.
- **RB0 (pin 6):** permanece como salida en las tres máscaras TRISB. El latch se inicializa bajo en `0x03E`, `0x047` y `0x06B`; en `0x050`, `0x059` y `0x062` se inicializa alto. La rutina `0x027` modifica RB0 bit a bit para la transmisión.
- **RB2 (pin 8):** cambia de entrada en TRISB=`0x4E`/`0xDE` a salida en TRISB=`0xDA`. Esto confirma uso bidireccional por fases; no asignar todavía nombres como DATA, CLK o ACK.
- **RB4 (pin 10), RB6 (pin 12) y RB7 (pin 13):** son salidas únicamente en el perfil TRISB=`0x4E` para RB4/RB7; RB6 permanece entrada. En los perfiles `0xDE` y `0xDA`, RB4 y RB7 son entradas.
- **RB5 (pin 11):** es salida en todos los perfiles observados. Su latch toma el valor del bit 5 de PORTB: alto en el perfil `0x03E` (`PORTB=0x30`) y bajo en los demás perfiles (`0x00` o `0x01`).
- **RB1 (pin 7) y RB3 (pin 9):** permanecen como entradas en todas las máscaras TRISB observadas y son consultadas por la máquina de estados.

### Rutinas de espera y contador

Las direcciones `0x000–0x014` son secuencias de retardo basadas en llamadas anidadas, NOP y CLRWDT; no contienen escrituras a puertos. Las entradas `0x00A`, `0x010` y `0x011` permiten entrar a distintos puntos de la misma secuencia, variando el tiempo de espera.

La rutina `0x015–0x021` actualiza el acumulador/divisor de fase y el contador de 32 bits descrito en la sección 3. Las rutinas `0x022–0x026` combinan esa actualización con retardos o bucles de espera. Por tanto, su efecto relevante para el cableado es temporal; no constituyen por sí mismas nuevas señales de salida.

### Límite de la reconstrucción

Con el firmware se conoce qué pines se configuran como entradas/salidas, qué niveles se escriben en sus latches y en qué fases se leen. No se puede deducir el componente receptor, la presencia de resistencias pull-up/pull-down, buffers o inversión lógica sin esquema, fotografía legible de ambas caras o mediciones de continuidad. La asignación física de RB1/RB2/RB3/RB4/RB5/RB6/RB7 y de RA2/RA3/T0CKI queda pendiente de trazado en la placa.


## 10. Continuidad física y corrección de la ruta de arranque (2026-11-25)

La documentación de continuidad `WRITER_RE_06_PIC_mapa_conexiones_U11` confirmó el siguiente pinout funcional. Estas conexiones son evidencia física; los niveles marcados como pendientes no deben confundirse con niveles lógicos medidos.

| PIC16C54C | Conexión física confirmada | Función / estado de emulación |
|---:|---|---|
| RA0, pin 17 | Control de energía; destino exacto pendiente | No modelado |
| RA1, pin 18 | Z180 `/RESET` | No modelado; requiere combinar latch PORTA y TRISA |
| RA2, pin 1 | Z180 `/INT1` | Implementado; activo bajo |
| RA3, pin 2 | `CS2` de SRAM U16/U17 | No modelado; ligado al futuro mapa SRAM |
| RA4/T0CKI, pin 3 | U19 pin 12, salida `/Y3` del 74HC138 | Continuidad confirmada; decodificación pendiente |
| RB0, pin 6 | Z180 `RXA1` | Implementado como PIC TX bit-bang → ASCI1 RX |
| RB1, pin 7 | Z180 `/HALT` | Implementado; contempla HALT y SLP |
| RB2, pin 8 | J1 pin 3, mediante R28 de 22 kΩ | Alto en reposo provisional; medir nivel real |
| RB3, pin 9 | U9 pin 11 (`2A1`, 74VHC244) | Agregación “cualquier tecla” provisional; falta columna exacta |
| RB4, pin 10 | Pad KB1 y diodo D3 hacia U10 Q1 | No modelado |
| RB5, pin 11 | D14 / trayectoria de supercapacitor | No modelado |
| RB6, pin 12 | Z180 `EXTAL` | No modelado como señal del PIC |
| RB7, pin 13 | Z180 `TXA1` | Implementado como entrada del PIC |

RA2 y T0CKI son pines distintos. La continuidad RA2→`/INT1` no convierte el pulso en un reloj de exploración de teclado.

### 10.1 Vector IRQ1: corrección de WRITER_RE_08

La conclusión de `WRITER_RE_08_IRQ1_vector_Ghidra` de que D700 no recibía un vector confirmado era incorrecta: la búsqueda de escrituras literales no detectó una instalación indirecta.

La ROM ejecuta:

```text
0x214B: LD HL,0
       LD DE,0x182A
       CALL 0x040C
```

La rutina `0x040C` calcula la dirección usando el registro `I`:

```text
LD A,I
LD D,A
LD E,0
ADD HL,DE
LD (HL),E
INC HL
LD (HL),D
```

El arranque había establecido `I=0xD7`; por tanto, se escribe `0x182A` en `D700–D701`. Los volcados vivos de RAM confirman los bytes `2A 18`. El ISR en `0x182A` actualiza D015, D017, D00F, D011 y D013, y termina con `EI/RET`.

El Z180 llega a `SLP` en `0x0E27` mientras espera actividad de interrupción. Ese PC por sí solo no demuestra un bloqueo: el PIC debe controlar `/INT1` y el estado `/HALT` correctamente.

### 10.2 Integración MAME y pruebas

El driver enlaza ahora:

- PIC RA2 → `Z180_INPUT_LINE_IRQ1`.
- PIC RB0 → `m_maincpu->rxa1_w()`.
- Z180 TXA1 → entrada RB7 del PIC.
- RB1 del PIC ← estado interno `m_HALT` del Z180. MAME codifica SLP como valor 2, pero su entrada pública `Z180_HALT` está enmascarada a bit 0; el driver consulta el byte de estado registrado para detectar HALT y SLP.
- RB2 usa alto como nivel de reposo provisional. El alternativo se prueba con `WRITER_PIC_RB2=0`.
- RB3 se aproxima con la agregación de teclas activas de la matriz; no equivale todavía a la conexión exacta U9/2A1.

Resultados en Vontar, con el PIC real y el D017 sintético deshabilitado:

| Prueba | Resultado |
|---|---|
| RB2=0, RB3=0 | RA2 permanece bajo; el Z180 repite IRQ1 y termina en PC `0x1869` (tormenta de interrupciones) |
| RB2=1, sin tecla | El PIC libera RA2, transmite por RB0, genera pulsos IRQ1 y el Z180 llega a la advertencia de memoria; PC final `0x0E27` |
| RB2=1, D017 sintético deshabilitado | La advertencia de memoria sigue apareciendo; el temporizador artificial no es necesario para llegar a ese estado |

Último estado observado sin D017 sintético: `PC=0E27`, `D011=0019`, `D013=0000`. El log muestra transiciones RB0, cambios RA2 y escrituras LCD. Esto confirma que la ruta de interrupción y el enlace de transmisión están activos, aunque la validación de cada byte y del baud rate todavía debe completarse.

### 10.3 Interpretación del estado de arranque

El emulador ya no se detiene por la interconexión PIC–Z180. Llega a `MEMORY ERROR WARNING` porque la firma fría de SRAM no coincide con la firma válida esperada. La inyección artificial de firma `ANALYSIS_FORCE_VALID_RAM_SIGNATURE` está deshabilitada. No se ejecutó un master reset ni se borró intencionalmente ningún estado persistente.

El volcado de 64 KiB guardado por `WRITER_MEM_DUMP_FILE` representa la ventana lógica `0000–FFFF` / física `C0000–CFFFF`; no es una imagen completa de los 256 KiB de SRAM.

### 10.4 Pendientes prioritarios

1. Medir el nivel de reposo real de RB2 en J1 pin 3 y confirmar si alto es el estado físico correcto.
2. Identificar qué columna exacta llega a U9/2A1 (RB3); sustituir la agregación “cualquier tecla” por esa señal.
3. Modelar RA1→`/RESET` usando conjuntamente PORTA y TRISA; cuando RA1 es entrada, considerar la resistencia de pull-up R19.
4. Modelar RA3→SRAM CS2 y reconstruir el mapa físico de las dos SRAM de 128 KiB, separándolo de la EPROM de 1 MiB.
5. Seguir las entradas del 74HC138 U19 y conectar T0CKI a los pulsos reales de `/Y3`.
6. Determinar el rol de RB4/RB5/RB6 y el control de energía RA0.
7. Validar baud rate, polaridad y los cuatro bytes RB0→RXA1 con captura temporal o trazado de RDRF/STAT1.
8. Implementar SRAM persistente y validar el procedimiento de master reset antes de declarar un arranque normal.

Las conclusiones de continuidad física anteriores prevalecen sobre las inferencias del análisis estático inicial. Mantener explícita la distinción entre conexión confirmada, nivel lógico medido y aproximación del emulador.


## 11. Addendum 2026-11-26 — fuente de T0CKI y relación con IRQ1

Esta sección actualiza las conclusiones históricas de las secciones 4, 7 y 10.2. La fuente externa de T0CKI que entonces estaba pendiente ya fue identificada:

- U11 RA4/T0CKI (pin 3) tiene continuidad con U19 74x138 pin 12 (/Y3).
- U19 decodifica A1/A6/A7 del Z180 y se habilita con /IORQ=0, E=1; /G2A está fijado a GND.
- La ROM utiliza el helper 0x0400, cuyo OUT (C),A escribe al puerto 0x42. La selección Y3 hace que /Y3 baje durante el ciclo de E/S y vuelva a alto al terminarlo.
- El PIC tiene OPTION=0x20: Timer0 usa T0CKI externo, flanco ascendente y prescaler 1:2. Los pulsos de bus reinician la ventana de espera del PIC. No es un temporizador de periodo fijo independiente.

El mismo levantamiento físico confirma que PIC RB3 pin 9 llega a U9 pin 11 (2A1). Esto corrige el límite descrito anteriormente como “RB3 aproximado sin destino exacto”: el destino en el encapsulado está confirmado, pero no se conoce aún qué columna física de la matriz corresponde a ese canal. El emulador puede conservar una agregación provisional de actividad de teclado; no presentarla como netlist físico completo.

La ROM instala indirectamente el vector INT1 D700-D701 = 0x182A mediante la rutina 0x040C con I=0xD7. El PIC RA2 pin 1 está conectado físicamente a Z180 /INT1; el handler 0x182A llama a 0x0C40, que contiene el debounce/repeat y la llamada al escáner 0x0BE2.

El informe complementario ../PIC_Z180_IRQ_AND_IO_CLOCK_HARDWARE_20261126.md contiene la tabla de vectores, el helper OUT, el mapa funcional de puertos y los límites de trazado de U8/U9/U10. La numeración física de J1 permanece pendiente: no asignar número de contacto a RB2 desde esta actualización.
