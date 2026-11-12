# Practice-track emphasis: design and feasibility

Status: design only. No playback, UI, or file-format code is included in this change.

This note is about Jon's fork of MuseScore Studio (`popatr/MuseScore`, `main` at `7b2d005464`). The tree reports itself as **5.0.0-dev** (`version.cmake`) and uses the playback architecture introduced with MuseScore 4. "MuseScore 4" below means that architecture. The pinned framework submodule is `muse_framework` `e40d9c9dc6b31733f53f9dc2ae1ca06a63bbf310` (the `muse/` directory was empty in this workspace; the framework files cited here were read from that commit).

## Recommendation

Start with a **playback-only per-note emphasis multiplier**, not with a restored per-voice mixer fader.

A per-voice fader is real and relatively small, and it is worth having. It cannot do the thing this feature is for. In closed-score divisi, Soprano 1 and Soprano 2 are often two noteheads on **one stem in voice 1**. They share a MuseScore voice, a playback layer, and (on Muse Sounds) a dynamics track. Turning voice 1 up turns both of them up.

The data that should be saved is "which parts claim this note," and a note is loud when **any** selected part claims it. That union rule falls out of the data model. It should not be implemented as a single boolean that automatic tools write and erase.

A QML plugin cannot do this. Evidence is in [Plugin API](#plugin-api).

## What Jon assumed, and what the code does

| Assumption | Finding |
| --- | --- |
| MuseScore 3's mixer had per-voice **volume** (voice 1 stems up, voice 2 stems down). | MuseScore 3 had per-voice **mute**, and per-**channel** volume. Closed-score S/A and T/B staff text routed voices onto separate channels, and those channels had their own faders. The handbook states outright that individual voice volume cannot be adjusted. See [MuseScore 3](#musescore-3). |
| MuseScore 4 has only per-staff / per-instrument volume. | The mixer fader is per **instrument track** (`partId` + `instrumentId`), which for a choir staff is the whole part. Dynamics and expression are already per staff **and** voice, inside one mixer strip. See [Pipeline](#playback-pipeline). |
| Emphasis must not change notation (no velocity or dynamic edits). | Correct, and necessary. `Note::userVelocity` is a visible, undoable, exported property, and a non-zero value **replaces** dynamics on the soundfont path. See [Why not velocity](#why-not-velocity). |
| A traditional plugin cannot do it. | Confirmed. The plugin API can write notation properties and a dead MIDI channel volume. It cannot reach the mixer, the playback model, or audio export. |

## Playback pipeline

One audio track is one instrument, not one voice and not one note. Everything finer than that is encoded inside the event stream that track plays.

### From notes to events

`PlaybackModel` (`src/engraving/playback/playbackmodel.h`) owns one `muse::mpe::PlaybackData` per `InstrumentTrackId`.

```cpp
struct InstrumentTrackId {
    muse::ID partId = 0;
    String instrumentId;
};
```

`makeInstrumentTrackId` (`src/engraving/dom/utils.cpp`) builds that key from `item->part()->id()` and `part->instrumentId(tick)`. A piano's two staves share a part and therefore one track. A "Women" choir part is one track even though it contains soprano and alto. A second instrument on the same part (instrument change) is a second track. Chord symbols are yet another track (`chordSymbolsTrackId`).

`PlaybackModel::processSegment` walks chord-rests and calls `PlaybackEventsRenderer::render`. `NoteRenderer::render` (`src/engraving/playback/renderers/noterenderer.cpp`) builds a `muse::mpe::NoteEvent` through `buildNoteEvent` in `renderingcontext.h`. Each event carries:

- pitch, timing, tempo, articulations
- `voiceLayerIndex` = `note->voice()` (0–3)
- `staffLayerIndex` = `note->staffIdx()`
- `nominalDynamicLevel` from `PlaybackContext::appliableDynamicLevel(chord->track(), utick)`
- `userVelocityFraction` = `note->userVelocity() / 127`

`track_idx_t` is `staffIdx * 4 + voice` (`staff2track` / `track2voice` in `src/engraving/dom/mscore.h`). The framework's `makeLayerIdx(staff, voice)` is the same formula (`framework/mpe/mpetypes.h`, `MAX_VOICES = 4`). Voice identity survives all the way to the synthesizer. The mixer never sees it.

`PlaybackModel::sendEvents` pushes two things downstream:

- `originEvents` — the note events
- `dynamics` — `PlaybackContext::dynamicLevelLayers`, a curve per `trackIdx`

### Where loudness is computed

Score dynamics are not MIDI velocity anymore.

1. Dynamics and hairpins are compiled into automation curves (`src/engraving/automation/automationtypes.h`). `AutomationCurveKey::staff` is scoped to a staff id plus an optional voice. `PlaybackContext::dynamicsCurve` reads the voice-specific curve and falls back to the staff-wide curve. This is the MuseScore 4.4 "apply to voice" work (upstream issue [19137](https://github.com/musescore/MuseScore/issues/19137)).
2. `nominalDynamicLevel` on the note event is a sample of that curve, in the framework's dynamic-level units (`dynamicLevelFromType` in `framework/mpe/mpetypes.h`).
3. `NoteEvent`'s constructor turns articulations plus that level into an expression curve (`framework/mpe/events.h`, `calculateExpressionCurve`). A non-zero `requiredVelocityFraction` is stored as `expressionCtx.velocityOverride` and does not scale the curve. It replaces later velocity calculations.
4. Instrument **volume, pan, mute, and solo** live outside the score, in `audiosettings.json` inside the mscz. `ProjectAudioSettings` (`src/project/internal/projectaudiosettings.h`) maps them by `InstrumentTrackId`. `PlaybackController::doAddTrack` (`src/playback/internal/playbackcontroller.cpp`) creates one `IPlayback` track per id and attaches those output params. The mixer strip is `MixerChannelItem` (`src/playback/qml/MuseScore/Playback/mixerchannelitem.h`), built by `MixerPanelModel::buildInstrumentChannelItem`. Its fader is track gain in dB, applied by `AutomationControlNode` after synthesis. `AutomationType::Volume` and `Pan` are instrument-scoped on purpose (`automationtypes.h`).

`InstrChannel::volume` (the old MIDI channel, 0–127) is still stored on the instrument and still written by MusicXML and MIDI export (`exportmidi.cpp` emits `CTRL_VOLUME`). The audio engine does not read it.

### Two synthesizers, two meanings of "louder"

Both synthesizers already split **voices** onto separate internal tracks. Neither can make one note of a chord louder than the other note without a further split, because those two notes share a voice layer.

**FluidSynth** (`framework/audio/.../fluidsynth/fluidsequencer.cpp`, `soundmapping.h`):

- `ChannelMap::resolveChannelForEvent` allocates a MIDI channel per `makeLayerIdx(staff, voice)` when single-note dynamics are on. The voice index is always part of the key. The staff index is collapsed to 0 when the soundfont has no single-note dynamics.
- Dynamics become CC11 (expression) **per channel**, resampled from that layer's curve (`addDynamicEvents`).
- Note-on velocity comes from `velocityOverride` when set. Otherwise, with single-note dynamics, it is the articulation shape (typically around half scale), and loudness is CC11. Without single-note dynamics, velocity is the dynamic level itself.
- Channel budget defaults to 16 (`m_maxChannels`). Extra layers compete with articulation channels (pizzicato, and so on).

**Muse Sampler** (`framework/musesampler/.../musesamplersequencer.cpp`), the Muse Sounds path:

- `findOrCreateTrack(makeLayerIdx(staff, voice))` creates one sampler track per layer.
- Main-stream notes are `ms_NoteEvent_5`. That struct has voice, time, duration, pitch, tempo, tuning, articulations, and notehead. It has **no dynamics field**.
- Loudness during playback is `addDynamicsEvent` on the **track**, from the same per-layer curve.
- Per-note `_dynamics` exists only on `ms_AuditionStartNoteEvent_5`, used when you click a note (`addAuditionNoteEvent`). There, `velocityOverride` replaces `nominalDynamicLevel`.

So:

- Scaling the existing per-voice dynamic curve changes live playback and export for both synths. That is per-voice volume.
- Two simultaneous notes on one voice share that curve. Per-note emphasis has to move one of them onto another layer (and give that layer its own scaled curve), or the sampler API has to grow a per-note dynamics field. The current pinned API has not grown one.

### Audio export reuses playback

`AbstractAudioWriter::doWriteAndWait` (`src/importexport/audioexport/internal/abstractaudiowriter.cpp`) waits until `PlaybackController` has loaded the same tracks used for live playback, calls `setNotation` and `setIsExportingAudio(true)`, then `IPlayback::saveSoundTrack`. MP3, WAV, FLAC, OGG, and AAC writers are thin subclasses. Video export calls the same path (`VideoWriter::startAudioExport`).

There is no second rendering pass. A gain that is in the event stream or the dynamic layers is in the exported file. A gain that exists only as a mixer widget state is also in the file, because export renders the live graph. A gain written into `userVelocity` is in the file too, and it is also in the notation.

Export is per part (`UnitType::PER_PART`), which is the instrument, not the voice. Upstream issue [27991](https://github.com/musescore/MuseScore/issues/27991) asks for voice-level practice tracks and is the same request as this project.

MIDI export is a different renderer (`src/importexport/midi`, plus `Note::customizeVelocity`). Emphasis will not appear in MIDI unless that path is taught about it. That is out of scope for a practice-track feature.

### Why not velocity

`Pid::USER_VELOCITY` is a normal note property (`src/engraving/dom/property.cpp`). It is written as `<velocity>` (`twrite.cpp`), shown in the Properties panel (`NotePlaybackModel` displays 0 as 64), and round-tripped through MusicXML as the `dynamics` attribute. Pre-4.0 velocity values are deliberately discarded on import (`read400/tread.cpp`: "MU4 user is not raw MIDI velocity").

On the Fluid path, a non-zero fraction becomes `velocityOverride` and `FluidSequencer::noteVelocity` returns immediately, ignoring the dynamic curve. On Muse Sounds click-to-play it replaces the audition dynamic. On Muse Sounds score playback it is ignored, because `ms_NoteEvent_5` never reads it.

`Note::play` (the Play checkbox) is also a notation property. Unchecking it is Marc Sabatella's documented mute workaround. It is saved, it is undoable, and it cannot express "quieter" or "this note belongs to S2 but also to S1."

## MuseScore 3

Sources: [Mixer, handbook 3](https://handbook.musescore.org/3/sound-and-playback/mixer), [Working with multiple voices](https://handbook.musescore.org/3/basics/working-with-multiple-voices), [forum thread 277238](https://musescore.org/en/node/277238), [forum thread 285997](https://musescore.org/en/node/285997).

MuseScore 3 was MIDI-channel based.

- Each instrument had one or more **channels** (normal, pizzicato, and the Soprano / Alto / Tenor / Bass channels on the choir templates). The mixer fader was CC7 on that channel.
- Each staff had four voices. The mixer could **mute** each voice (`Staff::playbackVoice`, still stored today as `playbackVoice1`–`playbackVoice4`). The handbook's limitation list says: "Cannot adjust individual Voice volume. Voices can be muted separately though."
- Hidden staff text `S/A` and `T/B` routed voices 1 and 3 to one channel and voices 2 and 4 to another. After that, the channel faders behaved like Soprano vs Alto volume. That is the memory of "per-voice volume." It still could not split two noteheads in one chord.

### What survived into this tree

`Staff::playbackVoice` is still a property, still written to mscx, and still exposed to plugins (`elements.h`, `Pid::PLAYBACK_VOICE1`–`4`). The only playback consult is commented out:

```cpp
// src/engraving/compat/midi/event.cpp
bool NPlayEvent::isMuted() const
{
    //! TODO
    // return a->mute() || a->soloMute() || !staff->playbackVoice(n->voice());
    return false;
}
```

`Channel::setMute` in the plugin API is an empty function whose comment says the mixer no longer works via `Channel` (`src/engraving/api/v1/instrument.h`). `Channel::setVolume` still writes `InstrChannel`, which export uses and the audio engine does not.

Tantacrul, on [issue 14677](https://github.com/musescore/MuseScore/issues/14677) (23 Nov 2022): per-voice mute was a MIDI capability dropped for 4.0, to be reintroduced later as part of a larger design. Marc Sabatella (18 Jun 2025) on the same issue: MuseScore 3 did not have per-voice volume either; the Play property and velocity override are the current workarounds; mute state in MuseScore 3 was saved with the score. Jojo-Schmitz, on [issue 12827](https://github.com/musescore/MuseScore/issues/12827), described the closed-score case: separate mixer volume for Soprano and Alto so a singer hears their line louder and the others under it.

### How hard is a restoration?

Per-voice **mute** is small. The flag is already on `Staff`. `PlaybackEventsRenderer` / `NoteRenderer::shouldRender` can skip a voice, or that voice's dynamic layer can be forced to silence. Wire the four buttons into the existing mixer strip, persist them (they already persist on the staff), and live playback and export follow. Risk: the flag currently lives on the staff as a notation property, so a plugin or a properties panel can already toggle it with no audible effect. Decide whether mute stays a staff property (saved in the mscx, visible to other versions as an unknown-but-ignored behavior) or moves next to mixer state in `audiosettings.json`.

Per-voice **volume** is a step larger, and it does not need new note data. Multiply that voice's dynamic curve in `PlaybackContext::dynamicLevelLayers` (or a wrapper around it) by a gain stored beside the mixer state. Both synths already consume that curve. The fader is four extra controls on the instrument strip, not four new audio tracks. Hairpins keep their shape because the whole curve scales.

That restoration is a good fallback and a good test that export matches playback. It is not the divisi feature. Two notes in one chord have one `trackIdx` and one curve.

## Plugin API

Plugins are QML/JS objects rooted at `mu::engraving::apiv1::PluginAPI` (`src/engraving/api/v1/qmlpluginapi.h`), exposed as `MuseScore`. They can read and write engraving properties through the undo stack (`Score.startCmd` / `endCmd`). They cannot see `PlaybackModel`, `IPlayback`, or `ProjectAudioSettings`. A search of `src/engraving/api` finds no playback controller, no audio export, and no mixer output params.

What a plugin can touch, and why each one fails:

| API | What it actually does |
| --- | --- |
| `Note.userVelocity` (`elements.h`, `Pid::USER_VELOCITY`) | Writes `<velocity>` into the score. Shows up in Properties. On soundfonts, replaces dynamics. On Muse Sounds playback, does nothing to the note. |
| `Note.play` | Notation property. Mutes the note everywhere, including a singer's "real" performance export. |
| `Note.color` / `EngravingItem.color` | Notation color. Prints and exports to PDF. `EngravingItem::curColor` uses the item color when `isPrinting` is set, before any view-only override. |
| `Staff.playbackVoice1`–`4` | Saved, and ignored by playback (`NPlayEvent::isMuted` is stubbed). |
| `Channel.volume` | Writes `InstrChannel`. MIDI and MusicXML export read it. The mixer does not. |
| `Channel.mute` / `setMute` | Documented no-op since 4.0 (`instrument.h`). |
| `Note.playEvents`, `Score.createPlayEvents` | `score.h` marks `createPlayEvents` deprecated: "PlayEvents don't have a playback effect in MuseScore 4." The list is the old `NoteEvent` ornament expansion used by the compat MIDI renderer. |
| `Score.metaTag` / `setMetaTag` | Arbitrary strings do round-trip inside `<metaTag>` (`writer.cpp` writes every entry). A plugin could stash a JSON blob there. Nothing in playback reads it. The score properties UI can show meta tags. |
| `Score.newCursor`, selection, `startCmd` | Enough to walk notes and set properties. Not enough to install a score-view mode. A dock plugin can react to the current selection and mutate properties. That mutation is the notation. |

There is no plugin entry point that runs between `PlaybackEventsRenderer` and the synth, and no entry point that wraps `saveSoundTrack`. A plugin that wants a practice MP3 today has to bake velocity or Play flags into the score, export, and undo. That violates the "don't touch the notation" requirement, and on Muse Sounds the velocity bake is inaudible during score playback.

Conclusion: this has to be a built-in change on the fork.

## Emphasis architecture

### Where the data lives

Keep emphasis out of note properties and out of the dynamic automation curves. Those are notation, or they are the audible result of notation. Other MuseScore versions, MusicXML, and PDF should keep seeing the original score.

Recommended store: a sidecar `practice-emphasis.json` inside the mscz, written next to `audiosettings.json` and `automation.json`.

- `MscWriter` already adds named JSON members and lists them in `META-INF/container.xml` (`src/engraving/infrastructure/mscwriter.cpp`). `MscSaver::writeMscz` is the save hook to mirror (`src/engraving/rw/mscsaver.cpp`).
- `XmlReader::unknown` skips unrecognized mscx tags (`src/engraving/rw/xmlreader.cpp`). A sidecar never enters the score XML, so a stock reader does not have to skip it.
- Stock MuseScore rewrites the zip from the files it knows. The next save in an official build **drops** the sidecar. That is the right failure mode: the notation is untouched, the practice data is gone, nothing looks edited. Document it in the file's own `generator` field.
- Do not hide this in `Score::metaTag`. Meta tags round-trip through official builds, which is attractive, and they also surface in score properties and travel into parts via `MscSaver`'s tag merge. A multi-megabyte choir's note map does not belong there.

Key notes by `EID` (`src/engraving/infrastructure/eid.h`). EIDs are written on elements (`twrite.cpp`) and restored on read (`read500/tread.cpp`). They survive save and reload. They do not automatically survive copy, paste, or explode-into-parts unless those commands are taught to copy membership. Phase 2 should include copy/paste of membership for notes, and should accept that a pasted note with a new EID starts untagged.

Schema sketch:

```json
{
  "version": 1,
  "generator": "popatr-practice-emphasis",
  "parts": [
    { "id": "S1", "name": "1st soprano", "staffId": "<eid of staff>", "voice": 0 }
  ],
  "membership": {
    "<note eid>": ["S1", "S2"]
  },
  "manual": {
    "<note eid>": "forceOn"
  },
  "profiles": [
    {
      "id": "s2-practice",
      "name": "S2 practice track",
      "selectedParts": ["S2"],
      "includeManual": true,
      "emphasizedGainDb": 6.0,
      "otherGainDb": -12.0,
      "otherMode": "attenuate",
      "ranges": [{ "fromMeasure": 1, "toMeasure": 16 }]
    }
  ],
  "activeProfileId": "s2-practice"
}
```

`otherMode` is `attenuate` or `mute`. Gains are decibels relative to the written dynamic, clamped to the synth's dynamic range. Defaults live in preferences so a new profile is not a blank form: emphasized +6 dB, others −12 dB is a starting point a singer can change.

`staffId` on a part definition is the staff's `EID`, not a staff index. Staff index shifts when a staff is inserted.

### How a note becomes loud

Membership is a set of part ids. A profile selects a set of parts. A note is emphasized when the intersection is non-empty, or when `manual` is `forceOn`. `forceOff` wins over membership inside that profile so a wrong automatic tag can be corrected without deleting the tag (the tag may still be true for a different profile).

```
claimed(note) = membership[note.eid]
emphasized(note, profile) =
    manual[note.eid] == forceOn
    || (manual[note.eid] != forceOff
        && profile.selectedParts ∩ claimed(note) ≠ ∅)
```

Deselecting S2 removes S2 from `selectedParts`. A note tagged `{S1, S2}` stays emphasized while S1 remains selected. No code path clears a tag because a different part was toggled.

Apply the decision in one place, after events exist and before `PlaybackData.mainStream.send`:

1. Decide emphasized vs other for each note event (the event can carry the note EID; today it does not, so the renderer pass should keep the `Note*` long enough to decide, or stamp a layer in `NoteRenderer::render`).
2. Per-voice-only gain (phase 1) scales that voice's existing dynamic layer. No event rewrite.
3. Per-note gain (phase 2) rewrites `staffLayerIndex` of emphasized notes by a fixed offset (`EMPHASIS_LAYER_OFFSET`, well above any real staff index; `staff_layer_idx_t` is 16-bit). `voiceLayerIndex` stays 0–3 so Muse Sampler's `_voice` field stays in range.
4. Duplicate each source dynamic layer onto the offset layer, scaled by `emphasizedGainDb`. Scale the original layer by `otherGainDb` (or replace it with silence when `otherMode` is `mute`).
5. A note is sent to exactly one layer. Shared notes that are emphasized are not also sent on the quiet layer, or the singer hears them twice.

`PlaybackModel::update` already rebuilds events for a tick range when the score changes. Emphasis edits should call that same update for the affected tracks, so playback and the next export match. Range playback, the metronome, and chord-symbol tracks are left alone.

Off-stream playback (clicking a note) goes through `PlaybackModel::triggerEventsForItems`. It should use the same gain so a click in emphasis mode matches score playback.

### Why a companion layer

| Backend | Same-voice simultaneous notes | What a gain on the existing curve does | What a companion layer does |
| --- | --- | --- | --- |
| Fluid, single-note dynamics | Share a MIDI channel and CC11 | Changes the whole voice | Separate channel, separate CC11. Note-on velocity stays the articulation velocity. |
| Fluid, no single-note dynamics | Staff index is ignored; voice still selects the channel | Changes note-on velocity, because velocity *is* the dynamic | Staff offset is ignored. Scale `nominalDynamicLevel` on the event instead. Voice offset would collide with the 0–3 voice field. |
| Muse Sounds | Share a sampler track and its dynamics events. `ms_NoteEvent_5` has no per-note dynamics | Changes the whole voice | `findOrCreateTrack` allocates another track. Dynamics events on that track are independent. `_voice` stays 0–3. |

The companion layer is the one mechanism that makes same-voice divisi audible on Muse Sounds. It has costs, listed under risks in phase 2. Soundfont-only velocity scaling is a trap: it sounds right on MS Basic and silent on Muse Sounds, which is the playback most choir users will have turned on.

### Profiles

A profile is a named selection plus gains plus an optional measure range. The score stores many profiles. One is active, and it is what playback and the next audio export use.

"Export practice tracks" walks the selected profiles, sets each one active, and calls the existing `saveSoundTrack` path once per profile into `ScoreName - S2 practice.mp3`. The export dialog gains a section; it does not grow a parallel renderer. Measure ranges are applied by treating notes outside the range as "other" (or by exporting only that span — prefer attenuating outside the range so the singer still hears context, and make that a checkbox).

Switching profiles must be cheap: re-render events, do not rebuild synth tracks unless the set of layers changed.

## Part model and automatic rules

A **part tag** is not a MuseScore part and not a voice. It is a label the user cares about (`S1`, `S2`, `A1`, …). Tags are assigned to notes. Profiles select tags.

Built-in tag sets, chosen in the auto-emphasis dialog because a score can be either layout:

1. **Closed SATB.** Upper staff voice 1 = S, voice 2 = A. Lower staff voice 1 = T, voice 2 = B. Voices 3 and 4 follow the same pairing as MuseScore 3's S/A text (voice 3 with voice 1, voice 4 with voice 2) only when the user asks for that. Default off, because voice 3 on a choir staff is often a divisi, not a duplicate soprano.
2. **Divisi within a voice.** The user picks a staff, a voice, and two tags (S1 and S2). Only that voice is examined.

Both can be applied on the same score: closed SATB for the Alto line, and a divisi rule on soprano voice 1 for S1/S2.

### Divisi rule

Look at each chord in the chosen staff and voice, inside the measure range. Order sounding noteheads from high to low. Ignore rests. Tied notes are one musical note: tag the tie chain's audible start, and copy the tag onto the tied successors so the highlight and the sustained sound agree. Grace notes follow their parent chord's tags unless the user overrides them.

| Chord in that voice | S1 | S2 |
| --- | --- | --- |
| One notehead | member | member |
| Two noteheads | upper only | lower only |
| Three or more | upper only | all lower noteheads, and the dialog warns |

The one-notehead case is the union rule's reason for existing. Written unison is claimed by both parts. Selecting either part emphasizes it. Selecting both and then clearing S2 leaves it emphasized via S1.

Three-note chords are ambiguous (S1, a middle voice, S2, or a three-way split). The default above matches "S2 sings everything that is not the top" more often than it matches a true three-part split. The dialog says so and offers "top / middle / bottom as S1 / S2 / extra," creating a third tag the user can rename. Do not guess silently.

Unisons written as two noteheads a unison apart (same pitch, two stems, or two noteheads on one stem at the same pitch) are two notes. Tag by position in the chord, high to low. If they share a pitch, both tags still apply to two different EIDs, and both sound. That is correct: the file really does contain two notes.

### Closed-SATB rule

A single notehead in voice 1 of the upper staff is S only. It is not also A. Alto is voice 2. Applying the divisi rule on top of voice 1 then splits S into S1 and S2 as above. Applying "2nd soprano" does not look at voice 2.

### Worked examples

Staff "Women," voice 1 is soprano, voice 2 is alto. Divisi rule on voice 1 with tags S1 and S2. Closed rule has already tagged voice 2 as A.

**Measure 1.** Voice 1 is a single G4. Voice 2 is E4.

- G4 membership = `{S1, S2}`. E4 = `{A}`.
- Profile "S2 practice" selects `{S2}`. G4 is emphasized. E4 is other. The unison soprano is loud, alto is quiet.
- Profile "S1 practice" selects `{S1}`. The same G4 is emphasized. Same sound, different label.
- Profile "S1+S2" selects both. G4 stays emphasized. Clearing S2 from the selection leaves G4 emphasized because S1 still claims it.

**Measure 2.** Voice 1 splits to G4 over E4 (one stem, two noteheads). Voice 2 holds C4.

- G4 = `{S1}`, E4 = `{S2}`, C4 = `{A}`.
- "S2 practice" emphasizes only E4. G4 and C4 are quiet. S1's G4 is not emphasized, because S2 does not claim it.

**Measure 3.** The user had both S1 and S2 selected, then clicks the shared G4 from measure 1 while editing the S2 tag.

- Click removes S2 from that note only. Membership becomes `{S1}`.
- S2 practice no longer emphasizes it. S1 practice still does.
- The click did not write velocity and did not clear S1.

**Measure 4.** A note is tagged `{S2}` by the rule, and the singer wants it quiet anyway. Manual `forceOff` on that EID. Every profile that would have emphasized it from S2 stays quiet on that note. The S2 tag remains for other tools.

**Range.** Profile range is measures 1–2. Measure 3's notes are "other" for this export even when their tags match. A second profile without a range exports the whole piece.

### What the rule must not do

- It must not move notes between voices or flip stems.
- It must not create dynamics or hairpins.
- It must not treat a chord's top note as S1 when the user asked for the Alto voice.
- It must not remove a tag because another tag was deselected.

Re-running the rule replaces tags it owns (`origin: "rule:divisi:S1S2"` on each assignment) and leaves manual edits (`origin: "manual"`) in place. The dialog offers "replace manual edits in this range" as an explicit checkbox, default off.

## UI sketch

### Emphasis mode

A toolbar toggle, next to note-input, named "Practice emphasis." While it is on:

- The active profile's emphasized notes draw red. Use `PaintOptions::overrideItemColor`, already consulted from `EngravingItem::curColor` and installed by `NotationPainting::paintView`. Printing returns the real color before that hook (`engravingitem.cpp`), so PDF and parts stay black unless the user has actually colored a note.
- Red is the emphasized set (the union), not the tag currently being edited. The tag being edited gets a second cue, a colored ring or a voice-style tint, so shared notes are visibly "in S1 and in S2."
- Click a note to toggle its membership in the **current tag**, not a global boolean. The status bar shows the current tag ("Editing S2"). Modifier-click sets `forceOff` / clears it.
- Drag-select and the existing selection filter (by voice, by staff) plus "Apply to selection" cover the manual bulk case.
- Turning the mode off leaves the data and the active profile in place. Playback stays colored while a profile is active, so the user can see what the export will do. A view menu item hides the color without deactivating the profile.

The highlight is a paint option. It is not `Pid::COLOR`.

### Auto-emphasis dialog

- Layout: Closed SATB, or Divisi on a staff/voice.
- Tags: defaults S1/S2 or the SATB set, editable.
- Measure range, defaulting to the current selection or the whole score.
- Preview count: "42 notes S1 only, 40 notes S2 only, 80 shared."
- Buttons: Apply, Apply and make profile.

### Profile and export

- Profiles live in the same panel: name, selected tags, emphasized gain, other gain or mute, range, active checkbox.
- Export dialog, audio section: "Practice tracks" lists profiles with checkboxes and uses the existing per-file writer once each. The ordinary export with no profile checkbox exports the score as written, with the active profile still applied if one is active — the panel should say that, and offer "export written balance" which temporarily clears the active profile for that run.

### Mixer (phase 1 only)

On each instrument strip, a small "Voices" row: four mute buttons and four gains, stored with the mixer state. They scale that voice's dynamic layer. They ignore part tags. They are the MuseScore 3 fallback, labeled as voices, not as S1/S2.

## Phased plan

Sizes are relative to this codebase, not a schedule. "Small" means a localized playback change and a unit test. "Large" means a new interaction mode, file IO, and export UI.

### Phase 1 — Per-voice gain and mute

Scale each voice's existing dynamic layer. Reconnect `Staff::playbackVoice` (or a mixer-side mute that does the same job) so a voice can be silenced.

- Touches `PlaybackContext::dynamicLevelLayers` or a thin wrapper called from `PlaybackModel::update`, the mixer QML strip, and `ProjectAudioSettings` (or the staff property, if mute stays where MuseScore 3 put it).
- Audible in live playback and in MP3/WAV because both already read those layers.
- Does not distinguish two notes in one chord.

Risks: scaling a curve that is already at the top of the Muse Sampler map (`dynamicLevelRatio` clamps at fff) will not get much louder; the useful direction is attenuating everyone else. Hairpin shape must be preserved (multiply, do not replace with a constant). Excerpts and parts have their own staves; gains keyed by staff id must be remapped or scoped to the master score only. Recommend master score only for v1.

This phase is the right spike to prove export matches playback. It is the wrong place to stop if the goal is divisi.

### Phase 2 — Manual per-note emphasis

Sidecar file, membership map, one active profile with a single manual tag, red highlight, click to toggle that tag, companion-layer routing, gains.

- This is the smallest version that makes a divisi note louder than its partner without editing the score.
- Automatic rules and named profile libraries wait.

Risks, in order:

1. **Muse Sounds track split.** A new layer is a new sampler track (`IMuseSamplerTracks::addTrack`). Choir patches can sound like a second choir rather than the same section brought forward. Legato and shared-sample behavior will not cross the split. That may be acceptable for a practice track and surprising if the user forgets a profile is active during normal playback. The UI must show that a profile is active, and "export written balance" must bypass it.
2. **Double playback** if a note is routed to both layers. The renderer must be exclusive.
3. **Fluid channel budget** (16). A companion layer per voice that actually contains emphasized notes is fine on an SATB score. Creating companion layers for every voice in a large orchestral score up front can collide with articulation channels. Allocate a companion layer only for voices that have at least one emphasized note.
4. **Non-SND soundfonts ignore `staffLayerIndex`.** Those presets need the nominal-dynamic scale path, or emphasis is inaudible. Detect `PlaybackSetupData::supportsSingleNoteDynamics` and branch.
5. **EID stability** across copy, paste, and linked parts. Missed links mean a part's emphasis and the master's drift.
6. **Tied notes.** Emphasis on the start note has to cover the tie chain or the sustain drops back to the quiet layer mid-tie.
7. **Repeats and measure repeats.** Events are generated in expanded utick space. Membership is on the written note, so every repeat hears the same decision. That is what we want. Jumps that share a note via partial ties need the same treatment `NoteRenderer` already gives ties.

Tests belong in `src/engraving/tests/playback/playbackmodel_tests.cpp`: a two-note chord, one emphasized, produces two layer indexes and two dynamic curves whose levels differ by the configured gain. A shared tag survives removing one of the two parts from the profile. No GUI required for that test.

### Phase 3 — Rules, profiles, batch export

The divisi and closed-SATB assignment passes, the preview counts, named profiles, measure ranges, and the export-dialog loop. The playback hook does not change. The risky work is the rule's treatment of three-note chords, cue notes, and lyrics-driven divisi (some editors mark S2 only with lyrics). Lyrics are a hint in the preview, not an automatic tag, until we have seen real scores. Octave-transposed tenor staves are still "top note is S1" on that staff; transposition is a playback concern the rule should ignore.

Risks: wrong tags on real choir engraving (voice 3 used as S2, divisi written as two voices rather than a chord, S2 on its own staff). The preview counts and the red highlight are what make a bad guess obvious. Re-run must not destroy manual edits.

### Where to start

Build phase 2's routing behind phase 1's gain knob.

1. Land per-voice curve scaling with a unit test and a hidden or minimal mixer control. Confirm on MS Basic and, if a Muse Sampler library is available, on Muse Sounds, that export matches playback. This is the pipeline proof.
2. Immediately add companion-layer routing for a hard-coded "these EIDs are emphasized" set, still with a unit test, before any UI. That is the divisi proof. If Muse Sounds sounds unacceptable with two tracks, stop and reconsider before building a mode. The fallback is then phase 1 plus the Play-property workaround, and the design should say so in the PR that hits the problem.
3. Only after that sound is acceptable: sidecar IO, emphasis mode, click-to-toggle, then rules and profiles.

Start with the playback spike, then the layer-split proof, then the UI. The mixer strip and the automatic S1/S2 dialog are the last pieces, because both of them are easy to build on top of a routing path that has already been heard, and both of them hide a bad routing path behind a finished-looking screen.

## Build and test notes (Linux)

How a later implementation pass would build this tree:

- The documented entry point is `cmake -P build.cmake` from the repository root (`README.md`). A debug build is `cmake -P build.cmake -DCMAKE_BUILD_TYPE=Debug`.
- The `muse` submodule must be populated. `.gitmodules` pins `muse_framework` at `e40d9c9dc6b31733f53f9dc2ae1ca06a63bbf310`. A checkout without `--recurse-submodules` leaves `muse/` empty and CMake cannot find the framework (`MUSE_FRAMEWORK_PATH`).
- `CMakeLists.txt` requires GCC 14 or Clang 20 (`check_compile_version`). C++20.
- Playback behavior for this feature belongs in `src/engraving/tests/playback/playbackmodel_tests.cpp`, which can run without opening the GUI. The cases that matter before any UI exists are: per-voice curve scaling changes `dynamicLevelLayers` and leaves notation properties untouched; a two-note chord with one emphasized note emits two layer indexes and two curves; a note claimed by both S1 and S2 stays emphasized after S2 is removed from the profile.
- Audible checks, once the app builds: MS Basic (Fluid, single-note dynamics on and off) and, if a Muse Sampler library is installed, Muse Sounds. Export the same passage to WAV and compare it with live playback. A profile that is not active must export the written balance.

What this design pass actually verified:

- The application sources under `src/` on `main` at `7b2d005464`, by reading them.
- The pinned framework commit, by checking out `framework/mpe`, `framework/audio`, and `framework/musesampler` from `e40d9c9` and reading the sequencer, channel map, and note-event structs. In that commit, `ms_NoteEvent_5` has no dynamics field; `ms_AuditionStartNoteEvent_5` does; `addDynamicsEvent` is per sampler track.

What this pass did not verify, because `muse/` was empty in the workspace and a full Studio build was not attempted:

- Compiling or running MuseScore, or the playback unit tests.
- How a second Muse Sampler track actually sounds on a choir preset (same section, or a doubled choir; legato across the split).
- Whether a realistic SATB-plus-piano score exhausts Fluid's 16-channel budget once companion layers exist.
- Whether the current Muse Sounds library, which is not in this repository, has changed the sampler API past the pinned headers. The headers the app compiles against are the ones cited above.

## Upstream considerations

Demand is already on the MuseScore tracker, and the maintainers have been explicit about the shape they will not accept.

- [Issue 14677](https://github.com/musescore/MuseScore/issues/14677). Tantacrul (2022-11-23) said the MIDI per-voice mute was dropped for 4.0 and would be reintroduced as part of a larger playback design, not restored as the old channel model. Marc Sabatella (2025-06-18) pointed out that MuseScore 3 never had per-voice volume, and that the Play property and velocity override are the available workarounds. A patch that only flips `NPlayEvent::isMuted` back on, or that writes `InstrChannel::volume`, repeats the thing they removed.
- [Issue 27991](https://github.com/musescore/MuseScore/issues/27991) is the choir practice-track request: export one voice out of a combined staff. [Issue 12827](https://github.com/musescore/MuseScore/issues/12827) asked for separate S/A mixer volume. [Issue 19137](https://github.com/musescore/MuseScore/issues/19137) (per-voice dynamics) shipped in 4.4 and is the model to extend: voice is already a dimension of the dynamic curve.
- A playback-only overlay is the part most likely to be discussable upstream. It does not change notation, printing, MusicXML, or MIDI. It composes with the dynamic curves that already exist. Per-voice gain is a small step on an accepted model.
- Two parts are unlikely to land unchanged. The companion-layer split depends on Muse Sounds behavior the app does not control, and a fork-only sidecar file is an explicit compatibility choice (stock MuseScore drops unknown zip members on the next save). An upstream version would want a file-format discussion, probably a versioned block that official readers preserve even when they ignore it, and a decision about whether MIDI export should learn the same gain. This design leaves MIDI out on purpose.
- Expanding the QML plugin API is not a route to the feature. Plugins cannot see `PlaybackModel` or `IPlayback`, and `Score.createPlayEvents` is documented as having no playback effect since MuseScore 4. Teaching plugins to write `userVelocity` would bake the practice balance into the score.

## Findings

- Loudness in this tree is a per-voice dynamic curve plus a per-instrument mixer fader. Audio export renders that same graph.
- MuseScore 3's "per-voice volume" was per-voice mute plus per-channel faders after S/A and T/B routing. Restoring a real per-voice gain is small and does not split a divisi chord.
- Muse Sounds score playback has no per-note dynamics field. Per-note emphasis means a second layer with its own curve, with a separate path for soundfonts that ignore the staff index.
- A plugin cannot reach any of those paths. Saving the emphasis as note velocity or note color would change the notation.
- The emphasis data should be a sidecar membership map, keyed by element id, interpreted as a union of part tags. Profiles are named selections of those tags plus two gains.

Start with the per-voice curve scale as a unit-tested spike, then the companion-layer split, and stop to listen before building emphasis mode. If the second Muse Sounds track is not acceptable, the honest fallback is per-voice gain plus the existing Play-property workaround, and the feature stops there.
