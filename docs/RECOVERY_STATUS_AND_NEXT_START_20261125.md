# Writer Instructor — avance de recuperación y punto de partida

Fecha: 2026-11-25  
Rama: `fix/irq1-ra2-wakeup`  
Repositorio: `SamDelorean/writer-emu`

## Estado confirmado

### 1. Interfaz PIC16C54C ↔ Z8S180

Se conserva el avance de los commits `31cc7a9` y `c652b53`: PIC RA2 → Z180 IRQ1, PIC RB0 → ASCI1 RXA1, TXA1 → PIC RB7, y lectura del estado HALT/SLP desde el estado crudo del Z180. El PIC supera la secuencia de arranque y ya no mantiene al procesador en el bloqueo anterior de IRQ1.

### 2. Entrada al procedimiento Master Reset

La ROM comprueba F1+F2 al inicio. El análisis de matriz identifica F1 en R2C6 y F2 en R6C6. El modo diagnóstico `WRITER_TEST_MASTER_RESET_KEYS=1` inyecta un único escaneo de 16 lecturas y después libera ambas teclas. La prueba alcanzó la pantalla `Enter Master Reset Password`.

**Alcance:** se validó el reconocimiento de la combinación F1+F2 y el acceso a la pantalla de contraseña. Todavía no se ha validado la captura de la secuencia de contraseña `ACE135` desde eventos de teclado reales. El modo headless usado en esta prueba no envió las teclas de contraseña.

### 3. Ejecución de la rutina original de Master Reset

Se ejecutó la rutina original de ROM en `0x2523` mediante el disparador temporal `WRITER_TEST_EXECUTE_MASTER_RESET=1`. El disparador solo redirige el PC; la inicialización posterior la ejecuta el Z180 con instrucciones de la ROM. No se inyectó ninguna firma de RAM en esta prueba.

Resultado observado en la captura posterior:

- `D000–D003 = 6A 91 3C 5A`: firma válida producida por la rutina original.
- `D800–DFFF`: 1546/2048 bytes en cero; 0 bytes `FF`; 39 valores distintos.
- `E000–EFFF`: 3317/4096 bytes en cero; 0 bytes `FF`; 130 valores distintos.
- `ECF0` contiene punteros/valores de inicialización, no un bloque uniforme.
- `D000=916A`, `D002=5A3C` en la muestra final del log.
- PC final registrado: `184F`.

Esto confirma que la rutina sí produce una RAM inicializada en las regiones observadas; no es solamente una firma artificial. No debe describirse como un borrado completo a cero: hay estructuras, vectores y valores no nulos.

### 4. Nuevo bloqueo posterior

Después de inicializar la firma y continuar el firmware, la LCD presenta:

```text
Aborting App - Fatal Error
Missing Application BIOS, 0x203B
Powering Down...
```

El código de retorno `0x203B` es la dirección que el firmware incluye en el mensaje. La ROM intenta abrir la aplicación `BIOS`, pero no la encuentra en el estado de almacenamiento actual. La hipótesis principal pasa a ser una BIOS de aplicación ausente en SRAM persistente o un error de mapeo/bancos/estructura de almacenamiento. Aún no está demostrado cuál de esas causas aplica.

## Capturas conservadas

Todas las capturas son de 65,536 bytes: una ventana lógica de 64 KiB del modelo actual. **No son un volcado completo de los 256 KiB físicos de SRAM.** El mapa de SRAM y su persistencia siguen pendientes de modelar correctamente.

| Archivo | Propósito | SHA-256 |
|---|---|---|
| `snap/rom_master_reset_test.bin` | Después de ejecutar la rutina original de Master Reset | `1b4ff17641c74fcbb074b10d086287383808a241aee60ea46cee4170e86f615f` |
| `snap/master_reset_key_test.bin` | Escaneo F1+F2 y pantalla de contraseña | `894e19239757c8b9b62b7c71b361c263d163ee57cbf5d191bd821903de651064` |
| `snap/valid_signature_test.bin` | Control: firma inyectada, no equivale a reset real | `b2003c02e43a93b0b7b42d2111c3bdaadff9fff9599f529ecee62ade758f0b2b` |

Log de la prueba de Master Reset: `diag/error.log`. El log registra la escritura de caracteres de ambos mensajes de error y el PC final.

## Punto de partida para la siguiente sesión

