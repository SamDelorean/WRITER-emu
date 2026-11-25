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
