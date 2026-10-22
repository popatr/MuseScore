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

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <optional>
#include <vector>

#include "mpe/events.h"
#include "mpe/mpetypes.h"
#include "mpe/tests/utils/articulationutils.h"
#include "mpe/tests/mocks/articulationprofilesrepositorymock.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"
#include "engraving/playback/playbackmodel.h"
#include "engraving/playback/practiceemphasis.h"

#include "utils/scorerw.h"

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::_;

using namespace mu::engraving;
using namespace muse::mpe;
using namespace muse;

namespace {
const String SCORE_FILE("playback/playbackmodel_data/practice_emphasis/ssatb-one-bar.mscx");

struct HeardNote {
    int midi = 0;
    staff_layer_idx_t staff = 0;
    voice_layer_idx_t voice = 0;
    dynamic_level_t level = 0;
};

int midiFromPitchLevel(pitch_level_t pitch)
{
    return ZERO_PITCH_LEVEL_MIDI_EQUIVALENT + static_cast<int>(pitch / PITCH_LEVEL_STEP);
}

std::vector<HeardNote> heardNotes(const PlaybackData& data)
{
    std::vector<HeardNote> notes;
    for (const auto& pair : data.originEvents) {
        for (const PlaybackEvent& event : pair.second) {
            if (!std::holds_alternative<muse::mpe::NoteEvent>(event)) {
                continue;
            }
            const muse::mpe::NoteEvent& note = std::get<muse::mpe::NoteEvent>(event);
            HeardNote heard;
            heard.midi = midiFromPitchLevel(note.pitchCtx().nominalPitchLevel);
            heard.staff = note.arrangementCtx().staffLayerIndex;
            heard.voice = note.arrangementCtx().voiceLayerIndex;
            heard.level = note.expressionCtx().nominalDynamicLevel;
            notes.push_back(heard);
        }
    }
    return notes;
}

const HeardNote* findMidi(const std::vector<HeardNote>& notes, int midi)
{
    for (const HeardNote& note : notes) {
        if (note.midi == midi) {
            return &note;
        }
    }
    return nullptr;
}

double curveOut(const DynamicAutomationLayers& layers, layer_idx_t key)
{
    auto it = layers.find(key);
    EXPECT_NE(it, layers.end()) << "missing dynamic layer " << key;
    if (it == layers.end() || it->second.empty()) {
        return 0.0;
    }
    return it->second.begin()->second.outValue;
}

class OverrideGuard
{
public:
    explicit OverrideGuard(const PracticeEmphasis::Config& config)
    {
        PracticeEmphasis::setOverrideForTesting(config);
    }

    ~OverrideGuard()
    {
        PracticeEmphasis::clearOverrideForTesting();
    }
};
}

class Engraving_PracticeEmphasisTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_defaultProfile = std::make_shared<ArticulationsProfile>();
        m_repositoryMock = std::make_shared<NiceMock<ArticulationProfilesRepositoryMock> >();
        ON_CALL(*m_repositoryMock, defaultProfile(_)).WillByDefault(Return(m_defaultProfile));
    }

    void TearDown() override
    {
        PracticeEmphasis::clearOverrideForTesting();
    }

    PlaybackData loadPart(MasterScore* score, size_t partIndex)
    {
        const Part* part = score->parts().at(partIndex);
        PlaybackModel model(modularity::globalCtx());
        model.profilesRepository.set(m_repositoryMock);
        model.load(score);
        return model.resolveTrackPlaybackData(part->id(), part->instrumentId());
    }

    ArticulationsProfilePtr m_defaultProfile;
    std::shared_ptr<NiceMock<ArticulationProfilesRepositoryMock> > m_repositoryMock;
};

TEST(Engraving_PracticeEmphasis, DynamicStepMatchesSixDecibels)
{
    const dynamic_level_t mf = dynamicLevelFromType(muse::mpe::DynamicType::mf);
    EXPECT_EQ(PracticeEmphasis::scaleDynamicLevel(mf, 0.f), mf);
    EXPECT_EQ(PracticeEmphasis::scaleDynamicLevel(mf, 6.f), dynamicLevelFromType(muse::mpe::DynamicType::f));
    EXPECT_EQ(PracticeEmphasis::scaleDynamicLevel(mf, -12.f), dynamicLevelFromType(muse::mpe::DynamicType::p));
    EXPECT_EQ(PracticeEmphasis::scaleDynamicLevel(dynamicLevelFromType(muse::mpe::DynamicType::fffffffff), 24.f),
              dynamicLevelFromType(muse::mpe::DynamicType::fffffffff));
    EXPECT_EQ(PracticeEmphasis::scaleDynamicLevel(dynamicLevelFromType(muse::mpe::DynamicType::ppppppppp), -24.f),
              dynamicLevelFromType(muse::mpe::DynamicType::ppppppppp));
}

