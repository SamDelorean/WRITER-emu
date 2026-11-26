# Writer Instructor — estado consolidado de ingeniería inversa
Fecha de corte: 2026-11-26  
Repositorio: `SamDelorean/WRITER-emu`  
Rama de trabajo: `fix/irq1-ra2-wakeup`  
Firmware objetivo: `akt_writer_v3.70_m27c801.bin`  
SHA-256 de la ROM: `f4086f4c792f72b7ee7a1a32334f8a3df9e8dedee3eaa20071915adee1540da9`

## 1. Alcance y calidad de evidencia

Este documento consolida el conocimiento disponible sobre la matriz/teclado, la ruta de eventos, la traducción de teclas, la salida al LCD y el estado general del trabajo de ingeniería inversa.

La evidencia de teclado procede de análisis estático de la ROM y de las trazas/pruebas de emulación documentadas previamente. Las direcciones de ROM citadas corresponden a la vista lógica baja de 64 KiB. Aunque la imagen EPROM tiene 1 MiB, **no se ha completado todavía el análisis de todos sus bancos ni de la superposición ROM/SRAM**.

La identificación de procesador usada en la pasada corregida es **Ghidra 12.1.3, lenguaje `z180:LE:16:default`**. Las primeras notas se generaron en un proyecto con lenguaje Z80 de 16 bits; esa configuración se conserva como antecedente, pero no debe tomarse como la configuración de referencia para nuevos análisis. Las fases de extracción de matriz/modificadores, manejadores y salida de caracteres/glyphs se ejecutaron también en el proyecto nuevo Z180. El análisis fue dirigido por scripts y desensamblado semilla (`-noanalysis`); no equivale a un análisis automático exhaustivo de toda la ROM.

No se modificaron bytes de ROM ni se aplicaron parches al emulador como parte de este análisis estático.

## 2. Arquitectura del teclado

### 2.1 Matriz y puertos

La matriz es de **8 filas × 16 posiciones lógicas**. La rutina de exploración identificada en `0x0BE2`:

1. Usa el puerto `0x40` para activar la fila.
2. Comienza con el estrobo `E=0x80` y lo rota a la derecha para recorrer las ocho filas.
3. Lee dos bancos de columnas: puertos `0x82` y `0x80`.
4. La rutina `0x0BD3` examina los bits de las lecturas y construye el índice lógico de tecla.

Transformación estática reconstruida:

```text
index = row * 16 + bank * 8 + (7 - input_bit)

row        = 0..7
bank       = 0 para puerto 0x82
           = 1 para puerto 0x80
input_bit  = 0..7
```

Esta fórmula describe el índice usado por la ROM, no una leyenda física del teclado. La polaridad eléctrica y la correspondencia definitiva entre cada índice y la posición/leyenda impresa deben confirmarse con trazas controladas de una sola tecla.

### 2.2 Tabla de traducción

Se localizó una tabla de **128 entradas** en `0x0964–0x0B63`, con 4 bytes por entrada (512 bytes en total):

| Desplazamiento | Contenido |
|---:|---|
| +0, +1 | Puntero little-endian al manejador |
| +2 | Dato base |
| +3 | Dato alternativo |

No es una tabla ASCII plana. El manejador interpreta los bytes de datos según la clase de tecla. En entradas alfabéticas y de puntuación, el segundo dato suele ser la variante alternativa; en teclas especiales, el byte puede ser un código de acción.

La extracción completa, con 128/128 entradas, está en [`KEYBOARD_TABLE_0964_20261126.tsv`](KEYBOARD_TABLE_0964_20261126.tsv). El informe de matriz existente es [`KEYBOARD_MATRIX_AND_TRANSLATION_TABLE_20261126.md`](KEYBOARD_MATRIX_AND_TRANSLATION_TABLE_20261126.md). La tabla incluye posiciones sin acción, caracteres imprimibles, controles y códigos especiales; no asignar leyendas a las entradas todavía desconocidas por simple proximidad.

### 2.3 Muestreo de modificadores

La rutina `0x0B64` compone una máscara de cinco bits. Las posiciones eléctricas examinadas son:

