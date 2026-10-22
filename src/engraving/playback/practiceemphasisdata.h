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

#include <string>
#include <utility>
#include <vector>
#include <unordered_map>

#include "global/async/channel.h"
#include "global/types/bytearray.h"

namespace mu::engraving {
class Note;
class Score;

//! Practice-track emphasis stored beside the score, never in notation properties.
//! A note is loud when any selected part claims it, unless a per-note override says otherwise.
//! A single notehead is claimed by both parts of that voice (written unison). The top note of a
//! chord belongs to the "1" part and every lower notehead belongs to the "2" part.
class PracticeEmphasisData
{
public:
    static constexpr float DEFAULT_EMPHASIZED_DB = 9.f;
    static constexpr float DEFAULT_OTHER_DB = -15.f;

    enum class Role {
        Any,
        Top,
        Lower
    };

    enum class Override {
        ForceOn,
        ForceOff
    };

    struct PartRule {
        std::string id;
        std::string name;
        std::string staffEid;
        int staffIndex = 0;
        int voice = 0;
        Role role = Role::Top;
        bool selected = false;

        bool operator==(const PartRule& other) const;
    };

    struct Document {
        int version = 1;
        bool playbackActive = false;
        bool emphasisMode = false;
        float emphasizedDb = DEFAULT_EMPHASIZED_DB;
        float otherDb = DEFAULT_OTHER_DB;
        int fromMeasure = 0;
        int toMeasure = 0;
        std::vector<PartRule> parts;
        std::unordered_map<std::string, Override> overrides;

        bool operator==(const Document& other) const;
    };

    static std::vector<PartRule> templatesForStaffCount(int staffCount);

    const Document& document() const { return m_document; }
    void setDocument(Document document, bool notifyListeners = true);

    bool isDefault() const;
    muse::ByteArray toJson() const;
    bool fromJson(const muse::ByteArray& json);

    void setPlaybackActive(bool active);
    void setEmphasisMode(bool enabled);
    void setGains(float emphasizedDb, float otherDb);
    void setRange(int fromMeasure, int toMeasure);
    void setPartSelected(const Score* score, const std::string& id, bool selected);
    void clearOverrides();

    bool partSelected(const std::string& id) const;
    bool isEmphasized(const Note* note) const;
    bool playbackEngaged() const;
    void toggleOverride(Note* note);

    //! Staff/voice pairs that contain at least one emphasized note.
    std::vector<std::pair<int, int> > emphasizedVoices(const Score* score) const;

    muse::async::Channel<> changed() const { return m_changed; }

private:
    bool claims(const Note* note, const PartRule& part) const;
    int resolvedStaffIndex(const Score* score, const PartRule& part) const;
    void notifyListeners();

    Document m_document;
    muse::async::Channel<> m_changed;
};
}
