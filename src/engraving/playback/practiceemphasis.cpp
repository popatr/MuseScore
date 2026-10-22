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

#include "practiceemphasis.h"

#include "practiceemphasisdata.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

#include "log.h"

#include "dom/chord.h"
#include "dom/masterscore.h"
#include "dom/note.h"
#include "dom/score.h"

#include "mpe/mpetypes.h"

#include "renderingcontext.h"
#include "utils/expressionutils.h"

using namespace mu::engraving;
using namespace muse::mpe;

namespace {
std::optional<PracticeEmphasis::Config> g_override;
std::optional<PracticeEmphasis::Config> g_environment;
bool g_logged = false;

float parseFloat(const char* text, float fallback)
{
    if (!text || text[0] == '\0') {
        return fallback;
    }

    char* end = nullptr;
    const float value = std::strtof(text, &end);
    if (end == text) {
        return fallback;
    }
    return value;
}

PracticeEmphasis::Config parseSpec(const char* raw)
{
    PracticeEmphasis::Config config;
    if (!raw || raw[0] == '\0' || std::strcmp(raw, "off") == 0 || std::strcmp(raw, "0") == 0) {
        return config;
    }

    if (std::strncmp(raw, "voice:", 6) == 0) {
        int staff = 0;
        int voice = 0;
        float gain = 0.f;
        if (std::sscanf(raw, "voice:%d:%d:%f", &staff, &voice, &gain) < 3 || staff < 1 || voice < 1 || voice > 4) {
            LOGE() << "Ignoring MUSESCORE_PRACTICE_EMPHASIS='" << raw
                   << "'. Expected voice:<staff>:<voice>:<dB> with staff >= 1 and voice in 1..4";
            return config;
        }

        config.mode = PracticeEmphasis::Mode::VoiceGain;
        config.staffIndex = staff - 1;
        config.voiceIndex = voice - 1;
        config.voiceGainDb = gain;
        return config;
    }

    if (std::strncmp(raw, "s1", 2) == 0 || std::strncmp(raw, "s2", 2) == 0) {
        config.mode = PracticeEmphasis::Mode::Divisi;
        config.part = raw[1] == '1' ? PracticeEmphasis::Part::S1 : PracticeEmphasis::Part::S2;
        config.staffIndex = 0;
        config.voiceIndex = 0;
        config.emphasizedDb = 6.f;
        config.otherDb = -12.f;

        if (raw[2] == ':') {
            float emphasized = config.emphasizedDb;
            float other = config.otherDb;
            if (std::sscanf(raw + 3, "%f:%f", &emphasized, &other) == 2) {
                config.emphasizedDb = emphasized;
                config.otherDb = other;
            }
        }

        config.emphasizedDb = parseFloat(std::getenv("MUSESCORE_PRACTICE_EMPHASIS_EMPH_DB"), config.emphasizedDb);
        config.otherDb = parseFloat(std::getenv("MUSESCORE_PRACTICE_EMPHASIS_OTHER_DB"), config.otherDb);
        return config;
    }

    LOGE() << "Ignoring MUSESCORE_PRACTICE_EMPHASIS='" << raw << "'. Expected off, voice:<staff>:<voice>:<dB>, s1, or s2";
    return config;
}

void logOnce(const PracticeEmphasis::Config& config)
{
    if (g_logged || config.mode == PracticeEmphasis::Mode::Off) {
        return;
    }
    g_logged = true;

    if (config.mode == PracticeEmphasis::Mode::VoiceGain) {
        LOGI() << "Practice emphasis: voice gain staff " << (config.staffIndex + 1)
               << " voice " << (config.voiceIndex + 1)
               << " " << config.voiceGainDb << " dB";
        return;
    }

    LOGI() << "Practice emphasis: divisi " << (config.part == PracticeEmphasis::Part::S1 ? "S1" : "S2")
           << " on staff " << (config.staffIndex + 1) << " voice " << (config.voiceIndex + 1)
           << " emphasized " << config.emphasizedDb << " dB, others " << config.otherDb << " dB"
           << ", companion staff offset " << PracticeEmphasis::EMPHASIS_STAFF_OFFSET;
}

muse::real_t scaleNormalized(muse::real_t normalized, float gainDb)
{
    const dynamic_level_t level = dynamicLevelFromNormalized(normalized);
    const dynamic_level_t scaled = PracticeEmphasis::scaleDynamicLevel(level, gainDb);
    return muse::real_t(static_cast<double>(scaled) / static_cast<double>(MAX_DYNAMIC_LEVEL));
}

void scaleCurve(DynamicAutomationMap& curve, float gainDb)
{
    if (gainDb == 0.f || curve.empty()) {
        return;
    }

    for (auto it = curve.begin(); it != curve.end(); ++it) {
        muse::mpe::AutomationPoint& point = it->second;
        point.outValue = scaleNormalized(point.outValue, gainDb);
        if (auto* arrival = std::get_if<muse::mpe::AutomationPoint::ExplicitArrival>(&point.inValue)) {
            arrival->value = scaleNormalized(arrival->value, gainDb);
        }
    }
}

DynamicAutomationMap constantCurve(dynamic_level_t level)
{
    muse::mpe::AutomationPoint point;
    point.outValue = muse::real_t(static_cast<double>(level) / static_cast<double>(MAX_DYNAMIC_LEVEL));

    DynamicAutomationMap curve;
    curve[timestamp_t(0)] = point;
    return curve;
}

bool partOwnsStaff(const DynamicAutomationLayers& layers, int staffIndex)
{
    for (auto it = layers.cbegin(); it != layers.cend(); ++it) {
        if (static_cast<int>(it->first / MAX_VOICES) == staffIndex) {
            return true;
        }
    }
    return false;
}
}