| Bit de máscara | Estrobo de fila | Puerto | Bit leído | Índice lógico derivado |
|---:|---:|---:|---:|---:|
| 0 | `0x20` | `0x82` | 1 | `0x26` |
| 1 | `0x04` | `0x82` | 1 | `0x56` |
| 2 | `0x02` | `0x80` | 1 | `0x6E` |
| 3 | `0x10` | `0x80` | 7 | `0x38` |
| 4 | `0x08` | `0x82` | 7 | `0x40` |

Los nombres semánticos de esos cinco bits siguen **sin asignarse**. No etiquetarlos todavía como Shift, Ctrl, Alt, Caps Lock, etc. El manejador alfabético muestra que los bits 0 y 1 seleccionan conjuntos alternativos distintos, posiblemente dos canales de modificación laterales; esta interpretación es una hipótesis funcional, no una identificación física definitiva.

### 2.4 Exploración, debounce y FIFO

La ruta estática principal queda trazada así:

- `0x0BE2`: exploración de filas y lectura de los dos bancos de columnas.
- `0x0BD3`: resolución de bit/índice.
- `0x0B64`: lectura de los cinco estados modificadores.
- `0x0C40`: debounce y seguimiento del estado actual/anterior de matriz.
- `0x0C22`: productor FIFO.
- `0x0CBE`: consumidor FIFO.
- `0x0D55`: dispatcher de eventos y manejadores.

La cola comienza en `D08D`, usa eventos de dos bytes y un contador en `D0AD`. El límite observado corresponde a **16 eventos**. El byte bajo contiene el índice lógico; el byte alto contiene la máscara de cinco bits. El dispatcher prioriza el resultado diferido en `D0AF`; si no hay uno pendiente, consume la FIFO. Antes de calcular la entrada de tabla limpia el bit 7 del índice. No se debe asignar al bit 7 un significado make/break sin una prueba separada.

## 3. Manejadores de tecla y semántica

| Rutina | Comportamiento reconstruido |
|---|---|
| `0x0D68` | Manejador sin salida/por defecto; numerosas posiciones de tabla apuntan aquí. |
| `0x0D8C` | XOR del byte de entrada contra el estado en `D088`. La entrada conocida `0x5A` usa dato `0x20` y conmuta ese bit; no es la tecla imprimible Space. |
| `0x0D94` | Devuelve un código especial opaco en L, con H=0. La entrada `0x27` devuelve `0x00A7`. |
| `0x0D9A` | Devuelve un código de acción en L y la máscara `D088 & 0x1F` en H. |
| `0x0DA4` | Manejador de puntuación/símbolos. Según la máscara, selecciona el byte base o el alternativo. |
| `0x0DBC` | Manejador alfabético. Gestiona la variante de carácter y una compuerta de modo en `D085`. |

El manejador alfabético empieza con el dato base si `D088=0`; de otro modo considera inicialmente el dato alternativo. Luego aplica el estado de bits 2–4 y la compuerta `D085`. Cuando exactamente uno de los bits 0/1 está activo en el modo filtrado, comprueba el carácter alternativo contra uno de dos conjuntos de ROM; si no pertenece al conjunto correspondiente, puede suprimir la salida. Los conjuntos extraídos son:

- Conjunto asociado al bit 0, región `0x0D24–0x0D3E` (27 bytes): ` ^&*()_+YUIOP{}|HJKL:NM<>?`
- Conjunto asociado al bit 1, región `0x0D3F–0x0D54` (22 bytes): ` ~!@#$%QWERTASDFGZXCVB`

Esto sugiere dos canales de modificación con grupos de teclas diferenciados. La denominación física/funcional de cada tecla modificadora sigue pendiente.

La rutina de configuración `0x0E77` (vector API `0x01B0`) toma tres bits de configuración: bit 0 controla `D085`; bit 1 controla `D086` (máscara/signo aplicado al índice); bit 2 selecciona el valor de `D087` (`0xE0` o `0xC0`). La rutina de reinicio `0x0E9B` limpia FIFO/estados y restaura el conjunto por defecto (`D085=0`, `D086=0`, `D087=0xE0`).

## 4. Códigos especiales y discrepancia del chord Master Reset

La tabla contiene códigos de acción además de caracteres imprimibles. Entre las entradas identificadas están:

- Índice `0x27`: manejador `0x0D94`, dato `0xA7`, resultado opaco `0x00A7`.
- Índice `0x69`: manejador `0x0D9A`, dato `0x1B` (código ESC).
- Índice `0x5A`: manejador `0x0D8C`, dato `0x20`, conmuta `D088` bit 5.
- Índice `0x7B`: Space imprimible normal, manejador de símbolos, base/alternativo `0x20/0x20`.

