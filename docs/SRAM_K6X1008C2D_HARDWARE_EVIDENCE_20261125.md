# U16/U17 — identificación y evidencia de SRAM Samsung K6X1008C2D-GB55

Fecha: 2026-11-25
Proyecto Drive: `01_Investigacion_y_desarrollo/WRITER_INSTRUCTOR`
Repositorio: `SamDelorean/writer-emu`
Rama de trabajo: `fix/irq1-ra2-wakeup`

## Resultado consolidado

La documentación y las fotografías originales de Drive permiten identificar U16 y U17 como **Samsung K6X1008C2D-GB55**. La hoja de datos subida por el usuario especifica una SRAM CMOS estática de **128K × 8 bits**. En su lista de productos, **GB55** corresponde a encapsulado **32-SOP**, tiempo de acceso de **55 ns**, familia comercial (0–70 °C) y designación LL. No se atribuye a GB55 la nota de producto lead-free, que en esa tabla marca otras variantes.

Por tanto, las dos memorias instaladas aportan una capacidad nominal conjunta de **256 KiB**, aunque el mapa de bancos y la selección individual de U16/U17 aún no están cerrados.

## 1. Evidencia primaria en Drive

Carpeta:
`01_Investigacion_y_desarrollo/WRITER_INSTRUCTOR/01_Hardware/Fotografias/`

Fotografías revisadas:
- `IMG_01D5C0E4-681F-41DA-A766-4F547526760C.jpeg`
- `IMG_0EAA76D4-257C-4DA8-AEFB-D6D450A2418F.jpeg`
- `IMG_1ED4C28A-05B1-4BEC-952C-96ABC36752FC.jpeg`
- `IMG_2D94439B-31C8-41BF-A92A-4CAE51FA1685.jpeg`
- `IMG_B9625C00-2ADC-4997-AFDD-7682FEC805BE.jpeg`

En las vistas de la placa se distinguen los designadores U16 y U17 y el marcaje Samsung `K6X1008C2D-GB55` en ambos dispositivos. La identificación es visual; la fotografía no constituye medición de señales.

Documento previo de identificación:
`01_Hardware/Identificacion_de_componentes/COMPONENTES_IDENTIFICADOS.md`

Libro de referencia actualizado en Drive: `WRITER_hardware_referencia.xlsx`, pestaña con nombre heredado `U_SRAM_K6T1008`. El contenido del pinout se corrigió para Samsung K6X1008C2D-GB55 (32-SOP) y se separó la continuidad medida de las rutas todavía hipotéticas. El nombre de la pestaña se conserva por compatibilidad con el índice existente.

## 2. Datos de hoja de datos

**Fuente primaria del proyecto:** Samsung Electronics, **K6X1008C2D Family — 128Kx8 bit Low Power full CMOS Static RAM**, copia incorporada por el usuario a Drive:
`01_Investigacion_y_desarrollo/WRITER_INSTRUCTOR/01_Hardware/K6X1008C2D_SAM.pdf`

- Revisión del PDF de Drive: **0.1, Preliminary, December 2002**.
- SHA-256 de la copia descargada desde Drive: `6ba861eeb244d238ffd2c20dd33cd86163df238e931dbff9e983aa057e20f331`.
- La copia de Drive contiene el pinout 32-SOP/32-DIP y la tabla de funcionamiento CS1/CS2/OE/WE utilizados en este informe.
- Se contrastó además con una copia pública posterior, revisión 1.0 de septiembre de 2003; el pinout 32-SOP y la tabla lógica de selección coinciden. La copia posterior no sustituye a la versión de Drive como referencia archivada del proyecto.

Datos aplicables confirmados en la hoja de Drive:
- Organización: 128K × 8 bits (131,072 bytes = 128 KiB por circuito).
- Alimentación de operación: 4.5–5.5 V.
- Umbral mínimo de retención indicado en las características: 2.0 V.
- Código GB55: 32-SOP, 55 ns, línea comercial (0–70 °C).

**Precaución de revisión:** la copia de Drive es preliminar (Rev. 0.1). No usar sus valores de consumo como valores finales de producción; para el modelo lógico de selección y el pinout sí se conserva la correspondencia corroborada con la revisión 1.0 posterior. La variante 32-TSOP Type1-Forward tiene un orden de pines distinto y no corresponde al encapsulado SOP visible en las fotografías de U16/U17.

## 3. Pinout correcto para el K6X1008C2D-GB55 (32-SOP)

La numeración siguiente es la del dibujo Samsung 32-SOP. Las entradas con barra se expresan con prefijo `/` cuando son activas en bajo.

