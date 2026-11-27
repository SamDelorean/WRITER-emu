# The Writer — plan de trazado físico de la matriz de teclado

**Fecha:** 2026-11-26
**Rama:** `fix/irq1-ra2-wakeup`
**Objetivo:** cerrar continuidad entre U10 (74x374), U8/U9 (74x244) y las ocho filas/dieciséis columnas físicas del teclado.

## 1. Estado de partida

La ROM y U19 ya permiten establecer la interfaz lógica:

- `OUT (0x40),A`: U19 `/Y2` activa el CP de U10. El byte A contiene el estrobo de fila (`0x80, 0x40, …, 0x01`).
- `IN (0x80)`: U19 `/Y4` habilita U8.
- `IN (0x82)`: U19 `/Y5` habilita U9.
- U11 RB3 pin 9 tiene continuidad a U9 pin 11 (2A1). Aún no se conoce qué columna física representa.
- U11 RB4 pin 10 tiene una trayectoria documentada a KB1 y por D3 hacia U10 Q1; falta cerrar el recorrido y su función exacta.

La matriz lógica de firmware es 8 filas × 16 posiciones: cada fila se lee mediante dos bancos de ocho bits. Esa coordenada lógica no identifica todavía los contactos físicos del teclado.

## 2. Pinout de referencia para el levantamiento

Los siguientes números son los pinouts estándar de los encapsulados DIP-20. Antes de medir, confirmar que los integrados de la placa tienen esa orientación y localizar físicamente el pin 1. Las funciones se contrastaron con las hojas de datos de Nexperia (74HC374) y Texas Instruments (SN74HC244).

### 2.1 U10 — 74x374

| Pin U10 | Señal | Tipo | Estado de trazado |
|---:|---|---|---|
| 1 | /OE | Entrada, activa baja | Destino/nivel físico pendiente |
| 2 | Q0 | Salida latch | Fila física pendiente |
| 3 | D0 | Entrada latch | Destino físico pendiente |
| 4 | D1 | Entrada latch | Destino físico pendiente |
| 5 | Q1 | Salida latch | Ruta parcial hacia D3/KB1; fila no cerrada |
| 6 | Q2 | Salida latch | Fila física pendiente |
| 7 | D2 | Entrada latch | Destino físico pendiente |
| 8 | D3 | Entrada latch | Destino físico pendiente |
| 9 | Q3 | Salida latch | Fila física pendiente |
| 10 | GND | Alimentación | No requiere trazado de matriz |
| 11 | CP | Reloj positivo | U19 pin 13 (/Y2), confirmado |
| 12 | Q4 | Salida latch | Fila física pendiente |
| 13 | D4 | Entrada latch | Destino físico pendiente |
| 14 | D5 | Entrada latch | Destino físico pendiente |
| 15 | Q5 | Salida latch | Fila física pendiente |
| 16 | Q6 | Salida latch | Fila física pendiente |
| 17 | D6 | Entrada latch | Destino físico pendiente |
| 18 | D7 | Entrada latch | Destino físico pendiente |
| 19 | Q7 | Salida latch | Fila física pendiente |
| 20 | VCC | Alimentación | No requiere trazado de matriz |

**Orden lógico de salidas:** Q0–Q7 corresponden a los ocho bits almacenados por el 74x374. El firmware rota el estrobo desde 0x80 hasta 0x01; no asignar todavía Q7=R0 u otra convención física sin continuidad.

### 2.2 U8 y U9 — 74x244

U8 se habilita mediante U19 pin 11 (/Y4) y se lee por el puerto 0x80. U9 se habilita mediante U19 pin 10 (/Y5) y se lee por el puerto 0x82.

| Pin 74x244 | Señal | Dirección | Estado |
|---:|---|---|---|
| 1 | 1OE | Entrada /OE activa baja | U19 /Y4 (U8) o /Y5 (U9), confirmado |
| 2 | 1A1 | Entrada | Línea física pendiente |
| 3 | 2Y4 | Salida | Bit de bus Z180 pendiente |
| 4 | 1A2 | Entrada | Línea física pendiente |
| 5 | 2Y3 | Salida | Bit de bus Z180 pendiente |
| 6 | 1A3 | Entrada | Línea física pendiente |
| 7 | 2Y2 | Salida | Bit de bus Z180 pendiente |
| 8 | 1A4 | Entrada | Línea física pendiente |
| 9 | 2Y1 | Salida | Bit de bus Z180 pendiente |
| 10 | GND | Alimentación | No requiere trazado de matriz |
| 11 | 2A1 | Entrada | U9: U11 RB3 pin 9 confirmado |
| 12 | 1Y4 | Salida | Bit de bus Z180 pendiente |
| 13 | 2A2 | Entrada | Línea física pendiente |
| 14 | 1Y3 | Salida | Bit de bus Z180 pendiente |
| 15 | 2A3 | Entrada | Línea física pendiente |
| 16 | 1Y2 | Salida | Bit de bus Z180 pendiente |
| 17 | 2A4 | Entrada | Línea física pendiente |
| 18 | 1Y1 | Salida | Bit de bus Z180 pendiente |
| 19 | 2OE | Entrada /OE activa baja | U19 /Y4 (U8) o /Y5 (U9), confirmado |
| 20 | VCC | Alimentación | No requiere trazado de matriz |

