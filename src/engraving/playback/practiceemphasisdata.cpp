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

#include "practiceemphasisdata.h"

#include <algorithm>
#include <functional>

#include "global/serialization/json.h"

#include "dom/chord.h"
#include "dom/masterscore.h"
#include "dom/measure.h"
#include "dom/note.h"
#include "dom/score.h"
#include "dom/segment.h"
#include "dom/staff.h"
#include "dom/tie.h"

#include "log.h"

using namespace mu::engraving;

namespace {
constexpr const char* GENERATOR = "popatr-practice-emphasis";

const char* roleToString(PracticeEmphasisData::Role role)
{
    switch (role) {
    case PracticeEmphasisData::Role::Any:
        return "any";
    case PracticeEmphasisData::Role::Lower:
        return "lower";
    case PracticeEmphasisData::Role::Top:
        break;
    }
    return "top";
}

PracticeEmphasisData::Role roleFromString(const muse::String& text)
{
    if (text == u"lower") {
        return PracticeEmphasisData::Role::Lower;
    }
    if (text == u"any") {
        return PracticeEmphasisData::Role::Any;
    }
    return PracticeEmphasisData::Role::Top;
}

int displayedMeasureNumber(const Measure* measure)
{
    if (!measure) {
        return 0;
    }
    return measure->measureNumber() + measure->measureNumberOffset() + 1;
}

bool samePitchIsTop(const Chord* chord, const Note* note)
{
    const std::vector<Note*>& notes = chord->notes();
    if (notes.size() <= 1) {
        return true;
    }

    int bestPitch = notes.front()->pitch();
    size_t topIndex = 0;
    size_t noteIndex = 0;
    for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i]->pitch() > bestPitch) {
            bestPitch = notes[i]->pitch();
            topIndex = i;
        }
        if (notes[i] == note) {
            noteIndex = i;
        }
    }
    return noteIndex == topIndex;
}

void eachTiedNote(Note* note, const std::function<void(Note*)>& visit)
{
    if (!note) {
        return;
    }

    Note* start = note;
    while (Tie* back = start->tieBack()) {
        Note* previous = back->startNote();
        if (!previous || previous == start) {
            break;
        }
        start = previous;
    }

    Note* current = start;
    while (current) {
        visit(current);
        Tie* forward = current->tieFor();
        Note* next = forward ? forward->endNote() : nullptr;
        if (!next || next == current) {
            break;
        }
        current = next;
    }
}

void visitChordNotes(const Chord* chord, const std::function<void(const Note*)>& visit)
{
    if (!chord) {
        return;
    }
    for (const Note* note : chord->notes()) {
        visit(note);
    }
    for (const Chord* grace : chord->graceNotes()) {
        visitChordNotes(grace, visit);
    }
}

PracticeEmphasisData::PartRule makePart(const char* id, const char* name, int staff, int voice, PracticeEmphasisData::Role role)
{
    PracticeEmphasisData::PartRule part;
    part.id = id;
    part.name = name;
    part.staffIndex = staff;
    part.voice = voice;
    part.role = role;
    return part;
}
}

bool PracticeEmphasisData::PartRule::operator==(const PartRule& other) const
{
    return id == other.id
           && name == other.name
           && staffEid == other.staffEid
           && staffIndex == other.staffIndex
           && voice == other.voice
           && role == other.role
           && selected == other.selected;
}

bool PracticeEmphasisData::Document::operator==(const Document& other) const
{
    return version == other.version
           && playbackActive == other.playbackActive
           && emphasisMode == other.emphasisMode
           && emphasizedDb == other.emphasizedDb
           && otherDb == other.otherDb
           && fromMeasure == other.fromMeasure
           && toMeasure == other.toMeasure
           && parts == other.parts
           && overrides == other.overrides;
}

