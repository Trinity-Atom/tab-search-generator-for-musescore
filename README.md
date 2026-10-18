# tab-search-generator-for-musescore

Buscador de tablaturas en **C++** con exportación a **MusicXML** para abrir y editar en **MuseScore**.

## Estado actual

Prototipo inicial por consola con:

- Búsqueda de canción (mock de proveedores)
- Listado de versiones
- Vista previa de acordes/letra
- Navegación para volver al listado o hacer nueva búsqueda
- Exportación a `.musicxml`

> Nota: por ahora los proveedores están simulados (`mockSearch`).

## Requisitos

- Compilador C++17 o superior (g++, clang++)

## Compilación

```bash
g++ -std=c++17 -O2 -Wall -Wextra -pedantic main.cpp -o tab_search
```

## Ejecución

```bash
./tab_search
```

## Uso rápido

1. Escribe el nombre de una canción
2. Elige una versión del listado
3. Previsualiza acordes/letra
4. Selecciona **Exportar a MusicXML**
5. Abre el archivo exportado en MuseScore

Los archivos se generan en la carpeta `exports/`.

## Próximos pasos sugeridos

- Integrar proveedores reales (LaCuerda/CifraClub/UltimateGuitar) con adaptadores por fuente
- Mejorar parsing de acordes complejos (`sus4`, `add9`, `dim`, etc.)
- Reemplazar notas de relleno por silencios o patrón configurable
- Añadir GUI (Qt/ImGui) para edición gráfica de acordes