TEST_F(Engraving_PracticeEmphasisTests, VoiceGainScalesOneVoiceAndLeavesTheChordAlone)
{
    MasterScore* score = ScoreRW::readScore(SCORE_FILE);
    ASSERT_TRUE(score);
    ASSERT_EQ(score->parts().size(), 2);
    score->initAutomation();

    PlaybackData before = loadPart(score, 0);
    const std::vector<HeardNote> written = heardNotes(before);
    const HeardNote* soprano = findMidi(written, 76);
    const HeardNote* alto = findMidi(written, 67);
    ASSERT_TRUE(soprano);
    ASSERT_TRUE(alto);
    const dynamic_level_t sopranoWritten = soprano->level;
    const dynamic_level_t altoWritten = alto->level;

    PracticeEmphasis::Config config;
    config.mode = PracticeEmphasis::Mode::VoiceGain;
    config.staffIndex = 0;
    config.voiceIndex = 1;
    config.voiceGainDb = 6.f;
    OverrideGuard guard(config);

    PlaybackData after = loadPart(score, 0);
    const std::vector<HeardNote> heard = heardNotes(after);
    const HeardNote* sopranoAfter = findMidi(heard, 76);
    const HeardNote* altoAfter = findMidi(heard, 67);
    const HeardNote* altoSecond = findMidi(heard, 65);
    ASSERT_TRUE(sopranoAfter);
    ASSERT_TRUE(altoAfter);
    ASSERT_TRUE(altoSecond);

    EXPECT_EQ(sopranoAfter->level, sopranoWritten);
    EXPECT_EQ(sopranoAfter->staff, 0);
    EXPECT_EQ(sopranoAfter->voice, 0);
    EXPECT_EQ(altoAfter->level, PracticeEmphasis::scaleDynamicLevel(altoWritten, 6.f));
    EXPECT_NE(altoAfter->level, altoWritten);
    EXPECT_EQ(altoSecond->level, altoAfter->level);
    EXPECT_EQ(altoAfter->staff, 0);
    EXPECT_EQ(altoAfter->voice, 1);
    EXPECT_EQ(after.dynamics.find(makeLayerIdx(PracticeEmphasis::EMPHASIS_STAFF_OFFSET, 0)), after.dynamics.end());

    const layer_idx_t altoLayer = makeLayerIdx(0, 1);
    if (before.dynamics.find(altoLayer) != before.dynamics.end()) {
        const dynamic_level_t scaled = PracticeEmphasis::scaleDynamicLevel(
            dynamicLevelFromNormalized(real_t(curveOut(before.dynamics, altoLayer))), 6.f);
        EXPECT_NEAR(curveOut(after.dynamics, altoLayer),
                    static_cast<double>(scaled) / static_cast<double>(MAX_DYNAMIC_LEVEL),
                    0.0001);
        EXPECT_NEAR(curveOut(after.dynamics, makeLayerIdx(0, 0)), curveOut(before.dynamics, makeLayerIdx(0, 0)), 0.0001);
    }
}