1. **Validar captura real de contraseña:** ejecutar el emulador con entrada gráfica o un mecanismo de eventos de teclado reproducible; registrar la matriz completa y comprobar la secuencia `ACE135`. No dar por validada esta parte a partir de la inyección de F1+F2.
2. **Investigar Application BIOS:** desensamblar desde el sitio que solicita `BIOS`, seguir la búsqueda del nombre, formato de directorio/descriptor y código que genera `0x203B`. Determinar si el archivo debe existir en SRAM no volátil o si se copia desde ROM/otra memoria.
3. **Cerrar el mapa de memoria:** reconstruir SRAM física de 256 KiB, líneas CS y bancos. El volcado actual de 64 KiB solo documenta la ventana visible.
4. **Persistencia:** implementar una imagen de SRAM completa, guardar/restaurar estado y repetir el Master Reset sobre una imagen vacía. Conservar una imagen inicializada como artefacto reproducible.
5. **Comparación:** capturar antes, inmediatamente después de Master Reset y después del intento de cargar BIOS. Diferenciar bytes escritos por la rutina de reset de los cambios de la búsqueda/carga de aplicación.

## Controles y cautelas

Los modos `WRITER_TEST_MASTER_RESET_KEYS`, `WRITER_TEST_EXECUTE_MASTER_RESET` y `WRITER_TEST_VALID_RAM_SIGNATURE` son instrumentación temporal y requieren activación explícita por variable de entorno. No deben activarse en una ejecución normal. En particular, la firma artificial sirve solo como control experimental; no es una solución de arranque.

No se ejecutó un borrado físico ni se modificó hardware real. La rutina de ROM se ejecutó en el emulador.

## Resultado ejecutivo

Se resolvió el bloqueo de comunicación PIC/IRQ1. Se demostró que el firmware reconoce F1+F2 y que la rutina original de Master Reset genera la firma y estructura inicial de RAM en las áreas observadas. El siguiente obstáculo es `Missing Application BIOS, 0x203B`. El próximo trabajo debe centrarse en la captura de contraseña, el formato de almacenamiento y la localización/carga de la Application BIOS, no en volver a forzar la firma de RAM.


## Nueva investigación — ruta de entrada de contraseña (2026-11-25, continuación)

### Separación entre detección de chord y lectura de caracteres

La rutina de matriz `0x0CF6` recorre ocho filas mediante `OUT (0x40),E` y lee dos bancos de columnas en `0x82` y `0x80`. El patrón esperado en `0x23AC` confirma F1=R2C6 y F2=R6C6: los bytes de columna `0x40` están en los índices 5 y 13 de la tabla de 16 bytes. Esta es la ruta usada para reconocer F1+F2 al arranque.

La primitiva `0x062F`, en cambio, espera el bit 7 del registro interno Z180 `0x05` y lee el registro `0x09`. Es la ruta de recepción serie ASCI1 (STAT1/RDR1), no un segundo escaneo directo de la matriz. En el flujo de arranque `0x243F–0x2456`, la ROM llama cuatro veces a `0x062F` y guarda los bytes en D011/D013.

### PIC y enlace serie

El desensamblado del PIC16C54C muestra:

- `0x027–0x03D`: transmisor bit-bang por RB0, desplaza el dato y ejecuta retardos.
- `0x0B4–0x0BB`: transmite cuatro bytes consecutivos tomados de RAM PIC `0x08–0x0B`.
- `0x0AF–0x0B3`: consulta RB7 como condición de disponibilidad/handshake antes de transmitir.
- `0x0C1–0x0C2`: genera un pulso bajo/alto en RA2, conectado a Z180 `/INT1`.
- `0x082–0x0A0` y `0x0D7–0x0E2`: la máquina de estados consulta RB2/RB3 y repite o abandona la secuencia.

Esto confirma que RB0→RXA1 es una ruta de datos real del PIC hacia el Z180. Sin embargo, el código PIC examinado no muestra todavía una decodificación completa de caracteres A/C/E/1/3/5: los cuatro bytes enviados podrían ser un paquete de estado/evento, una respuesta al handshake o datos de teclado; no se debe asumir que equivalen directamente a los caracteres.

### Consecuencia para la validación de ACE135

No se ha validado todavía la contraseña. La pantalla de contraseña fue alcanzada mediante la prueba de F1+F2, pero para introducir ACE135 se requiere comprender y reproducir la ruta que alimenta ASCI1/RDR1, además de las señales de teclado que provocan la actividad PIC. El modelo actual solo aproxima RB3 con `any_key_pressed()` y no modela RB4; por ello aún no representa completamente la interfaz eléctrica de teclado/PIC.

### Siguiente experimento controlado

