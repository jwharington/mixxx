# Tempo + Swing + Beat Locations: Implementation Checklist (Laroche 2001)

This is a step-by-step, low-risk implementation sequence for adding a new beat analyzer module and a compact widget that displays resulting BPM + swing.

---

## 0) Scope and acceptance criteria

### Functional scope
- Estimate **constant tempo** BPM, **swing** (delay of quarter-beats 2 & 4), and **beat locations** from audio.
- Persist beats in Mixxx `Beats` object as usual.
- Persist swing estimate in `beats_sub_version` metadata.
- Expose swing to GUI via new control object `visual_swing`.
- Add a small legacy-skin widget showing `BPM` + `SWING`.

### Acceptance criteria
- Analyzer returns beat list + stable BPM on constant-tempo material.
- Swing estimate is available after analysis and after reload from DB.
- Widget updates on track load/unload and while analyzed metadata changes.
- No regressions for existing beat plugins (Queen Mary / SoundTouch legacy).

---

## 1) Algorithm spec to implement

### 1.1 Transient detection
- STFT:

\[
X(k,i)=\sum_{n=0}^{N-1} h[n]x[n+iH]e^{-j2\pi kn/N}
\]

- Compressed band energy:

\[
E_i = \sum_{k=k_{\min}}^{k_{\max}} C(|X(k,i)|), \quad C(z)=\sqrt{z}\ \text{(or }\log(z+\epsilon)\text{)}
\]

- Positive novelty:

\[
\Delta E_i = \max(0, E_i - E_{i-1})
\]

- Peak-pick \(\Delta E_i\) to produce transient times \(t_n\).

### 1.2 Tempo/swing/phase likelihood
- Parameters: tempo \(T\) BPM, period \(P=60/T\), swing \(s\in[0,0.4]\), phase \(\phi\in[0,P)\).
- Quarter-beat anchors:

\[
q=P/4,\ \ b_0=0,\ b_1=q(1+s),\ b_2=2q,\ b_3=q(3+s)
\]

- Wrapped Gaussian mixture:

\[
p_\tau(t|T,s,\phi)=\sum_{i=0}^{3}p_i\,G_P\left(t-(\phi+b_i);\sigma\right)
\]

Recommended weights from paper:

\[
[p_0,p_1,p_2,p_3]=[0.4,0.15,0.3,0.15],\quad \sigma\approx0.05q
\]

- Maximize log-likelihood:

\[
L(T,s,\phi)=\sum_n \log\left(p_\tau(t_n|T,s,\phi)+\epsilon\right)
\]

\[
(\hat T,\hat s,\hat\phi)=\arg\max L(T,s,\phi)
\]

- Beat locations:

\[
\hat b_k = \hat\phi + k\hat P,\quad \hat P=60/\hat T
\]

### 1.3 Practical search settings (initial)
- Tempo grid: 70–140 BPM, coarse then refine.
- Swing grid: 0–40% in 5% steps (or 10% coarse + refine).
- Phase grid: 32 bins over \([0,P)\).
- Speed-up: coarse (5–15s segment) then full-track local refinement.

---

## 2) File-by-file implementation order (exact sequence)

## Step 1 — Add plugin skeleton and compile integration
1. **Create** `src/analyzer/plugins/analyzerlarocheswingbeats.h`
2. **Create** `src/analyzer/plugins/analyzerlarocheswingbeats.cpp`
3. **Edit** `src/analyzer/analyzerbeats.cpp`
   - include new plugin header
   - add plugin in `availablePlugins()`
   - instantiate in plugin selection branch
4. **Edit** `CMakeLists.txt`
   - add `src/analyzer/plugins/analyzerlarocheswingbeats.cpp` to sources list

**Checkpoint:** builds cleanly with stub plugin that returns failure gracefully.

---

## Step 2 — Implement transient extraction + ML search
1. **Edit** `src/analyzer/plugins/analyzerlarocheswingbeats.cpp`
   - STFT/novelty/transient extraction
   - likelihood evaluator
   - coarse-to-fine search
   - beat synthesis
2. (Optional helper) **Create** internal static helpers in same `.cpp` first; split later only if needed.

**Checkpoint:** plugin finalizes and returns non-empty beat list on simple rhythmic material.

---

## Step 3 — Persist swing estimate in beat sub-version
1. **Edit** `src/analyzer/plugins/analyzerlarocheswingbeats.cpp`
   - include extra version fields for beat factory creation:
     - `vamp_plugin_id=<id>`
     - `swing_pct=<value>`
     - optional: `swing_conf=<value>`
2. **Edit** `src/analyzer/analyzerbeats.cpp` (if needed)
   - keep passing plugin metadata via `getExtraVersionInfo(...)`
3. **Edit** `src/track/beatfactory.h`
   - add parser declaration for sub-version fragments
4. **Edit** `src/track/beatfactory.cpp`
   - add utility parser for `key=value|key2=value2...`
   - return `QHash<QString, QString>`

**Checkpoint:** analyzed track has `beats_sub_version` containing swing fields.

---

