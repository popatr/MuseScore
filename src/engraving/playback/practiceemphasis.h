/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include "mpe/events.h"

namespace mu::engraving {
class Note;
struct NominalNoteCtx;

//! Prototype practice-track emphasis. No UI. Set MUSESCORE_PRACTICE_EMPHASIS:
//!   off | (unset)              written balance
//!   voice:<staff>:<voice>:<dB> scale one voice's dynamic curve. Staff and voice are 1-based, voice is 1..4.
//!                              Example: voice:1:2:-12 attenuates staff 1, voice 2.
//!   s1 | s2                    divisi on staff 1 voice 1. The top note of a chord is S1, every lower
//!                              note is S2, and a single notehead belongs to both. Emphasized notes
//!                              move to a companion staff layer. Everyone else is attenuated.
//!   s1:<emphDb>:<otherDb>      same, with explicit gains. Defaults are +6 and -12.
//!                              One dynamic step (mf to f) is 6 dB.
//! MUSESCORE_PRACTICE_EMPHASIS_EMPH_DB and _OTHER_DB override the s1/s2 gains.
//! The same playback events feed live playback and audio export.
class PracticeEmphasis
{
public:
    static constexpr int EMPHASIS_STAFF_OFFSET = 256;
    static constexpr float DB_PER_DYNAMIC_STEP = 6.f;

    enum class Mode {
        Off,
        VoiceGain,
        Divisi
    };

    enum class Part {
        S1,
        S2
    };

    struct Config {
        Mode mode = Mode::Off;
        int staffIndex = 0;
        int voiceIndex = 0;
        float voiceGainDb = 0.f;
        Part part = Part::S1;
        float emphasizedDb = 6.f;
        float otherDb = -12.f;
    };

    static const Config& current();
    static void setOverrideForTesting(const Config& config);
    static void clearOverrideForTesting();

    static muse::mpe::dynamic_level_t scaleDynamicLevel(muse::mpe::dynamic_level_t level, float gainDb);

    static bool noteIsEmphasized(const Note* note, const Config& config);
    static void applyToNote(const Note* note, NominalNoteCtx& ctx);
    static void applyToDynamics(muse::mpe::DynamicAutomationLayers& layers);
};
}