const PracticeEmphasis::Config& PracticeEmphasis::current()
{
    if (g_override) {
        logOnce(*g_override);
        return *g_override;
    }

    if (!g_environment) {
        g_environment = parseSpec(std::getenv("MUSESCORE_PRACTICE_EMPHASIS"));
    }

    logOnce(*g_environment);
    return *g_environment;
}

void PracticeEmphasis::setOverrideForTesting(const Config& config)
{
    g_override = config;
}

void PracticeEmphasis::clearOverrideForTesting()
{
    g_override.reset();
}

void PracticeEmphasis::clearEnvironmentCacheForTesting()
{
    g_environment.reset();
    g_logged = false;
}

bool PracticeEmphasis::debugOverrideActive()
{
    return current().mode != Mode::Off;
}

dynamic_level_t PracticeEmphasis::scaleDynamicLevel(dynamic_level_t level, float gainDb)
{
    if (gainDb == 0.f) {
        return level;
    }

    const float steps = gainDb / DB_PER_DYNAMIC_STEP;
    const float scaled = static_cast<float>(level) + steps * static_cast<float>(DYNAMIC_LEVEL_STEP);
    const float clamped = std::clamp(scaled,
                                     static_cast<float>(MIN_DYNAMIC_LEVEL),
                                     static_cast<float>(MAX_DYNAMIC_LEVEL));
    return static_cast<dynamic_level_t>(std::lround(clamped));
}

bool PracticeEmphasis::noteIsEmphasized(const Note* note, const Config& config)
{
    if (!note || !note->chord() || config.mode != Mode::Divisi) {
        return false;
    }

    if (static_cast<int>(note->staffIdx()) != config.staffIndex || static_cast<int>(note->voice()) != config.voiceIndex) {
        return false;
    }

    const Chord* chord = note->chord();
    const std::vector<Note*>& notes = chord->notes();
    if (notes.size() <= 1) {
        // Written unison: both parts sing it. Selecting either part emphasizes it.
        return true;
    }

    int topPitch = notes.front()->pitch();
    for (const Note* candidate : notes) {
        topPitch = std::max(topPitch, candidate->pitch());
    }

    const bool isTop = note->pitch() == topPitch;
    return config.part == Part::S1 ? isTop : !isTop;
}

