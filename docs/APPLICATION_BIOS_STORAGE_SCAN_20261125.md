# Application BIOS — análisis de búsqueda de almacenamiento

Fecha: 2026-11-25  
Rama: `fix/irq1-ra2-wakeup`  
ROM: `akt_writer_v3.70_m27c801.bin` (1 MiB)  
SHA-256 ROM: `f4086f4c792f72b7ee7a1a32334f8a3df9e8dedee3eaa20071915adee1540da9`

## Resultado

El mensaje `Missing Application BIOS, 0x203B` se produce porque la búsqueda de la aplicación `SHELL` no devuelve un descriptor. La tabla de directorios en RAM deja vacío el índice `0x12` (`D0DF=0000`); la búsqueda se agota y la rutina genera el error en la dirección `0x203B`.

La evidencia apunta a que el emulador presenta datos de EPROM en una ventana donde el firmware espera almacenamiento SRAM/bloques de aplicaciones. La causa eléctrica exacta y el contenido de SRAM de una unidad real todavía no están verificados; no se debe declarar cerrado el mapa de memoria.

## 1. Cadena de ejecución y significado de 0x203B

La rutina de ROM `0x20FF`, llamada desde `0x2560` después de la inicialización de Master Reset:

1. Pone a cero `D023`, `D021` y `D03A`.
2. Copia `BIOS\0` desde `0x20E3` a `D031`.
3. Carga `HL=0x12` y `DE=0x20E8`, donde está `SHELL\0`.
4. Llama a `0x119D` para seleccionar el índice `0x12` de la tabla en `D0BB`, y a `0x11B0` para recorrer la lista y comparar sus descriptores.
5. Continúa a `0x2015`. Cuando no hay descriptor (`HL=0`) y `DE=0`, carga el texto `Missing Application` y llama a `0x1FF7` en `0x203B`.

Por tanto, `0x203B` es la dirección de la llamada al manejador fatal que se muestra como código de error; no es un ID de BIOS ni la dirección de una imagen BIOS.

## 2. Índice vacío en la captura

La selección del índice se calcula en `0x1179`:

`D0BB + (0x12 × 2) = D0DF`.

En `snap/live_snapshot_20261125_145659.bin`:
- `D031` contiene `BIOS\0`.
- `D0E3` contiene el puntero al texto `SHELL\0` (`0x20E8`).
- `D0E7` termina en cero después de recorrer la lista.
- `D0DF-D0E0 = 00 00`: el índice `0x12` está vacío.

El resultado concuerda con la ruta de error observada. El Master Reset inicializa explícitamente la tabla `D0BB-D0E2` en `0x112B`; sus entradas se deben poblar después si el escaneo encuentra bloques válidos.

## 3. Región física que escanea la ROM

La rutina `0x2206` lee `CBR`, calcula una ventana con `0x1064`, escribe el nuevo CBR en el registro Z180 `0x38` y comprueba una cabecera:
- palabra inicial: tamaño/longitud del bloque;
- firma requerida en los dos bytes siguientes: `3C 5A`;
- byte de flags posterior: sus bits 0, 1 y 2 habilitan las inserciones de índices `0x11`, `0x12` y `0x13` mediante `0x21CD`, `0x21E0` y `0x21F3`.

En esta ruta `HL=0xE400` entra a `0x1064`. La aritmética de la rutina produce `HL=F000` y `A=31`; el código escribe `CBR=0x31`. Con el `CBAR=0xF0` establecido por el arranque, la MMU del Z180 asigna la página lógica `F000` a:

`(0x31 << 12) + 0xF000 = 0x40000`.

La fuente de MAME del Z180 confirma la traducción mediante `m_mmu_cbr << 12` para la zona común superior.

## 4. Comparación con la EPROM suministrada

En el archivo EPROM, los bytes físicos en `0x40000` son:

`37 22 FF 00 ...`

La firma debería estar en `0x40002-0x40003`, pero allí hay `FF 00`, no `3C 5A`. Además, la búsqueda binaria de `3C 5A` no encuentra ninguna aparición en `0x40000-0x7FFFF`.

La zona `0x40000-0x7FFFF` mide exactamente 256 KiB, igual a la capacidad total documentada de las SRAM U16/U17 (2 × 128 KiB). También está documentada la conexión física PIC RA3 → CS2 de esas SRAM. Esto hace que la ventana sea compatible con el almacenamiento SRAM esperado por el firmware, pero el decodificado de direcciones y la selección efectiva de ambos chips todavía requieren verificación de hardware.

## 5. Limitación del modelo de memoria actual

El driver actual:
- asigna un único arreglo writable de 1 MiB;
- mapea todo el espacio físico `00000-FFFFF` a ese arreglo mediante `bankrw`;
- copia los 1 MiB de la EPROM a ese arreglo en `machine_reset`;
- todavía no separa la EPROM de las dos SRAM ni implementa persistencia/NVRAM de SRAM.

Por ello, cuando el firmware cambia CBR a `0x31`, la ventana `F000` lee los bytes de la EPROM cargada en `0x40000`, no una imagen independiente de SRAM. La búsqueda falla exactamente en la cabecera de la primera ubicación examinada, de modo que no se crea el descriptor `SHELL` para el índice `0x12`.

**Conclusión con alcance controlado:** el motivo inmediato del error está demostrado (índice `D0DF` vacío por no encontrar descriptor); la causa más probable en el emulador es la ausencia de la ventana SRAM real y/o de su contenido inicial de aplicaciones. No está demostrado todavía si la unidad física contiene `BIOS` en SRAM pregrabada, si se instala por otra rutina o si se requiere una imagen/estructura adicional.

## 6. Siguiente trabajo

1. Confirmar por continuidad/esquema la decodificación de direcciones de U16/U17, CS2, A0-A16 y su relación con la ventana física `0x40000-0x7FFFF`.
2. Obtener, si es posible, un volcado de ambas SRAM de una unidad real antes y después del Master Reset. Conservar cada imagen y su SHA-256; no asumir que SRAM vacía equivale a estado de fábrica.
3. Implementar una región SRAM separada y persistente en el emulador únicamente después de confirmar el mapa. Mantener EPROM como región de solo lectura.
4. Añadir una captura del banco/ventana física `0x40000-0x7FFFF` y de `D0BB-D0E2` durante el escaneo.
5. Repetir Master Reset con SRAM inicial vacía y con la imagen física recuperada, comparando si aparecen los descriptores de índices `0x11-0x13` y si se resuelve la búsqueda de `SHELL`/`BIOS`.

La ruta de contraseña `ACE135` sigue siendo un tema independiente: el bypass usado para ejecutar `0x2523` no valida la captura ni la aceptación de esa contraseña.