1. Registrar los cuatro bytes que recibe el Z180 durante el arranque, correlacionándolos con los valores transmitidos por PIC RAM `0x08–0x0B`.
2. Seguir las rutinas PIC `0x000, 0x001, 0x00A, 0x010, 0x011, 0x015, 0x022, 0x025, 0x050, 0x059, 0x062, 0x06B` para reconstruir el origen de esos cuatro bytes y las condiciones RB2/RB3/RB7.
3. Determinar si las teclas de contraseña entran por la matriz directa Z180, por el PIC/ASCI1 o por una combinación de ambas rutas.
4. Solo entonces crear una prueba automatizada de ACE135 que use los mismos registros/entradas que el hardware, sin saltar la rutina de lectura de la ROM.


### Corrección de interpretación — ASCI1 no está identificado como canal de contraseña

La primera lectura de `0x062F` como posible entrada de caracteres fue prematura. El PIC confirma una función de temporización:

- PIC `0x015–0x021`: incrementa una cuenta de 40 bits distribuida en `0x0C` y `0x08–0x0B`.
- PIC `0x0B4–0x0BB`: transmite los cuatro bytes `0x08–0x0B` por RB0.
- Z180 `0x2442–0x2456`: lee cuatro bytes mediante `0x062F` y los guarda en D011/D013.

Por ello, el enlace RB0→RXA1 está confirmado como transporte de la cuenta temporal del PIC en el arranque. No hay evidencia suficiente para afirmar que transporte las letras de la contraseña. La ruta de caracteres de `ACE135` sigue abierta.

La ROM compara `D03C` contra la cadena `ACE135\0` en `0x24B5–0x24BB`. El punto pendiente es identificar quién llena D03C y cómo se alimenta la cola de eventos que consume `0x0CBE` desde D08D. El único barrido directo de matriz identificado en el mapa estático de I/O es `0x0CF6`, usado en la comprobación de teclas especiales del arranque. Hay otros accesos a puertos en direcciones superiores, pero deben validarse como código ejecutable y no como falsos positivos de datos.

**Estado correcto:** F1+F2 por matriz directa está demostrado; firma y estructura RAM tras Master Reset están demostradas; el ingreso real de ACE135 y el origen de D03C/D08D no están demostrados. El próximo análisis debe rastrear escritores de D03C y productores de la cola D08D, además de validar el código que escanea la matriz durante la fase de contraseña.


### Continuación — consumidor de eventos y descarte de falsos positivos (2026-11-25)

El análisis de `0x0CBE`, `0x0D55` y el flujo `0x2492–0x24BB` permite precisar la ruta:

- `0x0CBE` es el consumidor FIFO. Lee el contador de elementos de 16 bits en `D0AD`; toma el primer elemento de dos bytes desde `D08D`, decrementa el contador y desplaza el resto de la cola hacia el inicio con `LDIR`.
- `0x0D55` primero revisa `D0AF`. Si hay un manejador diferido, lo consume y salta a `0x0E05`; si no, llama a `0x0CBE`. El valor obtenido se normaliza y se usa como índice para despachar mediante una tabla de punteros en `0x0964` (`JP (HL)`). El listado estático disponible termina en `0x0963`, por lo que aún no se han identificado los manejadores de esa tabla.
- `0x0D20` registra un puntero/valor en `D0AF`; `0x0E9B–0x0EBB` inicializa el estado de eventos, poniendo a cero `D0AD`, `D0AF`, `D08A`, `D088` y `D089`.
- `0x24B5` compara el contenido de `D03C` con la contraseña. `0x27C1` recorre una cadena terminada en NUL y convierte letras minúsculas a mayúsculas in situ. Esto es normalización, no captura de teclado.
- `0x267D` no escribe en `D03C`: carga el carácter `'*'` y salta a `0x257A`, una ruta de salida/formateo. Por tanto, no es el productor del búfer de contraseña.

Los únicos usos directos de `D08D` en el listado son las lecturas y el desplazamiento de `0x0CBE`; no aparece un escritor absoluto de la cola. El productor probablemente utiliza direccionamiento indirecto, otro segmento de código o una ruta de interrupción no cubierta por el desensamblado lineal. `D08A` se inicializa, pero tampoco tiene referencias directas adicionales en este listado; no se le asigna todavía una función sin más evidencia.

Los supuestos accesos `OUT0 (0x82)` en `0x82CF` e `IN0 B,(0x82)` en `0x83D9` están rodeados de bytes desensamblados como instrucciones incoherentes y no forman una secuencia de control plausible. Se clasifican como falsos positivos probables por desensamblado de datos; no son evidencia de un escáner de teclado.