void PracticeEmphasis::applyToNote(const Note* note, NominalNoteCtx& ctx)
{
    const Config& config = current();
    if (!note) {
        return;
    }

    if (config.mode == Mode::VoiceGain) {
        if (static_cast<int>(note->staffIdx()) == config.staffIndex && static_cast<int>(note->voice()) == config.voiceIndex) {
            ctx.dynamicLevel = scaleDynamicLevel(ctx.dynamicLevel, config.voiceGainDb);
        }
        return;
    }

    if (config.mode == Mode::Divisi) {
        const bool emphasized = noteIsEmphasized(note, config);
        ctx.dynamicLevel = scaleDynamicLevel(ctx.dynamicLevel, emphasized ? config.emphasizedDb : config.otherDb);
        if (emphasized) {
            ctx.staffIdx = static_cast<staff_idx_t>(ctx.staffIdx + EMPHASIS_STAFF_OFFSET);
        }
        return;
    }

    const Score* score = note->score();
    const MasterScore* master = score ? score->masterScore() : nullptr;
    if (!master) {
        return;
    }
    const PracticeEmphasisData& data = master->practiceEmphasis();
    if (!data.playbackEngaged()) {
        return;
    }

    const bool emphasized = data.isEmphasized(note);
    const float gain = emphasized ? data.document().emphasizedDb : data.document().otherDb;
    ctx.dynamicLevel = scaleDynamicLevel(ctx.dynamicLevel, gain);
    if (emphasized) {
        ctx.staffIdx = static_cast<staff_idx_t>(ctx.staffIdx + EMPHASIS_STAFF_OFFSET);
    }
}

void PracticeEmphasis::applyToDynamics(const Score* score, DynamicAutomationLayers& layers)
{
    const Config& config = current();
    if (config.mode == Mode::Off) {
        if (!score || layers.empty()) {
            return;
        }
        const MasterScore* master = score->masterScore();
        if (!master) {
            return;
        }
        const PracticeEmphasisData& data = master->practiceEmphasis();
        if (!data.playbackEngaged()) {
            return;
        }

        std::vector<std::pair<layer_idx_t, DynamicAutomationMap> > companions;
        for (const std::pair<int, int>& voice : data.emphasizedVoices(score)) {
            if (!partOwnsStaff(layers, voice.first)) {
                continue;
            }
            const layer_idx_t sourceKey = makeLayerIdx(static_cast<staff_layer_idx_t>(voice.first),
                                                       static_cast<voice_layer_idx_t>(voice.second));
            auto source = layers.find(sourceKey);
            DynamicAutomationMap curve = source != layers.end() ? source->second : constantCurve(NATURAL_DYNAMIC_LEVEL);
            scaleCurve(curve, data.document().emphasizedDb);
            const layer_idx_t companionKey = makeLayerIdx(static_cast<staff_layer_idx_t>(voice.first + EMPHASIS_STAFF_OFFSET),
                                                          static_cast<voice_layer_idx_t>(voice.second));
            companions.emplace_back(companionKey, std::move(curve));
        }

        for (auto it = layers.begin(); it != layers.end(); ++it) {
            if (static_cast<int>(it->first / MAX_VOICES) >= EMPHASIS_STAFF_OFFSET) {
                continue;
            }
            scaleCurve(it->second, data.document().otherDb);
        }
        for (auto& companion : companions) {
            layers[companion.first] = std::move(companion.second);
        }
        return;
    }

    if (layers.empty()) {
        return;
    }

    if (config.mode == Mode::VoiceGain) {
        const layer_idx_t key = makeLayerIdx(static_cast<staff_layer_idx_t>(config.staffIndex),
                                             static_cast<voice_layer_idx_t>(config.voiceIndex));
        auto it = layers.find(key);
        if (it != layers.end()) {
            scaleCurve(it->second, config.voiceGainDb);
        }
        return;
    }

    const bool ownsStaff = partOwnsStaff(layers, config.staffIndex);
    DynamicAutomationMap companionSource;
    const layer_idx_t sourceKey = makeLayerIdx(static_cast<staff_layer_idx_t>(config.staffIndex),
                                               static_cast<voice_layer_idx_t>(config.voiceIndex));
    if (ownsStaff) {
        auto source = layers.find(sourceKey);
        companionSource = source != layers.end() ? source->second : constantCurve(NATURAL_DYNAMIC_LEVEL);
        scaleCurve(companionSource, config.emphasizedDb);
    }

    for (auto it = layers.begin(); it != layers.end(); ++it) {
        scaleCurve(it->second, config.otherDb);
    }

    if (ownsStaff) {
        const layer_idx_t companionKey = makeLayerIdx(static_cast<staff_layer_idx_t>(config.staffIndex + EMPHASIS_STAFF_OFFSET),
                                                      static_cast<voice_layer_idx_t>(config.voiceIndex));
        layers[companionKey] = std::move(companionSource);
        if (layers.find(sourceKey) == layers.end()) {
            layers[sourceKey] = constantCurve(scaleDynamicLevel(NATURAL_DYNAMIC_LEVEL, config.otherDb));
        }
    }
}
