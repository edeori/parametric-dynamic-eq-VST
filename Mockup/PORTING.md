# Porting this mockup to the real JUCE editor

`paradyneq-ui.html` / `paradyneq-ui.js` are meant to be a 1:1 reference for
`Source/PluginEditor.cpp` / `Source/SpectrumAnalyzer.cpp` / `Source/BandEditor.cpp` --
every position and size here is a ratio of the same design box, so the numbers below
translate directly into JUCE layout code instead of being re-derived from scratch.

## Design box

`1180 x 715` px -- measured directly off `Mockup/mockup+components.png`'s own main-preview
region. This is the same "design resolution + scale everything by ratio" idiom already used
in `AdaptiveGate-VST/Source/UI/PedalViewComponent.cpp` (`designWidth`/`designHeight` +
a `bounds(x,y,w,h)` lambda that multiplies by `sx`/`sy`) -- reuse that exact pattern.

## Vertical rhythm (content area, 637px = 715 - 78 header)

Expressed as `fr` grid weights in the CSS (`grid-template-rows: 3fr 339fr 8fr 34fr 6fr 192fr 55fr`)
so they scale proportionally with the plugin window -- the same seven numbers are the seven
`removeFromTop()` heights to use in `resized()`, scaled by `sy`:

| Row | px (of 637) | Content |
|---|---:|---|
| gap | 3 | -- |
| spectrum panel | 339 | `SpectrumAnalyzer` |
| gap | 8 | -- |
| band tabs | 34 | band strip |
| gap | 6 | -- |
| band editor | 192 | `BandEditor` |
| bottom margin | 55 | -- |

## Horizontal insets

- Header/content side padding: `2.46%` of 1180 (~29px) on both sides.
- Spectrum graph's own inset from the panel edge (for axis labels): left 34px, right 46px,
  top 12px, bottom 20px of the panel's own box -- see `GRAPH_LEFT/RIGHT/TOP/BOTTOM` in
  `paradyneq-ui.js`, same idea as `SpectrumAnalyzer::graphBounds()`'s `removeFromLeft/Right/Top/
  Bottom` calls, just with slightly different numbers tuned for this mockup's own grid.
- Band editor's two-column split: `43.6fr / 4fr(divider) / 52.4fr` -- wider than the real
  editor's current full-width-stacked-rows layout (`BandEditor::resized()`); porting this
  layout back would mean rebuilding that method around the same two-column split instead.

## Curve/coordinate math -- copy directly, don't re-derive

`paradyneq-ui.js`'s `freqToX`/`xToFreq`/`dbToY`/`gainToY` and `bandMagnitudeDb()` are already
line-for-line ports of `SpectrumAnalyzer::frequencyToX/xToFrequency/spectrumDbToY/gainToY` and
`::bandMagnitudeResponseDb()` (including the shelf-slope-from-Q fix documented there). If the
JS and C++ ever drift, treat the C++ as authoritative and re-port, not the other way round.

## Deliberate mockup-only simplifications (not to carry over as "the design")

- **Non-frequency knobs use linear 0-100% scaling** (moth-ui.js's generic range-input
  handling) instead of the real plugin's skewed `NormalisableRange` (e.g. Q/Ratio/Attack/
  Release use skew factors 0.4/0.45/0.35/0.35 in `PluginProcessor::createParameterLayout`).
  Only the Freq knob gets a custom log-scale mapping here (`freqToKnobPosition`/
  `knobPositionToFreq`) because it's the one where linear-vs-log is perceptually obvious even
  in a static mockup click-through. Porting these back into JUCE should restore the real skew
  curves, not keep the mockup's plain-linear ones.
- **The "spectrum trace" is fixed fake pink-noise-ish data** (`makeFakeSpectrum()`), not live
  audio -- there is nothing to port here, the real plugin already has `SpectrumFifo`/FFT.
- **Max 8 bands** in the mockup (`MAX_BANDS`) vs. the real plugin's 100-slot ceiling -- purely
  to keep the demo model small; not a constraint to carry over.
- **Solo/Mute/Dynamic checkboxes are mockup-local state only** (no audio, no engine) -- the
  real plugin's actual solo/mute/dynamic-listen behaviour (`PluginProcessor.cpp`,
  `EQBand::process`'s `soloListen` branch) is unrelated code, already built and tested;
  this mockup only needs to *look* right when those are toggled, not *behave* like the DSP.

## Assets

- `Assets/paradyneq-background.png` -- background_v2.png as supplied, copied verbatim (the
  `--moth-background` URL in the HTML is intentionally relative to `moth-ui.css`'s own
  location, not this HTML file's -- see the comment right above that declaration; get this
  wrong and the background silently fails to load with no console-visible error in most quick
  glances).
- Knob face: `MothProduction-Shared-VST/UI/Components/Knobs/SplitFire-knob.png`, reused as-is
  (verified pixel-exact centred circle inscribed in its own square canvas -- a plain
  `border-radius:50%; overflow:hidden` wrapper reproduces it with no custom crop needed).
- Logo: `MothProduction-Shared-VST/UI/Logo/moth_logo.png`, reused as-is.
- Fonts: Oxanium/Barlow Condensed/Rajdhani via `moth-ui.css`'s own `@font-face` rules --
  matches `Source/Fonts` conventions used by MultiSqueeze/StringLife/PhaseLock already, if a
  bespoke `ParaDynFonts.h/.cpp` gets written later, copy the four-function pattern from one of
  those, don't hand-roll a new one (see the reskin skill's own guidance on this).