| Pin | Señal | Función |
|---:|---|---|
| 1 | NC | Sin conexión |
| 2 | A16 | Dirección |
| 3 | A14 | Dirección |
| 4 | A12 | Dirección |
| 5 | A7 | Dirección |
| 6 | A6 | Dirección |
| 7 | A5 | Dirección |
| 8 | A4 | Dirección |
| 9 | A3 | Dirección |
| 10 | A2 | Dirección |
| 11 | A1 | Dirección |
| 12 | A0 | Dirección |
| 13 | I/O1 | Datos D0* |
| 14 | I/O2 | Datos D1* |
| 15 | I/O3 | Datos D2* |
| 16 | VSS | Tierra |
| 17 | I/O4 | Datos D3* |
| 18 | I/O5 | Datos D4* |
| 19 | I/O6 | Datos D5* |
| 20 | I/O7 | Datos D6* |
| 21 | I/O8 | Datos D7* |
| 22 | /CS1 | Selección 1, activa en bajo |
| 23 | A10 | Dirección |
| 24 | /OE | Output Enable, activo en bajo |
| 25 | A11 | Dirección |
| 26 | A9 | Dirección |
| 27 | A8 | Dirección |
| 28 | A13 | Dirección |
| 29 | /WE | Write Enable, activo en bajo |
| 30 | CS2 | Selección 2, activa en alto |
| 31 | A15 | Dirección |
| 32 | VCC | Alimentación |

* La correspondencia I/O1→D0 hasta I/O8→D7 es la asignación de bus usada en el libro de referencia; la continuidad de cada línea de datos hacia el Z180 todavía está pendiente. La hoja de datos identifica las señales I/O1–I/O8, sin imponer el nombre D0–D7 de la placa.

**Corrección crítica del libro de referencia:** en el pinout anterior de la hoja `U_SRAM_K6T1008` aparecían CS1 en pin 20, /OE en pin 23, A10 en pin 24 y CS2 en pin 25. Esas ubicaciones no corresponden al dibujo Samsung 32-SOP. El pinout correcto es /CS1 pin 22, A10 pin 23, /OE pin 24, A11 pin 25 y CS2 pin 30.

## 4. Tabla lógica de selección y acceso

La tabla funcional Samsung establece:
- `/CS1 = 1` → dispositivo deseleccionado, salidas High-Z/standby.
- `CS2 = 0` → dispositivo deseleccionado, salidas High-Z/standby.
- Dispositivo seleccionado: `/CS1 = 0` y `CS2 = 1`.
- Lectura: `/CS1=0, CS2=1, /OE=0, /WE=1`.
- Escritura: `/CS1=0, CS2=1, /WE=0` (OE no determina la escritura).
  
Por tanto, **CS2 es activa en alto**. No debe modelarse como una señal activa-baja solo por la convención de nomenclatura habitual de chip-select.

## 5. Continuidad física documentada

El documento de Drive `WRITER_RE_06_PIC_mapa_conexiones_U11.docx` indica continuidad medida sobre la placa apagada:

| Origen | Destino | Estado |
|---|---|---|
| U11 PIC16C54C, RA3 pin 2 | CS2 pin 30 de U16 y U17 | Confirmado por continuidad |
| U11 RA2 pin 1 | Z180 pin 11, /INT1 | Confirmado por continuidad |
| U11 RA4/T0CKI pin 3 | U19 74HC138 pin 12, /Y3 | Confirmado por continuidad |

La conexión RA3→CS2 es común a ambos SRAM según ese levantamiento. La continuidad del net no identifica por sí misma cuál dispositivo es seleccionado por /CS1 en cada banco.

## 6. Correlación con la traza PIC del emulador

La instrumentación ejecutada el 25-11-2026 registró los cambios de RA3 desde el callback PORTA del PIC:
- `PORTA=0x00`: RA3 bajo.
- `PORTA=0x0C`: RA3 alto.
- RA3 permaneció alto desde la transición registrada en la línea 582 de `diag/error.log` hasta la línea 1621, después de la detección de las firmas de Master Reset.
- El intervalo contiene la ejecución ROM de `0x2206`, que inspecciona la ventana física `0x40000–0x7FFFF`.

Con la tabla Samsung ya identificada, el significado es más preciso: **RA3 alto afirma el requisito de CS2=1 durante ese escaneo**. Esto es consistente con la memoria habilitada por CS2, pero no demuestra por sí solo que el chip esté seleccionado: también se requiere /CS1 bajo.

Informe dinámico original: `docs/RA3_SRAM_CS2_TRACE_20261125.md`.

## 7. Lo que aún no está probado

La documentación disponible no demuestra todavía:
1. Continuidad de /CS1 pin 22 de U16 y U17 hacia las salidas de U21 (74HC139) u otra lógica de selección.
2. Continuidad de /OE pin 24 hacia /RD del Z180.
3. Continuidad de /WE pin 29 hacia /WR del Z180.
4. Continuidad pin-a-pin de A0–A16 y de I/O1–I/O8 a los buses del Z180.
5. La relación exacta de las direcciones altas con la selección de U16 frente a U17.
6. Contenido real de las SRAM físicas y estado de respaldo de VCC por C38.
7. El mapa físico definitivo que relaciona los 256 KiB de SRAM con las ventanas físicas del Z180.

