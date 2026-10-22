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

#include "practiceemphasispanelmodel.h"

#include <cmath>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/measurebase.h"
#include "engraving/playback/practiceemphasis.h"
#include "engraving/playback/practiceemphasisdata.h"

#include "notation/inotation.h"
#include "notation/inotationelements.h"
#include "notation/inotationinteraction.h"
#include "notation/inotationselection.h"

using namespace mu::playback;
using namespace mu::engraving;
using namespace mu::notation;

namespace {
int displayedMeasureNumber(const MeasureBase* measureBase)
{
    if (!measureBase || !measureBase->isMeasure()) {
        return 0;
    }
    const Measure* measure = toMeasure(measureBase);
    return measure->measureNumber() + measure->measureNumberOffset() + 1;
}
}

PracticeEmphasisPanelModel::PracticeEmphasisPanelModel(QObject* parent)
    : QObject(parent)
    , muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void PracticeEmphasisPanelModel::load()
{
    if (m_loaded) {
        refresh();
        return;
    }
    m_loaded = true;
    if (globalContext()) {
        globalContext()->currentNotationChanged().onNotify(this, [this]() {
            connectScore();
            refresh();
        });
    }
    connectScore();
    refresh();
}

MasterScore* PracticeEmphasisPanelModel::currentMaster() const
{
    if (!globalContext()) {
        return nullptr;
    }
    const INotationPtr notation = globalContext()->currentNotation();
    if (!notation || !notation->elements()) {
        return nullptr;
    }
    Score* score = notation->elements()->msScore();
    return score ? score->masterScore() : nullptr;
}

PracticeEmphasisData* PracticeEmphasisPanelModel::data() const
{
    MasterScore* master = currentMaster();
    return master ? &master->practiceEmphasis() : nullptr;
}

void PracticeEmphasisPanelModel::connectScore()
{
    // SetReplace keeps a single callback on this score. The previous score's channel
    // drops the callback itself when that score is destroyed.
    if (PracticeEmphasisData* emphasis = data()) {
        emphasis->changed().onReceive(this, [this]() {
            refresh();
        }, muse::async::Asyncable::Mode::SetReplace);
    }
}

void PracticeEmphasisPanelModel::refresh()
{
    emit stateChanged();
}

bool PracticeEmphasisPanelModel::hasScore() const
{
    return currentMaster() != nullptr;
}

bool PracticeEmphasisPanelModel::playbackActive() const
{
    const PracticeEmphasisData* emphasis = data();
    return emphasis && emphasis->document().playbackActive;
}

bool PracticeEmphasisPanelModel::emphasisMode() const
{
    const PracticeEmphasisData* emphasis = data();
    return emphasis && emphasis->document().emphasisMode;
}

int PracticeEmphasisPanelModel::emphasizedDb() const
{
    const PracticeEmphasisData* emphasis = data();
    return emphasis ? static_cast<int>(std::lround(emphasis->document().emphasizedDb))
                    : static_cast<int>(PracticeEmphasisData::DEFAULT_EMPHASIZED_DB);
}

int PracticeEmphasisPanelModel::otherDb() const
{
    const PracticeEmphasisData* emphasis = data();
    return emphasis ? static_cast<int>(std::lround(emphasis->document().otherDb))
                    : static_cast<int>(PracticeEmphasisData::DEFAULT_OTHER_DB);
}

int PracticeEmphasisPanelModel::fromMeasure() const
{
    const PracticeEmphasisData* emphasis = data();
    return emphasis ? emphasis->document().fromMeasure : 0;
}

int PracticeEmphasisPanelModel::toMeasure() const
{
    const PracticeEmphasisData* emphasis = data();
    return emphasis ? emphasis->document().toMeasure : 0;
}

QVariantList PracticeEmphasisPanelModel::parts() const
{
    MasterScore* master = currentMaster();
    const int staves = master ? static_cast<int>(master->nstaves()) : 2;
    QVariantList list;
    for (const PracticeEmphasisData::PartRule& part : PracticeEmphasisData::templatesForStaffCount(staves)) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), QString::fromStdString(part.id));
        item.insert(QStringLiteral("name"), QString::fromStdString(part.id + "  " + part.name));
        item.insert(QStringLiteral("selected"), master && master->practiceEmphasis().partSelected(part.id));
        item.insert(QStringLiteral("available"), master && part.staffIndex < staves);
        list.push_back(item);
    }
    return list;
}

bool PracticeEmphasisPanelModel::debugOverrideActive() const
{
    return PracticeEmphasis::debugOverrideActive();
}

void PracticeEmphasisPanelModel::setPlaybackActive(bool active)
{
    PracticeEmphasisData* emphasis = data();
    if (!emphasis) {
        return;
    }
    if (!active) {
        emphasis->setEmphasisMode(false);
    }
    emphasis->setPlaybackActive(active);
}

void PracticeEmphasisPanelModel::setEmphasisMode(bool enabled)
{
    PracticeEmphasisData* emphasis = data();
    if (!emphasis) {
        return;
    }
    if (enabled) {
        emphasis->setPlaybackActive(true);
    }
    emphasis->setEmphasisMode(enabled);
}

void PracticeEmphasisPanelModel::setEmphasizedDb(int db)
{
    PracticeEmphasisData* emphasis = data();
    if (!emphasis) {
        return;
    }
    emphasis->setGains(static_cast<float>(db), emphasis->document().otherDb);
}

void PracticeEmphasisPanelModel::setOtherDb(int db)
{
    PracticeEmphasisData* emphasis = data();
    if (!emphasis) {
        return;
    }
    emphasis->setGains(emphasis->document().emphasizedDb, static_cast<float>(db));
}

void PracticeEmphasisPanelModel::setFromMeasure(int measure)
{
    PracticeEmphasisData* emphasis = data();
    if (!emphasis) {
        return;
    }
    emphasis->setRange(measure, emphasis->document().toMeasure);
}

void PracticeEmphasisPanelModel::setToMeasure(int measure)
{
    PracticeEmphasisData* emphasis = data();
    if (!emphasis) {
        return;
    }
    emphasis->setRange(emphasis->document().fromMeasure, measure);
}

void PracticeEmphasisPanelModel::setPartSelected(const QString& partId, bool selected)
{
    MasterScore* master = currentMaster();
    if (!master) {
        return;
    }
    master->practiceEmphasis().setPartSelected(master, partId.toStdString(), selected);
}

void PracticeEmphasisPanelModel::useSelectionRange()
{
    if (!globalContext() || !globalContext()->currentNotation()) {
        return;
    }
    const INotationSelectionPtr selection = globalContext()->currentNotation()->interaction()->selection();
    if (!selection) {
        return;
    }
    const int from = displayedMeasureNumber(selection->startMeasureBase());
    int to = displayedMeasureNumber(selection->endMeasureBase());
    if (from <= 0) {
        return;
    }
    if (to <= 0) {
        to = from;
    }
    PracticeEmphasisData* emphasis = data();
    if (emphasis) {
        emphasis->setRange(from, to);
    }
}

void PracticeEmphasisPanelModel::clearOverrides()
{
    PracticeEmphasisData* emphasis = data();
    if (emphasis) {
        emphasis->clearOverrides();
    }
}