std::vector<PracticeEmphasisData::PartRule> PracticeEmphasisData::templatesForStaffCount(int staffCount)
{
    std::vector<PartRule> parts;
    if (staffCount <= 0) {
        return parts;
    }

    if (staffCount >= 4) {
        parts.push_back(makePart("S1", "1st soprano", 0, 0, Role::Top));
        parts.push_back(makePart("S2", "2nd soprano", 0, 0, Role::Lower));
        parts.push_back(makePart("A1", "1st alto", 1, 0, Role::Top));
        parts.push_back(makePart("A2", "2nd alto", 1, 0, Role::Lower));
        parts.push_back(makePart("T1", "1st tenor", 2, 0, Role::Top));
        parts.push_back(makePart("T2", "2nd tenor", 2, 0, Role::Lower));
        parts.push_back(makePart("B1", "1st bass", 3, 0, Role::Top));
        parts.push_back(makePart("B2", "2nd bass", 3, 0, Role::Lower));
        return parts;
    }

    if (staffCount == 1) {
        parts.push_back(makePart("S1", "1st soprano", 0, 0, Role::Top));
        parts.push_back(makePart("S2", "2nd soprano", 0, 0, Role::Lower));
        parts.push_back(makePart("A1", "1st alto", 0, 1, Role::Top));
        parts.push_back(makePart("A2", "2nd alto", 0, 1, Role::Lower));
        parts.push_back(makePart("T1", "1st tenor", 0, 2, Role::Top));
        parts.push_back(makePart("T2", "2nd tenor", 0, 2, Role::Lower));
        parts.push_back(makePart("B1", "1st bass", 0, 3, Role::Top));
        parts.push_back(makePart("B2", "2nd bass", 0, 3, Role::Lower));
        return parts;
    }

    // Closed SATB: soprano/alto share the upper staff, tenor/bass the lower.
    parts.push_back(makePart("S1", "1st soprano", 0, 0, Role::Top));
    parts.push_back(makePart("S2", "2nd soprano", 0, 0, Role::Lower));
    parts.push_back(makePart("A1", "1st alto", 0, 1, Role::Top));
    parts.push_back(makePart("A2", "2nd alto", 0, 1, Role::Lower));
    parts.push_back(makePart("T1", "1st tenor", 1, 0, Role::Top));
    parts.push_back(makePart("T2", "2nd tenor", 1, 0, Role::Lower));
    parts.push_back(makePart("B1", "1st bass", 1, 1, Role::Top));
    parts.push_back(makePart("B2", "2nd bass", 1, 1, Role::Lower));
    return parts;
}

void PracticeEmphasisData::setDocument(Document document, bool notify)
{
    m_document = std::move(document);
    if (notify) {
        notifyListeners();
    }
}

bool PracticeEmphasisData::isDefault() const
{
    return !m_document.playbackActive
           && !m_document.emphasisMode
           && m_document.emphasizedDb == DEFAULT_EMPHASIZED_DB
           && m_document.otherDb == DEFAULT_OTHER_DB
           && m_document.fromMeasure == 0
           && m_document.toMeasure == 0
           && m_document.parts.empty()
           && m_document.overrides.empty();
}

muse::ByteArray PracticeEmphasisData::toJson() const
{
    muse::JsonObject root;
    root.set("version", m_document.version);
    root.set("generator", GENERATOR);
    root.set("playbackActive", m_document.playbackActive);
    root.set("emphasisMode", m_document.emphasisMode);
    root.set("emphasizedGainDb", static_cast<double>(m_document.emphasizedDb));
    root.set("otherGainDb", static_cast<double>(m_document.otherDb));
    root.set("otherMode", "attenuate");
    root.set("fromMeasure", m_document.fromMeasure);
    root.set("toMeasure", m_document.toMeasure);

    muse::JsonArray parts;
    for (const PartRule& part : m_document.parts) {
        muse::JsonObject item;
        item.set("id", part.id);
        item.set("name", part.name);
        item.set("staffEid", part.staffEid);
        item.set("staffIndex", part.staffIndex);
        item.set("voice", part.voice);
        item.set("role", roleToString(part.role));
        item.set("selected", part.selected);
        parts.append(item);
    }
    root.set("parts", parts);

    muse::JsonObject overrides;
    for (const auto& pair : m_document.overrides) {
        overrides.set(pair.first, pair.second == Override::ForceOn ? "forceOn" : "forceOff");
    }
    root.set("overrides", overrides);

    return muse::JsonDocument(root).toJson();
}