**Conclusión de esta pasada:** está demostrado el formato y consumo de la cola de eventos y el despacho por tabla; no está localizado su productor ni el escritor de `D03C`. La siguiente prueba debe instrumentar escrituras en `D03C` y `D08D` durante la pantalla de contraseña, y registrar PC/HL/DE/BC al producirse cada escritura. En paralelo, hay que recuperar/desensamblar el bloque de tabla y manejadores desde `0x0964` y el código de interrupción asociado. No se debe inyectar ACE135 ni modificar la lógica de entrada hasta identificar esa ruta.

### Continuación — productor FIFO y matriz de caracteres (2026-11-25)

La instrumentación dinámica corrigió el mapeo físico de la RAM: las direcciones lógicas `D000-DFFF` observadas por la ROM corresponden a la ventana física `CD000-CDFFF`. Los write taps en `CD03C-CD041` y `CD08D-CD0AF`, junto con read taps en `CD0AD-CD0B0`, no alteran las lecturas/escrituras originales.

Se localizó el productor que faltaba:

- ISR `0x182A` llama a `0x0C40`.
- `0x0C40` gestiona antirrebote/repetición; llama al escáner `0x0BE2`, a la lectura de modificadores `0x0B64` y al encolador `0x0C22`.
- `0x0C22` inserta eventos de 16 bits en `D08D + 2*D0AD`; `D0AD` cuenta eventos. La capacidad inferida es 16 eventos.
- `0x0BE2` escanea la matriz mediante puerto `0x40` y lecturas `0x80/0x82`; `0x0BD3` resuelve el índice de tecla.

La tabla de 128 entradas de `0x0964`, con stride de cuatro bytes, permite reconstruir las posiciones y códigos de `ACE135`: A `R3C6/B9`, C `R2C10/A5`, E `R0C10/85`, 1 `R1C6/99`, 3 `R1C10/95`, 5 `R4C14/C1`. El byte alto del evento transporta modificadores.

La prueba dinámica confirma dos fases: una tecla A temprana puede entrar y salir de la FIFO antes de que se inicialice `D03C`; una A tardía sí se encola después del prompt, pero en esa ejecución `0x0D55` lee `D0AF=F8`, toma la ruta de resultado pendiente y no consume la FIFO. No se observó escritura de carácter en `D03C-D041`. La sincronización entre inicialización del búfer, espera `0x0E08`, `D0AF` y despacho FIFO sigue abierta.

El informe detallado, incluyendo registros de prueba, tabla de posiciones, instrucciones y próximos pasos, está en `docs/PASSWORD_INPUT_TRACE_20261125.md`. No se ha validado `ACE135` ni la ruta auténtica de comparación de contraseña. La rutina ROM `0x2523` se ejecutó después mediante un bypass diagnóstico de IRQ1 y generó las firmas de inicialización; el error posterior `Missing Application BIOS, 0x203B` permanece.


### Continuación — evento A posterior a la inicialización del editor (2026-11-25)

Se ejecutó una prueba más precisa: Lua espera a que el editor tenga `D1E7=D03C`, `D1E9=D03C` y `D1EB=6`, y entonces pulsa A en `R3C6` (frame 137). El evento `0x00B9` se inserta correctamente en `D08D`; `D0AD` pasa de 0 a 1. Por tanto, esta vez la tecla sí llega a la FIFO después de inicializarse el búfer de contraseña.

Sin embargo, no se observa una lectura de `D08D` ni una ejecución del consumidor `0x0CBE` (su lectura de `D0AD` se esperaría con `PC_NEXT=0CC2`). No hay escritura de carácter en `D03C-D041` y `D0AD` permanece en 1 hasta el snapshot final. `D0AF` todavía vale cero cuando se encola A; posteriormente se escribe `0x00F8` desde `0x0D20` y se limpia en `0x0D55`. Por orden temporal, F8 es un resultado posterior de espera, no la causa inicial de que A no sea consumida.

Después del IRQ, los PCs muestreados pasan por código `0Cxx/183x` y luego por otras rutinas `106x/16xx`; no se observa el retorno normal a `0x0E29` seguido de `0x0E0F/0x0E12` y `0x0CBE`. La hipótesis de trabajo cambia: la matriz y el productor FIFO funcionan; el siguiente objetivo es reconstruir el retorno de interrupción/stack del Z180 al despertar de `SLP` y verificar por qué el flujo foreground del editor no vuelve a despachar. No se debe forzar el consumo de la FIFO ni modificar ROM.

