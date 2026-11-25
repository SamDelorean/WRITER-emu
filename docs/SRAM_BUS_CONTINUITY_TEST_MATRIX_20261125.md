# The Writer — matriz dirigida de continuidad SRAM U16/U17

Fecha: 2026-11-25
Objetivo: cerrar /CS1, /OE, /WE y los buses de direcciones/datos de las dos SRAM Samsung K6X1008C2D-GB55 sin repetir la identificación ni la continuidad ya confirmada RA3→CS2.

## 1. Estado de evidencia

### Confirmado
- U16 y U17: Samsung K6X1008C2D-GB55, SRAM 128K×8, encapsulado 32-SOP, 55 ns.
- U11 PIC16C54C RA3 pin 2 → CS2 pin 30 de U16 y U17: continuidad física medida.
- Datasheet de SRAM: CS2 pin 30 activa-alta; /CS1 pin 22 activa-baja; /OE pin 24 activa-baja; /WE pin 29 activa-baja.
- U21 está identificado en la placa y en la referencia como 74HC139. La fotografía de conjunto muestra su ubicación junto a U19/U20.
- El mapa de memoria del emulador todavía es una sola región plana de 1 MiB; no separar ni seleccionar SRAM en el código hasta cerrar la decodificación física.

### Todavía no medido
- /CS1 pin 22 de U16 y U17 hacia U21 u otra lógica.
- /OE pin 24 hacia /RD del Z180.
- /WE pin 29 hacia /WR del Z180.
- Direcciones A0–A16 y datos I/O1–I/O8 hacia el Z180.
- Entradas y habilitaciones de U21.
- Contenido real de cada SRAM y selección efectiva durante el escaneo ROM 0x2206.

**Importante:** la hoja U_74HC139 de WRITER_hardware_referencia.xlsx clasifica todas sus conexiones propuestas como HIPOTESIS. No tomar la ruta U21→/CS1 como confirmada.

## 2. Referencias de pinout

### U16/U17 — Samsung K6X1008C2D-GB55 (32-SOP)

Fuente primaria: 01_Hardware/K6X1008C2D_SAM.pdf, carpeta Drive del proyecto.

| Función | Pin SRAM | Polaridad / observación |
|---|---:|---|
| A0–A16 | 12, 11, 10, 9, 8, 7, 6, 5, 27, 26, 23, 25, 4, 28, 3, 31, 2 | 17 entradas de dirección |
| I/O1–I/O8 | 13, 14, 15, 17, 18, 19, 20, 21 | Bus bidireccional de 8 bits |
| /CS1 | 22 | Activa en bajo; selección individual candidata |
| /OE | 24 | Activa en bajo |
| /WE | 29 | Activa en bajo |
| CS2 | 30 | Activa en alto; RA3 de U11, ya confirmado |
| VSS | 16 | GND |
| VCC | 32 | Alimentación/respaldo; C38 asociado |

### U21 — SN74HC139N (16 pines)

Fuente: Texas Instruments, SNx4HC139, SCLS108E, Rev. E.

| Pin U21 | Señal |
|---:|---|
| 1 | /1G (habilitación activa-baja, mitad 1) |
| 2 | 1A |
| 3 | 1B |
| 4 | 1Y0 (salida activa-baja) |
| 5 | 1Y1 (salida activa-baja) |
| 6 | 1Y2 (salida activa-baja) |
| 7 | 1Y3 (salida activa-baja) |
| 8 | GND |
| 9 | 2Y3 (salida activa-baja) |
| 10 | 2Y2 (salida activa-baja) |
| 11 | 2Y1 (salida activa-baja) |
| 12 | 2Y0 (salida activa-baja) |
| 13 | 2B |
| 14 | 2A |
| 15 | /2G (habilitación activa-baja, mitad 2) |
| 16 | VCC |

Orientación: determinar pin 1 en la placa por la muesca/marca del encapsulado antes de asignar números. No numerar el integrado solo por la orientación de la leyenda impresa.

La tabla de verdad TI indica que, con G=0, únicamente la salida seleccionada pasa a bajo; con G=1 todas las salidas quedan altas. El orden de selección es Y0 con B=0,A=0; Y1 con B=0,A=1; Y2 con B=1,A=0; Y3 con B=1,A=1.