## Step 4 — Expose swing to deck control layer
1. **Edit** `src/mixer/basetrackplayer.h`
   - add `std::unique_ptr<ControlObject> m_pVisualSwing;`
2. **Edit** `src/mixer/basetrackplayer.cpp`
   - create control: `ConfigKey(getGroup(), "visual_swing")`
   - on track load and metadata updates:
     - parse swing from `pTrack->getBeats()->getSubVersion()`
     - set `m_pVisualSwing`
   - on unload: reset swing control (e.g., 0 / invalid sentinel)
   - connect to relevant track signals (`beatsUpdated`, and load/unload path)

**Checkpoint:** `visual_swing` appears in ControlObject space and updates with loaded track.

---

## Step 5 — Add compact BPM+Swing display widget
1. **Create** `src/widget/wtemposwingdisplay.h`
2. **Create** `src/widget/wtemposwingdisplay.cpp`
   - suggested base: `WLabel` (display-only) or `WWidget`
   - use `PollingControlProxy` for:
     - `visual_bpm`
     - `visual_swing`
     - `track_loaded`
   - format examples:
     - `124.53 BPM  |  SW 18%`
     - fallback: `-- BPM  |  SW --`
3. **Edit** `CMakeLists.txt`
   - add `src/widget/wtemposwingdisplay.cpp`

**Checkpoint:** widget compiles and updates text when controls change.

---

## Step 6 — Wire widget into legacy skin parser
1. **Edit** `src/skin/legacy/legacyskinparser.h`
   - declare `QWidget* parseTempoSwingDisplay(const QDomElement& node);`
2. **Edit** `src/skin/legacy/legacyskinparser.cpp`
   - include `widget/wtemposwingdisplay.h`
   - add node dispatch for `<TempoSwingDisplay>`
   - implement parser function similar to `parseBpmEditor(...)`
3. **Edit** one pilot skin XML (minimal first), e.g.:
   - `skins/LateNight/decks/row_2_3_TitleArtistTime.xml`
   - or `skins/Swing/decks/row_text_left.xml` and right variant
   - add `<TempoSwingDisplay>` node with deck group binding

Example XML snippet:

```xml
<TempoSwingDisplay>
  <TooltipId>visual_bpm</TooltipId>
  <NumberOfDigits>2</NumberOfDigits>
  <BpmDecimals>2</BpmDecimals>
  <SwingDecimals>0</SwingDecimals>
  <NoTrackText>-- BPM | SW --%</NoTrackText>
  <Pos>10,10</Pos>
  <Size>180,20</Size>
  <Color>,$TextColor</Color>
  <BgColor>,$BgColor</BgColor>
  <Align>left</Align>
</TempoSwingDisplay>
```

**Checkpoint:** widget visible in selected skin and shows BPM+swing.

---

## Step 7 — Tests and verification
1. **Create** `src/test/analyzerlarocheswingbeats_test.cpp`
   - synthetic click tracks with known BPM/swing/phase
   - verify BPM/swing/beat error bounds
2. **Create or extend** beat factory parsing tests (new test file or existing track tests)
   - round-trip for sub-version parse/encode fields
3. **Manual verification**
   - analyze a straight track (expect swing near 0)
   - analyze swung track (expect non-zero swing)
   - load/unload deck track and observe widget reset/update

**Checkpoint:** tests pass and no analyzer regression.

---

## 3) Suggested PR slicing (safe incremental)

### PR 1: Scaffolding
- Add plugin files + CMake + registration in `AnalyzerBeats`.
- No behavior change for default plugin.

### PR 2: Algorithm MVP
- Implement transient + ML search + beat output.
- Persist `swing_pct` metadata.

### PR 3: Deck control plumbing
- Add `visual_swing` control and update on load/beats change.

### PR 4: Widget + parser
- Add `WTempoSwingDisplay`, parser support, and one pilot skin integration.

### PR 5: Tests + polish
- Unit tests, formatting/UX polish, docs/changelog note.

---

## 4) Definition of done
- [ ] New analyzer selectable from beat plugins.
- [ ] Beatgrid generated correctly for constant-tempo rhythmic tracks.
- [ ] Swing estimate persisted and reloadable from `beats_sub_version`.
- [ ] `visual_swing` control available and updated at runtime.
- [ ] Small BPM+Swing widget works in at least one shipped skin.
- [ ] New tests pass in CI; no existing test regressions.

---

## 5) Risks and mitigations
- **Risk:** False transients in legato/noisy tracks.
  - **Mitigation:** novelty threshold tuning + confidence score + fallback to existing plugin.
- **Risk:** Tempo octave errors (half/double).
  - **Mitigation:** candidate post-filtering and score comparison across octave-related BPMs.
- **Risk:** Skin compatibility.
  - **Mitigation:** introduce new widget tag only; existing skins unaffected unless they opt in.

---

## 6) Notes for implementation discipline
- Keep each PR buildable and testable.
- Do not weaken tests or assertions to pass CI.
- Keep plugin metadata keys stable once released (`swing_pct`, etc.).