La comparación de patrones de Master Reset está cerca de `0x0CF6`; los patrones almacenados en ROM alrededor de `0x23AC` incluyen dos posiciones que, al aplicar la transformación actual, corresponden a índices `0x29` (tabla `z/Z`) y `0x69` (ESC). Sin embargo, documentación y pruebas previas del emulador habían identificado el chord de arranque como F1+F2 en R2C6/R6C6. **La correlación entre el patrón crudo, el orden de bits/puertos y las entradas de traducción aún no está cerrada.** No reasignar ni fijar leyendas F1/F2 en la tabla hasta reconciliar esta discrepancia con una captura de una tecla individual y el índice/evento real.

## 5. Entrega al editor y ruta de contraseña

El lector bloqueante de eventos está en `0x0E08` (API `0x01D0`). La entrada del editor, alrededor de `0x25DE`, llama al lector y despacha el resultado por una tabla de callbacks:

| Resultado | Callback | Interpretación observada |
|---:|---:|---|
| `0x00F8` | `0x2673` | Guarda resultado diferido/restaura contexto de cursor |
| `0x00A7` | `0x2673` | Mismo callback diferido |
| `0x001B` | `0x266E` | Escribe cero en el puntero de carácter actual |
| `0x000D` | `0x2677` | Restaura/actualiza contexto del cursor |
| `0x0008` | `0x2636` | Backspace |

Los resultados no capturados por la tabla pasan al camino de inserción ASCII en `0x25FD`. Este acepta bytes bajos `0x20–0x7F`, comprueba bits de estado y límites de buffer, e inserta en el buffer del editor. Por tanto, el enlace directo normal desde el teclado al editor es de 7 bits.

La ROM contiene la cadena de contraseña `ACE135\0` y una comparación con el buffer en `D03C` en el flujo de Master Reset (`0x24B5–0x24BB`); la igualdad conduce a la ruta que alcanza `0x2523`. **No está demostrado que la contraseña se haya introducido correctamente mediante teclas reales.** Se ha alcanzado la pantalla de contraseña mediante la prueba del chord y se ha ejecutado la rutina original `0x2523` mediante un desvío diagnóstico; ese desvío no prueba captura ni aceptación de `ACE135`. El siguiente análisis de entrada debe identificar los escritores de `D03C` y el productor real de eventos durante la fase de contraseña, sin forzar el consumidor FIFO.

## 6. Salida de caracteres, LCD y acentos

La primitiva de salida está en `0x0812`:

- Si el bit 7 de L está limpio, envía `L | 0x80` como dato de carácter por el puerto `0xC0`.
- Si el bit 7 está activo, trata el valor como comando del controlador y sigue la ruta `0x0837`, con handshake/control en `0xC2`.

La rutina `0x0874` carga un glyph personalizado desde un registro de 9 bytes (slot + ocho filas bitmap); `0x0890` transmite el glyph a ambos controladores LCD. La inicialización `0x08C5` carga los slots `0x0C–0x0F` con patrones de bloque/vacío:

- `0x0C`: ocho filas `00`.
- `0x0D`: cuatro filas `1F`, seguidas de cuatro `00`.
- `0x0E`: cuatro filas `00`, seguidas de cuatro `1F`.
- `0x0F`: ocho filas `1F`.

No son letras acentuadas. La tabla normal del teclado y la inserción estándar del editor son de 7 bits; el bit 7 en la salida LCD distingue comandos/datos, no una extensión Latin-1. **Conclusión limitada:** la ruta normal teclado → editor → salida de texto identificada no proporciona directamente caracteres acentuados. No se ha demostrado que toda la aplicación sea incapaz de producirlos: todavía podrían existir rutas de dibujo bitmap, otras tablas, aplicaciones/bancos ROM adicionales o escrituras especiales al controlador.

## 7. Estado general de la ingeniería inversa

### Confirmado o reproducido