bool PracticeEmphasisData::fromJson(const muse::ByteArray& json)
{
    if (json.empty()) {
        m_document = Document();
        return true;
    }

    std::string error;
    const muse::JsonDocument doc = muse::JsonDocument::fromJson(json, &error);
    if (!error.empty() || !doc.isObject()) {
        LOGE() << "Ignoring practice-emphasis.json: " << error;
        return false;
    }

    const muse::JsonObject root = doc.rootObject();
    Document next;
    next.version = root.value("version").toInt();
    if (next.version <= 0) {
        next.version = 1;
    }
    next.playbackActive = root.value("playbackActive").toBool();
    next.emphasisMode = root.value("emphasisMode").toBool();
    next.emphasizedDb = static_cast<float>(root.contains("emphasizedGainDb")
                                           ? root.value("emphasizedGainDb").toDouble()
                                           : static_cast<double>(DEFAULT_EMPHASIZED_DB));
    next.otherDb = static_cast<float>(root.contains("otherGainDb")
                                      ? root.value("otherGainDb").toDouble()
                                      : static_cast<double>(DEFAULT_OTHER_DB));
    next.fromMeasure = std::max(0, root.value("fromMeasure").toInt());
    next.toMeasure = std::max(0, root.value("toMeasure").toInt());

    const muse::JsonArray parts = root.value("parts").toArray();
    for (size_t i = 0; i < parts.size(); ++i) {
        const muse::JsonObject item = parts.at(i).toObject();
        PartRule part;
        part.id = item.value("id").toStdString();
        part.name = item.value("name").toStdString();
        part.staffEid = item.value("staffEid").toStdString();
        part.staffIndex = item.value("staffIndex").toInt();
        part.voice = item.value("voice").toInt();
        part.role = roleFromString(item.value("role").toString());
        part.selected = item.value("selected").toBool();
        if (part.id.empty() || part.voice < 0 || part.voice > 3 || part.staffIndex < 0) {
            continue;
        }
        next.parts.push_back(std::move(part));
    }

    const muse::JsonObject overrides = root.value("overrides").toObject();
    for (const std::string& key : overrides.keys()) {
        const muse::String value = overrides.value(key).toString();
        if (value == u"forceOn") {
            next.overrides[key] = Override::ForceOn;
        } else if (value == u"forceOff") {
            next.overrides[key] = Override::ForceOff;
        }
    }

    m_document = std::move(next);
    return true;
}

void PracticeEmphasisData::setPlaybackActive(bool active)
{
    if (m_document.playbackActive == active) {
        return;
    }
    m_document.playbackActive = active;
    notifyListeners();
}

void PracticeEmphasisData::setEmphasisMode(bool enabled)
{
    if (m_document.emphasisMode == enabled) {
        return;
    }
    m_document.emphasisMode = enabled;
    notifyListeners();
}

void PracticeEmphasisData::setGains(float emphasizedDb, float otherDb)
{
    if (m_document.emphasizedDb == emphasizedDb && m_document.otherDb == otherDb) {
        return;
    }
    m_document.emphasizedDb = emphasizedDb;
    m_document.otherDb = otherDb;
    notifyListeners();
}

void PracticeEmphasisData::setRange(int fromMeasure, int toMeasure)
{
    fromMeasure = std::max(0, fromMeasure);
    toMeasure = std::max(0, toMeasure);
    if (m_document.fromMeasure == fromMeasure && m_document.toMeasure == toMeasure) {
        return;
    }
    m_document.fromMeasure = fromMeasure;
    m_document.toMeasure = toMeasure;
    notifyListeners();
}

void PracticeEmphasisData::setPartSelected(const Score* score, const std::string& id, bool selected)
{
    if (!score || id.empty()) {
        return;
    }

    const std::vector<PartRule> templates = templatesForStaffCount(static_cast<int>(score->nstaves()));
    const PartRule* templ = nullptr;
    for (const PartRule& candidate : templates) {
        if (candidate.id == id) {
            templ = &candidate;
            break;
        }
    }

    PartRule* existing = nullptr;
    for (PartRule& part : m_document.parts) {
        if (part.id == id) {
            existing = &part;
            break;
        }
    }

    if (!existing && !templ) {
        return;
    }

    if (!existing) {
        PartRule created = *templ;
        if (const Staff* staff = score->staff(static_cast<size_t>(created.staffIndex))) {
            if (!staff->eid().isValid()) {
                staff->assignNewEID();
            }
            created.staffEid = staff->eid().toStdString();
        }
        created.selected = selected;
        m_document.parts.push_back(std::move(created));
        notifyListeners();
        return;
    }

    if (existing->selected == selected) {
        return;
    }
    existing->selected = selected;
    notifyListeners();
}

void PracticeEmphasisData::clearOverrides()
{
    if (m_document.overrides.empty()) {
        return;
    }
    m_document.overrides.clear();
    notifyListeners();
}

bool PracticeEmphasisData::partSelected(const std::string& id) const
{
    for (const PartRule& part : m_document.parts) {
        if (part.id == id) {
            return part.selected;
        }
    }
    return false;
}

