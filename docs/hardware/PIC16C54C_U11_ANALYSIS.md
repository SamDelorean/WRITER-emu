# The Writer — PIC16C54C U11: firmware e interconexión

**Estado:** análisis estático inicial documentado; trazado eléctrico físico pendiente.
**Fecha:** 2026-11-21
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