El libro de referencia asignaba provisionalmente /CS1 al 74HC139 U21, /OE al /RD, /WE al /WR, direcciones al bus A0–A16 y datos al bus D0–D7. Esas conexiones deben conservarse como **HIPÓTESIS** hasta contrastarlas con el registro de continuidad o una captura eléctrica.

## 8. Consecuencia para el emulador y siguiente trabajo

- Sustituir cualquier referencia de componente genérica K6T1008 por **K6X1008C2D-GB55** cuando se describan U16/U17.
- Corregir el pinout de referencia; no seguir utilizando la antigua ubicación de CS1/CS2.
- En el futuro modelo SRAM, implementar selección combinada: `selected = !CS1_n && CS2`, lectura por `!OE_n && !WE_n` según el ciclo de bus real, y escritura con `!WE_n` mientras CS1/CS2 habilitan el circuito. Los tiempos completos de ciclo deben contrastarse con el bus MAME.
- No activar el acceso de SRAM solamente con RA3: RA3 es CS2 compartida; falta identificar la ruta de /CS1 que discrimina bancos/chips.
- Mantener SRAM y EPROM como regiones físicas distintas; el `bankrw` plano de 1 MiB es solo un modelo diagnóstico.
- Prioridad siguiente: localizar/recuperar las mediciones de U21 pin a /CS1 de U16/U17 y las conexiones /OE, /WE. Después observar pasivamente las lecturas durante `0x2206` y determinar qué dispositivo responde a cada dirección.

No se ha modificado la ROM ni inyectado una BIOS, firmas o contenido artificial en este trabajo. La validación de la contraseña `ACE135` permanece separada.

[Estado preparado a partir de fotografías de Drive, el DOCX de continuidad U11, el libro de referencia y la hoja de datos Samsung K6X1008C2D, Rev. 1.0.]


## 9. Referencias de prueba para señales y buses aún no medidos

Se revisaron las fotografías de U21 y se contrastó el pinout con Texas Instruments. U21 está identificado en la placa como 74HC139; las fotografías confirman su ubicación, pero no las redes conectadas a sus pines. Para evitar elevar la hipótesis a hecho, la lista de pines se usa únicamente como guía de continuidad.

### U21 — SN74HC139N

| Pin | Señal | Función |
|---:|---|---|
| 1 | /1G | Habilitación activa-baja, decoder 1 |
| 2 | 1A | Selección |
| 3 | 1B | Selección |
| 4–7 | 1Y0–1Y3 | Salidas activas-bajas |
| 8 | GND | Tierra |
| 9–12 | 2Y3–2Y0 | Salidas activas-bajas |
| 13 | 2B | Selección |
| 14 | 2A | Selección |
| 15 | /2G | Habilitación activa-baja, decoder 2 |
| 16 | VCC | Alimentación |

La tabla de verdad TI confirma que con G=0 una salida seleccionada pasa a bajo y las otras quedan altas; con G=1 todas quedan altas. Por tanto es eléctricamente compatible con una función de selección /CS1, pero no prueba que U21 esté conectado a U16/U17 de esa forma.

### U1 — Z8S180 PLCC-68: candidatos para continuidad

| Señal | Pin U1 | Uso en la matriz |
|---|---:|---|
| A0–A16 | 14–31 (con hueco de VSS pin 18) | Direcciones de SRAM candidatas |
| A17 | 32 | Posible entrada de selección alta |
| A18/TOUT | 33 | Posible entrada de selección alta; función secundaria a verificar |
| A19 | 35 | Posible entrada de selección alta |
| D0–D7 | 37–44 | Bus de datos candidato |
| /MREQ | 63 | Habilitación de memoria candidata hacia U21 |
| /WR | 66 | Control de escritura candidato hacia SRAM /WE |
| /RD | 67 | Control de lectura candidato hacia SRAM /OE |

El mapa completo pin-a-pin está en docs/SRAM_BUS_CONTINUITY_TEST_MATRIX_20261125.md. Es una guía de medición, no un resultado de continuidad.

### Próximo resultado que se debe capturar

1. Salida U21 que presenta continuidad con U16 pin 22 /CS1.
2. Salida U21 que presenta continuidad con U17 pin 22 /CS1.
3. Si /OE 24 de cada SRAM llega directamente a U1 /RD 67.
4. Si /WE 29 de cada SRAM llega directamente a U1 /WR 66.
5. Continuidad completa de los buses A0–A16 e I/O1–I/O8.
6. Entradas y habilitaciones U21 que determinan el banco seleccionado.

No iniciar continuidad con tensión presente. C38 conserva respaldo para SRAM: una descarga por debajo de la tensión de retención puede borrar el contenido. No cortocircuitar C38; resolver primero si debe preservarse el contenido actual y, en ese caso, cómo volcar las dos SRAM sin interrumpir su alimentación.

Fuentes de referencia: TI SNx4HC139, SCLS108E Rev. E, pin configuration y functional modes; ZiLOG Z8S180/Z8L180, DS006002-ZMP0200, Figure 3 y Table 1.
