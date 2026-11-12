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
class Score;
struct NominalNoteCtx;

//! Practice-track emphasis. Playback and audio export share this path.
//!
//! The score's practice-emphasis.json is the normal control. MUSESCORE_PRACTICE_EMPHASIS is a
//! debug override that ignores the score file when it is set:
//!   off | (unset)              use the score sidecar
//!   voice:<staff>:<voice>:<dB> scale one voice's dynamic curve. Staff and voice are 1-based.
//!   s1 | s2                    divisi on staff 1 voice 1. Top note is S1, lower notes are S2,
//!                              a single notehead belongs to both. Defaults are +6 and -12 dB.
//!   s1:<emphDb>:<otherDb>      same split with explicit gains.
//! MUSESCORE_PRACTICE_EMPHASIS_EMPH_DB and _OTHER_DB override the s1/s2 gains.
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
    static void clearEnvironmentCacheForTesting();
    static bool debugOverrideActive();

    static muse::mpe::dynamic_level_t scaleDynamicLevel(muse::mpe::dynamic_level_t level, float gainDb);

    static bool noteIsEmphasized(const Note* note, const Config& config);
    static void applyToNote(const Note* note, NominalNoteCtx& ctx);
    static void applyToDynamics(const Score* score, muse::mpe::DynamicAutomationLayers& layers);
};
}
