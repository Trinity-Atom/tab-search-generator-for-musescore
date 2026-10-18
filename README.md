# tab-search-generator-for-musescore

Buscador de tablaturas en **C++** con exportación a **MusicXML** para abrir y editar en **MuseScore**.

## Estado actual

Aplicación de escritorio con **Qt Widgets** y versión por consola, con:

- Búsqueda de canciones en LaCuerda, CifraClub y UltimateGuitar
- Listado de versiones
- Vista previa de acordes/letra
- Navegación para volver al listado o hacer nueva búsqueda
- Exportación a `.musicxml`
- Editor gráfico de título, artista, capo y filas de acordes/letra
- Búsqueda y descarga en segundo plano, con avisos por proveedor

La búsqueda obtiene metadatos reales; el contenido público se descarga al
seleccionar una versión y se conserva durante esa búsqueda. Un fallo en una
fuente no impide consultar las demás. Los errores de red, bloqueos y cambios de
formato se muestran explícitamente, sin sustituirlos por resultados simulados.

## Requisitos

- Compilador C++17 o superior (MSVC, g++, clang++)
- CMake 3.20 o superior y acceso a Internet durante la primera configuración
- Qt 6.5 o superior para la GUI: módulos Widgets y Concurrent (Test para las pruebas)
- Linux/macOS: libcurl con HTTPS y sus cabeceras de desarrollo
- Windows: Windows SDK (se utiliza WinHTTP con la configuración de proxy del sistema)

CMake descarga versiones fijadas de nlohmann/json y libxml2 para interpretar JSON
y HTML. No se desactiva la validación de certificados TLS.

## Compilación

Instala un SDK de Qt 6 compatible con tu compilador y arquitectura
(por ejemplo, Qt 6.8 MSVC 2022 de 64 bits para MSVC en Windows).
Indica la carpeta del SDK que contiene `bin`, `lib` y `plugins`:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64"
cmake --build build --config Release
```

En Linux/macOS, usa la ruta de tu SDK Qt en `CMAKE_PREFIX_PATH` si CMake no
lo detecta automáticamente. Qt se instala por separado, no se descarga con CMake.
La GUI está activada por defecto; para compilar **solo la consola**, sin Qt:

```bash
cmake -S . -B build-console -DBUILD_GUI=OFF
cmake --build build-console --config Release
```

Si Qt ya está en la ruta de búsqueda de CMake:

```bash
cmake -S . -B build -DBUILD_GUI=ON
cmake --build build --config Release
```

## Ejecución

```bash
./build/tab_search_gui
```

En Windows con un generador de Visual Studio:
`.\build\Release\tab_search_gui.exe`. La compilación ejecuta `windeployqt`
para copiar las DLL y plugins necesarios junto al ejecutable. No muevas
solo el `.exe`: conserva también las DLL y las carpetas de plugins.
El equipo de destino necesita el runtime de Visual C++ correspondiente.

La consola sigue disponible como `./build/tab_search`, o
`.\build\Release\tab_search.exe` en Windows.

## Uso rápido

1. Escribe el nombre de una canción/artista y pulsa **Buscar** o Enter.
2. Selecciona una versión del listado para descargar su contenido público.
3. Edita título, artista, capo o las celdas de acordes/letra. Usa un acorde
   por fila; puedes añadir filas o seleccionar varias para eliminarlas.
4. Pulsa **Exportar MusicXML...** y elige dónde guardarlo.
5. Abre el archivo exportado en MuseScore.

La GUI permite elegir la ubicación; la consola genera archivos en `exports/`.
**Abrir fuente** abre la página original en el navegador.

Los cambios se conservan al alternar entre versiones durante la misma búsqueda.
Una nueva búsqueda o el cierre piden confirmación si hay cambios sin exportar.
No hay guardado de proyectos ni restauración automática tras cerrar.
Los acordes inválidos, el título vacío y la ausencia de acordes impiden exportar.
La notación DO/RE/MI/FA/SOL/LA/SI se normaliza al exportar.

Las operaciones de red no bloquean el hilo gráfico. Mientras están en curso
se deshabilitan la edición, nuevas búsquedas y el cierre, hasta finalizar o
agotar los tiempos de espera del transporte. No hay cancelación de peticiones.
Si falla la carga, el mensaje muestra el motivo y un doble clic en la versión
permite reintentar. Los avisos de otras fuentes no impiden editar resultados
válidos.

## Adaptadores y límites

- **LaCuerda:** consulta `busca.php?exp=...`, recoge enlaces de canciones en
  `i_main` y lee los acordes de `t_body/pre` (o `pre#tCode` cuando contiene el
  texto). El artista se obtiene de la ruta de la fuente. Una página de búsqueda
  incompleta se informa como error.
- **CifraClub:** consulta el buscador público `solr.sscdn.co/cc/h2/`, descarta
  resultados de artistas y descarga `cifra_cnt/pre` desde la página de la canción.
- **UltimateGuitar:** interpreta el estado JSON `js-store/data-content` de la
  búsqueda y de la versión seleccionada. Solo incluye versiones públicas
  `Chords`; excluye Pro, contenido privado, tablaturas numéricas y Guitar Pro.

La interfaz `Provider` separa búsqueda y carga; `HttpClient` permite probar cada
fuente sin conexión. Las implementaciones están en [providers.cpp](providers.cpp)
y el transporte en [http.cpp](http.cpp). Se consulta la primera página de cada
buscador, sin prometer un catálogo exhaustivo de versiones.