### U1 — Zilog Z8S180, encapsulado PLCC-68

Fuente: Zilog, DS006002-ZMP0200, Figura 3 y Tabla 1. Las señales con barra son activas en bajo.

| Señal Z180 | Pin PLCC |
|---|---:|
| A0 | 14 |
| A1 | 15 |
| A2 | 16 |
| A3 | 17 |
| A4 | 19 |
| A5 | 20 |
| A6 | 21 |
| A7 | 22 |
| A8 | 23 |
| A9 | 24 |
| A10 | 25 |
| A11 | 26 |
| A12 | 27 |
| A13 | 28 |
| A14 | 29 |
| A15 | 30 |
| A16 | 31 |
| A17 | 32 |
| A18/TOUT | 33 |
| A19 | 35 |
| D0–D7 | 37–44 |
| /MREQ | 63 |
| /WR | 66 |
| /RD | 67 |

## 3. Advertencia de preservación de SRAM

C38 está documentado como supercapacitor asociado al respaldo de SRAM. Una prueba de continuidad requiere placa sin alimentación y sin tensión residual, pero llevar VCC de SRAM por debajo de su tensión de retención puede borrar el contenido existente.

**Antes de desconectar alimentación y esperar la descarga:**
1. Decidir si el contenido actual de SRAM física debe preservarse.
2. Si debe preservarse, obtener primero un volcado válido de U16/U17 o un método de lectura respaldada que no interrumpa VCC.
3. No cortocircuitar C38 ni descargarlo directamente con las puntas del multímetro.
4. No usar continuidad/ohmios mientras haya tensión en la placa. Verificar VCC SRAM respecto a GND en modo voltímetro antes de pasar a continuidad.

Si no se dispone de volcado y el contenido podría ser único, detenerse antes de apagar/descargar la SRAM; primero resolver el método de lectura.

## 4. Orden de pruebas

Usar modo continuidad/resistencia únicamente con la placa ya desenergizada y VCC SRAM a 0 V. Compensar la resistencia de las puntas. Registrar el valor medido, no solamente el pitido. Como guía, una ruta directa de cobre normalmente se acerca a la resistencia de las puntas; una lectura mayor o inestable requiere investigar componentes intermedios y no se marca como conexión directa.

### Prueba A — identificar las líneas /CS1 individuales

Comprobar U16 pin 22 y U17 pin 22 contra las ocho salidas activas-bajas de U21:

| Destino SRAM | U21 pin 4 (1Y0) | 5 (1Y1) | 6 (1Y2) | 7 (1Y3) | 12 (2Y0) | 11 (2Y1) | 10 (2Y2) | 9 (2Y3) |
|---|---|---|---|---|---|---|---|---|
| U16 pin 22 /CS1 | medir | medir | medir | medir | medir | medir | medir | medir |
| U17 pin 22 /CS1 | medir | medir | medir | medir | medir | medir | medir | medir |

Registrar la menor resistencia estable de cada par. La expectativa de trabajo es que cada /CS1 llegue a una salida distinta, pero esto **no está supuesto como resultado**. Si ambos llegan a la misma salida, o ninguno llega a U21, no cerrar el mapa: comprobar si hay una puerta/inversor intermedio y seguir ese net.

### Prueba B — habilitaciones y entradas de U21

Comprobar continuidad desde:
- U21 pin 1 (/1G) hacia U1 /MREQ pin 63 y, si no es directo, hacia lógica intermedia.
- U21 pin 15 (/2G) hacia U1 /MREQ pin 63 y, si no es directo, hacia lógica intermedia.
- U21 pin 2 (1A), pin 3 (1B), pin 14 (2A), pin 13 (2B) hacia U1 A17 pin 32, A18/TOUT pin 33, A19 pin 35 u otras señales que aparezcan en continuidad.

No asignar A17/A18/A19 a una entrada concreta hasta medirla. A18 comparte función secundaria TOUT; la conexión física y el modo del Z180 deben comprobarse por separado.

### Prueba C — /OE y /WE

| Origen SRAM | Candidato directo Z180 | Resultado |
|---|---|---|
| U16 pin 24 /OE | U1 pin 67 /RD | medir |
| U17 pin 24 /OE | U1 pin 67 /RD | medir |
| U16 pin 29 /WE | U1 pin 66 /WR | medir |
| U17 pin 29 /WE | U1 pin 66 /WR | medir |