En el 74x244 los pines A son entradas y los Y son salidas no inversoras tri-state. Para reconstruir una columna completa hay que identificar tanto el lado A conectado a la matriz como el lado Y que llega al bus de datos. No deducir el bit leído por la ROM únicamente del número A1/A2/etc.

## 3. Orden de trabajo recomendado

### Etapa A — U10 y las ocho filas

1. Con la placa sin alimentación, identificar y marcar el pin 1 de U10.
2. Medir continuidad desde U10 Q0 (pin 2) hasta los contactos/pads del conector o membrana del teclado. Registrar cada destino, incluso si pasa por un diodo o resistencia.
3. Repetir para Q1 (pin 5), Q2 (pin 6), Q3 (pin 9), Q4 (pin 12), Q5 (pin 15), Q6 (pin 16) y Q7 (pin 19).
4. Para cada Q, comprobar si hay un único net o si existen ramas. No cerrar una fila hasta registrar todos los destinos del net.
5. Después, rastrear D0–D7 (pines 3,4,7,8,13,14,17,18) para determinar si vienen directamente del bus de datos Z180 o si hay lógica intermedia.
6. Revisar U10 /OE pin 1 y determinar si las salidas están siempre habilitadas o si existe control externo.

### Etapa B — U8 y U9: columnas

1. Empezar por U9 pin 11 (2A1), ya que RB3 llega a ese pin. Seguir el net desde el pin 11 hacia el conector/matriz y registrar diodos/componentes intermedios.
2. Repetir con las otras siete entradas A de U9: pines 13, 15, 17, 2, 4, 6 y 8.
3. Repetir para las ocho entradas A de U8 con el mismo orden de pines.
4. Una vez identificados los 16 nets de entrada, rastrear las ocho salidas Y de cada buffer hasta D0–D7 del Z180. Esta segunda mitad es necesaria para convertir la posición A/Y física en el bit de entrada que usa la ROM.
5. Verificar si alguna línea comparte diodos, resistencias o nets con U11 RB4/KB1.

### Etapa C — matriz y diodos

- Identificar cada contacto de fila y columna en el conector del teclado, sin asignar nombres R/C hasta completar el netlist.
- Si una tecla o línea tiene diodo en serie, la prueba de continuidad puede ser direccional. Usar modo diodo y registrar ambos sentidos; no interpretar ausencia de beep como circuito abierto sin comprobar polaridad.
- Documentar la orientación de cada diodo por su banda/cátodo y el componente de origen/destino.
- Conectar las filas y columnas lógicas de la ROM solo después de correlacionar los nets físicos y el orden de bits de U8/U9.

## 4. Seguridad y calidad de medición

- Apagar y desconectar completamente la placa antes de usar continuidad/ohmímetro. Esperar a que se descarguen los capacitores.
- No usar continuidad con la placa energizada.
- No puentear pines, forzar filas, ni inyectar niveles lógicos durante esta fase.
- No conectar J1 a USB-UART ni a equipo externo: su pinout y niveles siguen sin determinarse.
- Medir entre el pin del IC y el destino físico. Si el circuito en paralelo produce una lectura ambigua, registrar “continuidad en circuito, ruta indirecta posible”; no declarar un net directo hasta aislar/seguir la pista.
- Registrar el punto de referencia exacto y la orientación de vista de cada conector. No usar “izquierda/derecha” sin indicar desde qué cara y orientación se observa.

## 5. Formato mínimo de cada resultado

Para cada pin medido, registrar:

| Campo | Ejemplo de registro |
|---|---|
| IC/pin origen | U10 pin 2 (Q0) |
| Punto destino | JKEY contacto físico X / pad / pin de otro IC |
| Medición | Continuidad / resistencia / modo diodo |
| Polaridad de puntas | Roja en origen / negra en destino |
| Valor | Beep, Ω medidos o caída de diodo |
| Otros destinos | Todos los puntos con continuidad |
| Confianza | Directo confirmado / ruta a través de componente / ambiguo |
| Foto o referencia | Nombre de imagen y orientación, si existe |

## 6. Primer levantamiento que desbloquea el resto

La primera tanda debe ser únicamente U10 Q0–Q7, ocho medidas, con la placa apagada. Es el camino más directo para convertir los ocho estrobos lógicos de la ROM en ocho filas físicas. Después se sigue con U9, empezando por 2A1 (pin 11), para identificar la columna conectada a RB3.

No hay fotografías de la placa The Writer disponibles en los archivos accesibles de esta sesión. Por tanto, este documento prepara el pinout y el orden de medición, pero **no declara cerrada ninguna conexión física nueva**. La siguiente evidencia necesaria es una fotografía nítida de ambas caras de la placa y del conector de teclado, o los resultados de continuidad de U10 Q0–Q7. No se ha inferido ni inventado un netlist a partir de la tabla lógica de la ROM.
