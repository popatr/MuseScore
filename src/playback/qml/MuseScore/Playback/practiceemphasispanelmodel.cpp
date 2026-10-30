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
#include <unordered_set>

#include "translation.h"

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

std::vector<PracticeEmphasisData::PartRule> visibleParts(const MasterScore* master)
{
    const int staves = master ? static_cast<int>(master->nstaves()) : 2;
    std::vector<PracticeEmphasisData::PartRule> result = PracticeEmphasisData::templatesForStaffCount(staves);
    if (!master) {
        return result;
    }

    std::unordered_set<std::string> seen;
    for (PracticeEmphasisData::PartRule& part : result) {
        seen.insert(part.id);
        for (const PracticeEmphasisData::PartRule& existing : master->practiceEmphasis().document().parts) {
            if (existing.id == part.id) {
                part = existing;
                break;
            }
        }
    }
    for (const PracticeEmphasisData::PartRule& part : master->practiceEmphasis().document().parts) {
        if (seen.insert(part.id).second) {
            result.push_back(part);
        }
    }
    return result;
}

const PracticeEmphasisData::PartRule* findVisible(const std::vector<PracticeEmphasisData::PartRule>& parts, const std::string& id)
{
    for (const PracticeEmphasisData::PartRule& part : parts) {
        if (part.id == id) {
            return &part;
        }
    }
    return nullptr;
}

QString dropdownLabel(const PracticeEmphasisData::PartRule& part)
{
    if (part.builtin) {
        return QString::fromStdString(part.id + "  " + part.name);
    }
    if (!part.name.empty()) {
        return QString::fromStdString(part.name);
    }
    return QString::fromStdString(part.id);
}

bool isDescendantOf(const std::vector<PracticeEmphasisData::PartRule>& parts, const std::string& candidateId, const std::string& ancestorId)
{
    if (candidateId.empty() || ancestorId.empty() || candidateId == ancestorId) {
        return false;
    }
    std::unordered_set<std::string> seen;
    std::string current = candidateId;
    while (!current.empty() && seen.insert(current).second) {
        const PracticeEmphasisData::PartRule* part = findVisible(parts, current);
        if (!part) {
            break;
        }
        if (part->parentId == ancestorId) {
            return true;
        }
        current = part->parentId;
    }
    return false;
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
    for (const PracticeEmphasisData::PartRule& part : visibleParts(master)) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), QString::fromStdString(part.id));
        item.insert(QStringLiteral("label"), part.builtin ? QString::fromStdString(part.id)
                                                          : dropdownLabel(part));
        item.insert(QStringLiteral("selected"), master && master->practiceEmphasis().partSelected(part.id));
        item.insert(QStringLiteral("available"), master && (part.builtin ? part.staffIndex < staves : true));
        list.push_back(item);
    }
    return list;
}

QVariantList PracticeEmphasisPanelModel::editingPartOptions() const
{
    QVariantList list;
    for (const PracticeEmphasisData::PartRule& part : visibleParts(currentMaster())) {
        QVariantMap item;
        item.insert(QStringLiteral("text"), dropdownLabel(part));
        item.insert(QStringLiteral("value"), QString::fromStdString(part.id));
        list.push_back(item);
    }
    return list;
}

QString PracticeEmphasisPanelModel::editingPartId() const
{
    const PracticeEmphasisData* emphasis = data();
    return emphasis ? QString::fromStdString(emphasis->document().editingPartId) : QString();
}

QString PracticeEmphasisPanelModel::editingPartName() const
{
    const std::string id = editingPartId().toStdString();
    if (id.empty()) {
        return {};
    }
    if (const PracticeEmphasisData::PartRule* part = findVisible(visibleParts(currentMaster()), id)) {
        return QString::fromStdString(part->name);
    }
    return {};
}

bool PracticeEmphasisPanelModel::editingPartBuiltin() const
{
    const std::string id = editingPartId().toStdString();
    if (id.empty()) {
        return true;
    }
    if (const PracticeEmphasisData::PartRule* part = findVisible(visibleParts(currentMaster()), id)) {
        return part->builtin;
    }
    return true;
}

QString PracticeEmphasisPanelModel::editingPartParentId() const
{
    const std::string id = editingPartId().toStdString();
    if (id.empty()) {
        return {};
    }
    if (const PracticeEmphasisData::PartRule* part = findVisible(visibleParts(currentMaster()), id)) {
        return QString::fromStdString(part->parentId);
    }
    return {};
}

QVariantList PracticeEmphasisPanelModel::parentOptions() const
{
    QVariantList list;
    QVariantMap none;
        none.insert(QStringLiteral("text"), muse::qtrc("playback", "(none)"));
    none.insert(QStringLiteral("value"), QString());
    list.push_back(none);

    const std::string editingId = editingPartId().toStdString();
    const std::vector<PracticeEmphasisData::PartRule> parts = visibleParts(currentMaster());
    for (const PracticeEmphasisData::PartRule& part : parts) {
        if (part.id == editingId || isDescendantOf(parts, part.id, editingId)) {
            continue;
        }
        QVariantMap item;
        item.insert(QStringLiteral("text"), dropdownLabel(part));
        item.insert(QStringLiteral("value"), QString::fromStdString(part.id));
        list.push_back(item);
    }
    return list;
}

bool PracticeEmphasisPanelModel::canDeleteEditingPart() const
{
    return hasScore() && !editingPartBuiltin() && !editingPartId().isEmpty();
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
    MasterScore* master = currentMaster();
    if (!emphasis || !master) {
        return;
    }
    if (enabled) {
        emphasis->setPlaybackActive(true);
        if (emphasis->document().editingPartId.empty()) {
            emphasis->setEditingPartId(master, "S1");
        }
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

void PracticeEmphasisPanelModel::setEditingPartId(const QString& partId)
{
    MasterScore* master = currentMaster();
    if (!master) {
        return;
    }
    master->practiceEmphasis().setEditingPartId(master, partId.toStdString());
}

void PracticeEmphasisPanelModel::setEditingPartName(const QString& name)
{
    PracticeEmphasisData* emphasis = data();
    if (!emphasis) {
        return;
    }
    emphasis->setPartName(emphasis->document().editingPartId, name.toStdString());
}

void PracticeEmphasisPanelModel::setEditingPartParentId(const QString& parentId)
{
    MasterScore* master = currentMaster();
    PracticeEmphasisData* emphasis = data();
    if (!master || !emphasis) {
        return;
    }
    emphasis->setPartParent(master, emphasis->document().editingPartId, parentId.toStdString());
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

void PracticeEmphasisPanelModel::addCustomPart()
{
    MasterScore* master = currentMaster();
    PracticeEmphasisData* emphasis = data();
    if (!master || !emphasis) {
        return;
    }
    emphasis->addCustomPart(master, {}, emphasis->document().editingPartId);
}

void PracticeEmphasisPanelModel::deleteEditingPart()
{
    PracticeEmphasisData* emphasis = data();
    if (!emphasis) {
        return;
    }
    emphasis->deletePart(emphasis->document().editingPartId);
}

void PracticeEmphasisPanelModel::resetEditingPartEdits()
{
    PracticeEmphasisData* emphasis = data();
    if (!emphasis) {
        return;
    }
    emphasis->resetPartEdits(emphasis->document().editingPartId);
}