Se conservan los acordes reconocidos de cada línea (incluidos varios acordes y
pasajes instrumentales), letra y capo cuando está disponible. El modelo sigue
siendo de acordes, no de tablatura nota a nota; los ritmos y la melodía no se
deducen de las páginas. La exportación conserva el patrón simplificado existente.
Las indicaciones de repetición como `(x2)` no se expanden en MusicXML.
Consola y GUI comparten el exportador de [musicxml.cpp](musicxml.cpp):
4/4, un acorde por negra y C4 de relleno, sin melodía ni letra en el archivo
exportado. La edición de letra es local a la vista previa.

## Acordes compatibles

Proveedores, editor y exportador usan el mismo parser de [chords.cpp](chords.cpp).
La raíz, el bajo y los grados añadidos/alterados se exportan con semántica
MusicXML, sin sustituir sufijos desconocidos por acordes mayores:

| Familia | Ejemplos |
| --- | --- |
| Mayores, menores y quintas | `C`, `Am`, `C5` |
| Sextas y séptimas | `C6`, `Cm6`, `C7`, `Cmaj7`, `Cm7`, `CmMaj7` |
| Suspendidos | `Csus2`, `Csus4`, `C7sus2`, `C7sus4` |
| Disminuidos y aumentados | `Bdim`, `Bdim7`, `Bm7b5`, `Caug`, `Caug7` |
| Extensiones | `C9`, `Cm9`, `Cmaj9`; también `11` y `13` en esas familias |
| Grados añadidos | `Cadd9`, `Cmadd9`, `Cadd11` |
| Alteraciones | `G7b9`, `C7(b9,#11)`, `C7#5` |
| Inversiones | `D/F#`, `C7(b9)/E`, `REm7/FA#` |

`Cadd9` conserva la tríada mayor y añade la novena sin séptima; `C9` incluye
la séptima dominante. Los grados que ya existen se alteran; los que faltan se
añaden. `Bm7b5` se representa como semidisminuido, no como un menor séptima sin
su quinta alterada. Los símbolos se conservan en la vista previa y el texto del
tipo de acorde se incluye en MusicXML para mostrar los modificadores sin
duplicarlos como grados impresos.

Se normalizan DO/RE/MI/FA/SOL/LA/SI (también en el bajo), `♯`/`♭`,
`°` → `dim`, `ø` → `m7b5`, `+` → `aug`, `min` → `m` y `M7` → `maj7`.
Usa un símbolo sin espacios por fila. Se rechazan grados desconocidos,
duplicados o contradictorios, paréntesis incompletos y bajos inválidos; la GUI
muestra el motivo y la fila. La exportación valida todos los acordes antes de
escribir, sin truncar un archivo existente cuando un acorde falla.

Las líneas de las fuentes admiten progresiones agrupadas `(C-G)` y conservan
los paréntesis internos de `C7(b9)`. Si una línea inequívocamente armónica
contiene un símbolo de acorde no compatible, la carga informa del error.
No se admiten todavía omisiones `no3`/`omit5`, dobles alteraciones, poliacordes,
microtonos ni la sintaxis `6/9` (la barra se reserva al bajo).
Esto mejora los símbolos armónicos, no genera melodía, ritmo ni digitaciones.

Estas fuentes no tienen un contrato de API estable: pueden cambiar su HTML/JSON
o devolver HTTP 403/429/5xx según la ubicación y las restricciones del sitio.
No se eluden CAPTCHA, autenticación ni muros de pago, y no se hacen reintentos
automáticos. Las solicitudes tienen tiempos de espera, un máximo de cinco
redirecciones HTTPS a dominios permitidos y un límite de 4 MiB por respuesta.
Usa únicamente contenido que tengas permiso para utilizar y respeta las
condiciones de cada proveedor. El enlace original se muestra y se incluye en
MusicXML como atribución.

## Pruebas

```bash
ctest --test-dir build -C Release --output-on-failure
```

Las pruebas usan contenido sintético, sin red ni letras de terceros. Cubren
adaptadores, codificación de consultas, carga diferida, fallos parciales,
validación de URLs, acordes múltiples, capo y exportación MusicXML.
La suite `chords` comprueba normalización, errores y la estructura XML de tipos,
grados e inversiones, incluida la diferencia entre `Cadd9` y `C9`.
Para generar un ejemplo aislado de `Fmaj7b5`, `Bbmaj7` y `D7` y comprobarlo
visualmente en MuseScore:

```powershell
.\build\Release\chord_tests.exe --export-example prueba-acordes.musicxml
```

Este ejemplo contiene los símbolos armónicos sobre notas C4 de relleno, no las
notas que forman cada acorde.
Con `BUILD_GUI=ON`, Qt Test también comprueba la búsqueda/carga asíncronas,
los errores parciales, la edición y conservación de cambios entre versiones,
la validación, el guardado en rutas Unicode y las confirmaciones de descarte.
Las pruebas de GUI usan la plataforma `offscreen` de Qt y contenido sintético.
Para un diagnóstico manual con red (su resultado depende del sitio):
`./build/provider_tests --live`, o `.\build\Release\provider_tests.exe --live`
en Windows con Visual Studio. Este diagnóstico muestra recuentos y errores,
no el contenido de las canciones. También puede comprobarse una URL pública
conocida sin pasar por el buscador: `provider_tests --live LaCuerda URL`.

## Próximos pasos sugeridos

- Ampliar acordes con omisiones y notaciones adicionales
- Reemplazar notas de relleno por silencios o patrón configurable
- Añadir guardado de proyectos y vista previa de partitura
