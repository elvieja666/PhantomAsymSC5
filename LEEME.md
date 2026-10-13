# Phantom Asym v1.6.0 (sidechain only)

Silencia el audio que entra, en full range, siguiendo los cycles de la señal que le mandes por el **sidechain input**. Interfaz estándar de JUCE.

## Controles

| Control | Qué hace |
|---|---|
| **Depth** | Máxima reducción del audio, que se alcanza cuando el sidechain llega a 0 dBFS (100 % = silence total) |
| **Depth Bypass** | Apaga el efecto (con un fade de 5 ms, sin clicks). El Output sigue aplicándose |
| **Mode** | **Half** (valor por defecto): silencia un solo semiciclo de cada cycle. **Whole**: silencia un cycle completo, deja pasar el siguiente, y así alternando |
| **Polarity** | De 0° (Positive) a 180° (Negative). Mueve dónde del waveform cae el silence: a 0° la ventana arranca en el cruce ascendente del sidechain, a 180° medio cycle después |
| **Key Input** | Ganancia en dB (-24 a +36) que se aplica a la señal del sidechain antes de detectarla. Sirve cuando el key llega bajo: al subirlo, el efecto reacciona más fuerte (recordá que la cantidad de efecto sigue el level del key) |
| **Key Listen** | Solo se escucha el key input (con el Key Input aplicado), en lugar del audio. Sirve para ajustar el level y ver qué detecta el plugin. Sin nada conectado al sidechain, queda en silencio. El Output no se aplica en este modo |
| **Output** | Volumen final (-12 a +6 dB) |

Depth, Polarity y Output tienen una caja numérica: hacé clic, escribí el valor exacto y apretá Enter. En Polarity podés escribir grados (por ejemplo `90` o `90°`) o las palabras `pos` y `neg`.

## Cómo se calcula la cantidad de efecto

La reducción **sigue el level del sidechain**: a 0 dBFS es igual a **Depth**, y a medida que el level baja (por ejemplo cuando el kick decae) el efecto baja de forma continua hasta cero. No hay threshold que lo corte de golpe.

- Dentro de cada cycle, el valor sale del **peak** de ese cycle del sidechain.
- La relación es lineal en amplitud: un sidechain con peak a -6 dBFS da la mitad del Depth, y a -12 dBFS un cuarto. Para más efecto, subí el **Key Input**.

## Cómo usarlo

1. Poné Phantom Asym en la pista (o el master) que querés afectar.
2. Mandá al **sidechain input** una señal con solo el **sub** (por ejemplo, un kick o un 808 filtrado). El plugin no tiene filtros internos: si le mandás medios y agudos, detecta cycles de más.
3. Sin señal en el sidechain, el audio pasa sin cambios.

### Logic Pro

- El sidechain se elige en el menú **Side Chain** del header de la ventana del plugin.
- Si no aparece: cerrá Logic, reemplazá `Phantom Asym.component`, y en Logic Pro > Settings > Plug-in Manager seleccioná Phantom Asym y tocá **Reset & Rescan Selection**.

## Notas

- Solo atenúa, nunca amplifica. No agrega latency ni filtros en el camino del audio.
- El sidechain se detecta en mono (promedio de L y R), y la misma bajada se aplica a todos los canales.
- Cuando el sidechain se apaga (entre golpes), el contador se reinicia y cada golpe arranca igual (el primer cycle se silencia).
- Los proyectos guardados con una versión anterior mantienen su Mode guardado. Los valores de Mode cambiaron de orden en la 1.5.0 (Half primero), así que revisalo la primera vez.
- Key Listen puede sonar fuerte si subís mucho el Key Input: bajá el volumen del monitor antes de activarlo.

## Compilar e instalar

1. Reemplazá los archivos de tu carpeta del proyecto por los de este zip (y borrá `Source/PluginEditor.cpp` y `Source/PluginEditor.h` si todavía están).
2. En GitHub Desktop: escribí un Summary, **Commit to main**, **Push origin**.
3. Esperá la compilación en **Actions**, descargá el artifact de Windows o Mac.
4. Mac: `.vst3` a `~/Library/Audio/Plug-Ins/VST3/` y `.component` a `~/Library/Audio/Plug-Ins/Components/`, después `xattr -cr` sobre cada uno.
   Windows: `.vst3` a `C:\Program Files\Common Files\VST3\`