TEST_F(Engraving_PracticeEmphasisTests, DivisiSplitsTheChordAndKeepsTheSharedUnison)
{
    MasterScore* score = ScoreRW::readScore(SCORE_FILE);
    ASSERT_TRUE(score);
    score->initAutomation();

    PlaybackData writtenWomen = loadPart(score, 0);
    PlaybackData writtenMen = loadPart(score, 1);
    const std::vector<HeardNote> womenWrittenNotes = heardNotes(writtenWomen);
    const std::vector<HeardNote> menWrittenNotes = heardNotes(writtenMen);
    ASSERT_TRUE(findMidi(womenWrittenNotes, 76));
    ASSERT_TRUE(findMidi(menWrittenNotes, 60));
    const dynamic_level_t writtenLevel = findMidi(womenWrittenNotes, 76)->level;
    const dynamic_level_t tenorWritten = findMidi(menWrittenNotes, 60)->level;

    auto run = [&](PracticeEmphasis::Part part) {
        PracticeEmphasis::Config config;
        config.mode = PracticeEmphasis::Mode::Divisi;
        config.part = part;
        config.staffIndex = 0;
        config.voiceIndex = 0;
        config.emphasizedDb = 6.f;
        config.otherDb = -12.f;
        OverrideGuard guard(config);
        return std::pair<PlaybackData, PlaybackData> { loadPart(score, 0), loadPart(score, 1) };
    };

    const dynamic_level_t loud = PracticeEmphasis::scaleDynamicLevel(writtenLevel, 6.f);
    const dynamic_level_t quiet = PracticeEmphasis::scaleDynamicLevel(writtenLevel, -12.f);
    const dynamic_level_t tenorQuiet = PracticeEmphasis::scaleDynamicLevel(tenorWritten, -12.f);
    const staff_layer_idx_t companion = static_cast<staff_layer_idx_t>(PracticeEmphasis::EMPHASIS_STAFF_OFFSET);

    {
        auto [women, men] = run(PracticeEmphasis::Part::S1);
        const std::vector<HeardNote> notes = heardNotes(women);
        const HeardNote* e5 = findMidi(notes, 76);
        const HeardNote* c5 = findMidi(notes, 72);
        const HeardNote* d5 = findMidi(notes, 74);
        const HeardNote* alto = findMidi(notes, 67);
        ASSERT_TRUE(e5);
        ASSERT_TRUE(c5);
        ASSERT_TRUE(d5);
        ASSERT_TRUE(alto);

        EXPECT_EQ(e5->staff, companion);
        EXPECT_EQ(e5->level, loud);
        EXPECT_EQ(d5->staff, companion);
        EXPECT_EQ(d5->level, loud);
        EXPECT_EQ(c5->staff, 0);
        EXPECT_EQ(c5->level, quiet);
        EXPECT_EQ(c5->voice, 0);
        EXPECT_EQ(alto->staff, 0);
        EXPECT_EQ(alto->voice, 1);
        EXPECT_EQ(alto->level, quiet);

        const std::vector<HeardNote> menNotes = heardNotes(men);
        const HeardNote* tenor = findMidi(menNotes, 60);
        const HeardNote* bass = findMidi(menNotes, 48);
        ASSERT_TRUE(tenor);
        ASSERT_TRUE(bass);
        EXPECT_EQ(tenor->staff, 1);
        EXPECT_EQ(tenor->level, tenorQuiet);
        EXPECT_EQ(bass->staff, 1);
        EXPECT_EQ(bass->voice, 1);
        EXPECT_EQ(bass->level, tenorQuiet);

        EXPECT_NE(women.dynamics.find(makeLayerIdx(companion, 0)), women.dynamics.end());
        EXPECT_GT(curveOut(women.dynamics, makeLayerIdx(companion, 0)), curveOut(women.dynamics, makeLayerIdx(0, 0)));
    }

    {
        auto [women, men] = run(PracticeEmphasis::Part::S2);
        const std::vector<HeardNote> notes = heardNotes(women);
        const HeardNote* e5 = findMidi(notes, 76);
        const HeardNote* c5 = findMidi(notes, 72);
        const HeardNote* d5 = findMidi(notes, 74);
        ASSERT_TRUE(e5);
        ASSERT_TRUE(c5);
        ASSERT_TRUE(d5);

        // S2 keeps the lower divisi note and the shared unison. S1's E5 stays quiet.
        EXPECT_EQ(c5->staff, companion);
        EXPECT_EQ(c5->level, loud);
        EXPECT_EQ(d5->staff, companion);
        EXPECT_EQ(d5->level, loud);
        EXPECT_EQ(e5->staff, 0);
        EXPECT_EQ(e5->level, quiet);
        EXPECT_EQ(findMidi(notes, 67)->level, quiet);
        const std::vector<HeardNote> menNotes = heardNotes(men);
        ASSERT_TRUE(findMidi(menNotes, 43));
        EXPECT_EQ(findMidi(menNotes, 43)->level, tenorQuiet);
        EXPECT_GT(curveOut(women.dynamics, makeLayerIdx(companion, 0)), curveOut(women.dynamics, makeLayerIdx(0, 0)));
    }
}
