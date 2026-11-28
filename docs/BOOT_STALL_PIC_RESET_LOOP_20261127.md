
## Intervención 2026-11-28 (horaria, continuación): fingerprint sin cambios; grep de riesgo en otros drivers MAME inconcluso (presupuesto agotado)

Verificado al inicio (VONTAR, `/home/spc/projects/WRITER_INSTRUCTOR/writer-emu`):
`HEAD` en `b650abc` (`origin/fix/irq1-ra2-wakeup` al día, sin divergencia;
incluye ya el housekeeping de la intervención anterior: `tools/dis_driver.py`
commiteado en `e2ae945`, `.gitignore` actualizado en `b650abc`). Árbol de
trabajo limpio, nada pendiente de commit.

Huella de `z180asci.cpp` (ambas copias, `/home/spc/projects/mame-as2k-stable-arm64/...`
y `/mnt/sd/projects/mame-as2k-stable-arm64/...`): **idéntica** a la registrada
en las tres intervenciones anteriores -- 20420 bytes, mtime `2026-11-27 06:53`,
SHA256 `96b23fd1435b26da8af28aa390d48d579c899c7399f24ee6bf18fa51039c1cea`. El
grep de control `output_txa(0)` en el archivo solo encuentra la línea 462 ya
existente (`case STATE_BREAK: output_txa(0); break;` dentro de
`transmit_edge()`, citada en el Hallazgo 7) -- es la misma línea de siempre,
no el parche. **Samuel todavía no ha aplicado el parche del Hallazgo 7 ni ha
autorizado explícitamente una aplicación automática.** Conforme a lo indicado
por las intervenciones anteriores, esta ejecución **no reintentó `edit_block`**
sobre ese archivo.

Se revisó también el doc de Drive completo (secciones 1-48) buscando
cualquier nota manual de Samuel -- no se encontró ninguna.

Con el bloqueo principal sin movimiento posible, se usó el presupuesto de
Desktop Commander para avanzar el pendiente #3 de "Próximos pasos" (riesgo
de generalizar el parche como cambio de core: comprobar si otros drivers de
MAME que usan `Z180ASCI_CHANNEL` dependen del comportamiento actual de
`BRK_SEND`). El `grep -rn` sobre todo `src/mame/` en el árbol compartido
(`/home/spc/projects/mame-as2k-stable-arm64`) resultó **lento** (mismo
problema de "búsqueda global lenta" ya documentado en intervenciones previas
al buscar `z180asci.cpp` por `find`) y no devolvió resultados dentro del
presupuesto de 5 llamadas; el proceso (PID `1510310` en VONTAR) quedó
corriendo en segundo plano al agotarse las llamadas disponibles para esta
intervención. No se tocó ningún archivo de código, no se lanzó ningún build.

**Pendiente para la próxima intervención con presupuesto:**
1. Decisión explícita de Samuel sobre el parche del Hallazgo 7 (sigue siendo
   el único bloqueante real; ver intervenciones anteriores para las dos
   opciones).
2. Si no hay decisión todavía: antes de repetir el grep de riesgo, comprobar
   si el proceso PID `1510310` en VONTAR sigue vivo (`ps -p 1510310`) y
   matarlo si corresponde; repetir la búsqueda con alcance más acotado y
   rápido, por ejemplo `git grep -n "ASEXT_BRK_SEND\|STATE_BREAK" -- src/mame`
   (si el árbol es un repo git, suele ser mucho más rápido que `grep -r`
   sobre archivos no indexados) o limitar a subdirectorios concretos en vez
   de todo `src/mame/`.

Estado del bloqueo: **SIGUE ABIERTO**. Sin novedad sobre la causa raíz
(Hallazgo 7) ni sobre la decisión pendiente de Samuel; esta intervención
confirmó que nada cambió y dejó iniciado (sin resultado aún) el chequeo de
riesgo opcional del paso 3 de "Próximos pasos".