int PracticeEmphasisData::resolvedStaffIndex(const Score* score, const PartRule& part) const
{
    if (score && score->masterScore() && !part.staffEid.empty()) {
        const EID eid = EID::fromStdString(part.staffEid);
        const EngravingObject* object = score->masterScore()->eidRegister()->itemFromEID(eid);
        if (object && object->isStaff()) {
            return static_cast<int>(toStaff(object)->idx());
        }
    }
    return part.staffIndex;
}

bool PracticeEmphasisData::claims(const Note* note, const PartRule& part) const
{
    if (!note || !note->chord() || !part.selected) {
        return false;
    }
    if (static_cast<int>(note->voice()) != part.voice) {
        return false;
    }
    if (static_cast<int>(note->staffIdx()) != resolvedStaffIndex(note->score(), part)) {
        return false;
    }

    const Measure* measure = note->findMeasure();
    const int number = displayedMeasureNumber(measure);
    if (m_document.fromMeasure > 0 && number < m_document.fromMeasure) {
        return false;
    }
    if (m_document.toMeasure > 0 && number > m_document.toMeasure) {
        return false;
    }

    const Chord* chord = note->chord();
    const bool shared = chord->notes().size() <= 1;
    const bool top = samePitchIsTop(chord, note);
    switch (part.role) {
    case Role::Any:
        return true;
    case Role::Top:
        return shared || top;
    case Role::Lower:
        return shared || !top;
    }
    return false;
}

bool PracticeEmphasisData::isEmphasized(const Note* note) const
{
    if (!note) {
        return false;
    }

    if (note->eid().isValid()) {
        const auto it = m_document.overrides.find(note->eid().toStdString());
        if (it != m_document.overrides.end()) {
            if (it->second == Override::ForceOn) {
                return true;
            }
            if (it->second == Override::ForceOff) {
                return false;
            }
        }
    }

    for (const PartRule& part : m_document.parts) {
        if (claims(note, part)) {
            return true;
        }
    }
    return false;
}

bool PracticeEmphasisData::playbackEngaged() const
{
    if (!m_document.playbackActive) {
        return false;
    }
    for (const PartRule& part : m_document.parts) {
        if (part.selected) {
            return true;
        }
    }
    for (const auto& pair : m_document.overrides) {
        if (pair.second == Override::ForceOn) {
            return true;
        }
    }
    return false;
}

void PracticeEmphasisData::toggleOverride(Note* note)
{
    if (!note) {
        return;
    }

    const bool ruleOn = [&]() {
        for (const PartRule& part : m_document.parts) {
            if (claims(note, part)) {
                return true;
            }
        }
        return false;
    }();
    const bool currently = isEmphasized(note);

    Override stored = Override::ForceOff;
    bool clear = false;
    if (currently) {
        stored = Override::ForceOff;
    } else if (ruleOn) {
        clear = true;
    } else {
        stored = Override::ForceOn;
    }

    eachTiedNote(note, [&](Note* tied) {
        if (!tied->eid().isValid()) {
            tied->assignNewEID();
        }
        const std::string key = tied->eid().toStdString();
        if (clear) {
            m_document.overrides.erase(key);
        } else {
            m_document.overrides[key] = stored;
        }
    });
    notifyListeners();
}

std::vector<std::pair<int, int> > PracticeEmphasisData::emphasizedVoices(const Score* score) const
{
    std::vector<std::pair<int, int> > voices;
    if (!score) {
        return voices;
    }

    auto add = [&voices](int staff, int voice) {
        const std::pair<int, int> key { staff, voice };
        if (std::find(voices.begin(), voices.end(), key) == voices.end()) {
            voices.push_back(key);
        }
    };

    for (Measure* measure = score->firstMeasure(); measure; measure = measure->nextMeasure()) {
        for (Segment* segment = measure->first(SegmentType::ChordRest); segment; segment = segment->next(SegmentType::ChordRest)) {
            for (track_idx_t track = 0; track < score->ntracks(); ++track) {
                const EngravingItem* element = segment->element(track);
                if (!element || !element->isChord()) {
                    continue;
                }
                visitChordNotes(toChord(element), [&](const Note* note) {
                    if (isEmphasized(note)) {
                        add(static_cast<int>(note->staffIdx()), static_cast<int>(note->voice()));
                    }
                });
            }
        }
    }
    return voices;
}

void PracticeEmphasisData::notifyListeners()
{
    m_changed.send();
}