- La interfaz PIC16C54C ↔ Z8S180 dejó atrás el bloqueo de arranque anterior: PIC RA2 está asociado a `/INT1`; RB0 transporta datos a ASCI1 RXA1; TXA1 se observa en RB7; RB1 observa el estado de `/HALT`/SLP.
- El firmware reconoce el chord de arranque documentado como F1+F2 y llega al prompt de contraseña en pruebas de emulación; la correlación exacta del chord con la tabla necesita la reconciliación descrita arriba.
- La rutina original de ROM `0x2523` se ha ejecutado en el emulador por desvío temporal de diagnóstico. La propia ROM produjo `D000–D003 = 6A 91 3C 5A` (`D000=916A`, `D002=5A3C`); no fue una firma inyectada en esa prueba.
- El decodificador U19 (74HC138) y sus relaciones con el Z180/puertos han sido documentados con continuidad y contraste de pinout. U19 selecciona, entre otras señales, los buffers de columna U8/U9 y el latch U10; véase [`U19_74138_PIN_MAP_20261125.md`](U19_74138_PIN_MAP_20261125.md).
- Se identificó y documentó la tabla completa de 128 entradas del teclado y las rutinas de scan, debounce, FIFO y dispatch.

### Bloqueo actual de arranque

Después del Master Reset, el firmware llega a:

```text
Aborting App - Fatal Error
Missing Application BIOS, 0x203B
Powering Down...
```

El análisis estático indica que `0x20FF` busca el índice `0x12` para `SHELL`, pero la entrada correspondiente `D0DF` está en cero. La rutina `0x2206` mapea la dirección lógica `F000` a la región física `0x40000` (con `CBR=31`, `CBAR=F0`) y espera un encabezado `3C 5A`; en la imagen EPROM suministrada se observó `FF 00` en esa posición y no se localizaron marcadores `3C 5A` en `0x40000–0x7FFFF`. Esa región corresponde a una ventana de 256 KiB, equivalente a la capacidad SRAM física total documentada. La hipótesis de trabajo es que falta una imagen/overlay de SRAM o su contenido persistente, pero la decodificación completa de chip-selects y el mapa real de bancos siguen sin confirmarse.

Los volcados de RAM mencionados en documentación anterior son ventanas lógicas de 64 KiB. **No representan un volcado completo de los 256 KiB físicos de SRAM** (dos dispositivos de 128 KiB). El modelo actual de shadow de 1 MiB es diagnóstico y no representa fielmente un overlay EPROM/SRAM ni la persistencia.

## 8. Pendientes priorizados

1. Resolver experimentalmente la relación puerto/bit → índice para una sola tecla y cerrar la discrepancia del chord Master Reset antes de asignar leyendas físicas.
2. Identificar el significado de los cinco bits modificadores con pruebas individuales; después mapearlos a leyendas físicas.
3. Rastrear los escritores de `D03C`, el productor de `D08D` y el origen de los eventos durante el prompt; validar la entrada real de `ACE135` sin bypass.
4. Completar el mapa de bancos de la ROM EPROM de 1 MiB y reconstruir registros Z180 BBR/CBR/CBAR a lo largo de las rutas de carga.
5. Reconstruir los selects por chip de las dos SRAM, su bus de datos/direcciones, `/OE`, `/WE` y persistencia; después determinar cómo se localiza/carga la Application BIOS.
6. Inspeccionar los bancos y rutinas de aplicaciones para establecer si existe una ruta de glyph bitmap para acentos u otros caracteres fuera del camino ASCII estándar.
7. Mantener las pruebas de bypass, firmas artificiales y estímulos de teclado como instrumentación diagnóstica explícita; no convertirlas en comportamiento normal del emulador.

## 9. Artefactos y referencias del repositorio

- [Tabla completa de traducción de teclado](KEYBOARD_TABLE_0964_20261126.tsv)
- [Informe de matriz y tabla de traducción](KEYBOARD_MATRIX_AND_TRANSLATION_TABLE_20261126.md)
- [Mapa de conexiones U19 74HC138](U19_74138_PIN_MAP_20261125.md)
- [Estado de recuperación y punto de partida previo](RECOVERY_STATUS_AND_NEXT_START_20261125.md)
- [Mapa de conexiones U20 7400](U20_7400_PIN_MAP_20261125.md)

Las ROM y los dumps de PIC/ SRAM no se agregan a este repositorio. El proyecto Ghidra Z180 y sus bases de datos son artefactos locales de análisis; los informes y exportaciones textuales deben versionarse por separado. La carpeta temporal de Drive contiene el paquete de evidencias subido el 2026-11-26, pero el conector directo de Drive no estaba habilitado durante esta publicación.