Si no hay continuidad directa, identificar primero el componente intermedio; no modificar el modelo del emulador suponiendo que /RD o /WR se conectan directamente.

### Prueba D — bus de direcciones

Los pines de SRAM enumerados en la tabla corresponden al pinout Samsung 32-SOP. Las conexiones hacia los pines PLCC del Z180 son candidatos directos que deben medirse para U16 y U17.

| Señal | Pin SRAM U16 y U17 | Candidato Z180 PLCC |
|---|---:|---:|
| A0 | 12 | 14 |
| A1 | 11 | 15 |
| A2 | 10 | 16 |
| A3 | 9 | 17 |
| A4 | 8 | 19 |
| A5 | 7 | 20 |
| A6 | 6 | 21 |
| A7 | 5 | 22 |
| A8 | 27 | 23 |
| A9 | 26 | 24 |
| A10 | 23 | 25 |
| A11 | 25 | 26 |
| A12 | 4 | 27 |
| A13 | 28 | 28 |
| A14 | 3 | 29 |
| A15 | 31 | 30 |
| A16 | 2 | 31 |

Realizar primero la tabla para U16 y repetirla para U17. Si una señal es compartida, ambas SRAM deben mostrar continuidad con el mismo pin de dirección del Z180. Registrar cualquier permutación; no forzar la correspondencia por el número de pin.

### Prueba E — bus de datos

| Señal SRAM | Pin SRAM U16/U17 | Candidato Z180 PLCC |
|---|---:|---:|
| I/O1 | 13 | D0, pin 37 |
| I/O2 | 14 | D1, pin 38 |
| I/O3 | 15 | D2, pin 39 |
| I/O4 | 17 | D3, pin 40 |
| I/O5 | 18 | D4, pin 41 |
| I/O6 | 19 | D5, pin 42 |
| I/O7 | 20 | D6, pin 43 |
| I/O8 | 21 | D7, pin 44 |

La equivalencia I/O1→D0 ... I/O8→D7 es la asignación de bus esperada, no una continuidad confirmada. Medir en ambos chips y anotar las permutaciones si existen.

## 5. Cómo interpretar el resultado

1. Si U16 y U17 comparten A0–A16 y D0–D7, el bus principal está paralelo y la distinción entre chips debe surgir de /CS1 (o de una ruta de selección equivalente).
2. Si /CS1 de cada SRAM termina en salidas distintas del 74HC139, identificar las entradas A/B y G de esa mitad para derivar la ecuación de selección.
3. Si /OE va a /RD y /WE a /WR, se confirma control directo de lectura/escritura; si pasan por lógica, documentar esa lógica antes de implementarla.
4. Una vez conocidos los nets de U21 y las direcciones altas, calcular qué rango físico selecciona U16 y cuál U17. No asumir que A17 divide 128 KiB/128 KiB hasta confirmar que realmente gobierna la selección.
5. Solo entonces separar la EPROM de 1 MiB de las dos SRAM de 128 KiB en MAME, con SRAM persistente y selección de chip fiel al circuito.

## 6. Estado de la emulación

El driver todavía usa una única región escribible de 1 MiB (bankrw). No se modificará esa región, no se inyectarán firmas 3C 5A, no se escribirá un descriptor BIOS artificial y no se tocará la ROM mientras no esté cerrado el mapa de selección y bancos.

La investigación del teclado/contraseña ACE135 es independiente de estas pruebas de memoria.

## 7. Fuentes técnicas

- Samsung Electronics, K6X1008C2D_SAM.pdf, Drive del proyecto, revisión 0.1 preliminar, diciembre de 2002.
- Texas Instruments, SNx4HC139 Dual 2-Line To 4-Line Decoders/Demultiplexers, SCLS108E, revisión E, febrero de 2022.
- ZiLOG, Z8S180/Z8L180 Enhanced Z180 Microprocessor, DS006002-ZMP0200, Figura 3 (pinout PLCC-68) y Tabla 1 (funciones y numeración de pines).
- WRITER_RE_06_PIC_mapa_conexiones_U11.docx, continuidad física de U11: RA3 pin 2 → CS2 pin 30 de U16/U17, ya cerrada.