Prueba y snapshot: `diag/error.log`, `snap/password_immediate_A_20261125.bin`; script: `diag/password_immediate_A.lua`. El informe técnico detallado está en `docs/PASSWORD_INPUT_TRACE_20261125.md`. La entrada auténtica de `ACE135` sigue sin validarse; la ejecución diagnóstica de `0x2523` está documentada en la sección final. `Missing Application BIOS, 0x203B` sigue sin resolverse.


### Continuación — muestreo de PC durante el bloqueo de entrada (2026-11-25)

Se añadió `WRITER_TRACE_PASSWORD_FLOW=1`, desactivado por defecto. El driver toma una muestra por frame de PC, SP, IFF1/IFF2, D011/D017, D0AD/D0AF y punteros del editor. La compilación de `writerdiag` terminó correctamente.

En la prueba con A inmediata, el muestreo observó `PC=0x0E27`, `SP=0xCFF0`, `IFF1=1` mientras el Z180 estaba en SLP. Tras el evento, el PC muestreado pasó por `0Cxx/18xx` con IFF1=0 y luego por `106x/11xx/16xx/27xx` con IFF1=1. `D0AD` permaneció en 1 y `D0AF` en 0. No se muestrearon `0E29`, `0E0F`, `0E12` ni el consumidor `0CBE`; al ser una muestra por frame, esto no demuestra que nunca se ejecutaran entre muestras. Sí confirma que la entrada permanece pendiente durante muchos ciclos de IRQ/temporizador.

El nuevo trace está en `diag/error.log` y la RAM en `snap/password_flow_A_20261125.bin`. El siguiente paso es observar la dirección de retorno y la pila al entrar/salir de IRQ1, y explicar por qué la FIFO no se atiende después del despertar. No cambiar ROM ni forzar el consumo.


### Continuación — ejecución real de ROM `0x2523` mediante despertar IRQ1 (2026-11-25)

Se consiguió ejecutar la rutina de Master Reset de la ROM sin cambiar el PC mientras el Z180 estaba dormido en `SLP`. La prueba usa `WRITER_TEST_MASTER_RESET_KEYS=1` y `WRITER_TEST_BYPASS_PASSWORD_TO_RESET=1`; no modifica bytes de ROM ni inyecta las firmas de RAM.

Secuencia y evidencia:

1. El chord F1+F2 alcanza `Enter Master Reset Password`. El editor está inicializado: `D1E7=D03C`, `D1E9=D03C`, `D1EB=0006`, `D1ED=008C`; el Z180 espera en `0E27`, `SP=CFF0`.
2. El driver lee el vector real de IRQ1 en `D700-D701` (`182A`) y lo redirige temporalmente a `2523`. El PIC permanece operativo y conserva RA2→`/INT1`; no se fuerza el despertar.
3. El siguiente pulso PIC en RA2 despierta al Z180 desde `SLP` y entrega la ejecución al destino `2523` a través del vector IRQ1. La rutina comienza con `CALL 22F3`; en `2526` la ROM instala su propia pila `SP=D000`.
4. La ROM ejecuta sus llamadas de inicialización (`112B`, `10CC`, `10B1`, `2206`) y escribe por sí misma `D000=916A` y `D002=5A3C`. Al detectar ambas firmas, el driver restaura `D700-D701=182A`.
5. La traza final termina en `PC=2568`, `SP=D000`, `D000=916A`, `D002=5A3C`, `D073=00`, `D011=004A`. La pantalla capturada muestra `Powering Down...`; el registro contiene `Aborting App - Fatal Error / Missing Application BIOS, 0x203B / Powering Down...`.

**Resultado:** la entrada a `0x2523` y la inicialización ROM que produce las firmas están validadas. El vector original `0x182A` también se restaura. El fallo posterior `Missing Application BIOS, 0x203B` permanece. Esta prueba es un bypass diagnóstico de teclado/comparación: no demuestra que `ACE135` haya sido introducida o aceptada, ni equivale a una prueba de contraseña auténtica.

Artefactos de esta ejecución:

- Log: `diag/error.log` (marcadores `password editor active`, `redirected IRQ1 vector D700 182A->2523`, `ROM reset signature ... restored IRQ1 vector 182A`, `FINAL PC=2568`).
- Build/run: `diag/build.log`.
- RAM (ventana lógica de 64 KiB): `snap/live_snapshot_20261125_145659.bin`.
- LCD: `snap/frame_20261125_145659.ppm` (vista PNG derivada: `snap/frame_20261125_145659.png`).

**Siguiente bloqueo:** determinar qué imagen/segmento de Application BIOS espera la ROM en la ruta de error `0x203B`, y si está ausente en el dump o si no se modela el banco/medio desde el que se carga. Mantener separada esta investigación del problema de entrada de `ACE135`.
