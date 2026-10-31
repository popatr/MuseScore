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

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "rcommand/icommanddispatcher.h"

#include "context/iglobalcontext.h"

namespace mu::engraving {
class MasterScore;
class PracticeEmphasisData;
}

namespace mu::playback {
class PracticeEmphasisPanelModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(bool hasScore READ hasScore NOTIFY stateChanged)
    Q_PROPERTY(bool playbackActive READ playbackActive WRITE setPlaybackActive NOTIFY stateChanged)
    Q_PROPERTY(bool emphasisMode READ emphasisMode WRITE setEmphasisMode NOTIFY stateChanged)
    Q_PROPERTY(int emphasizedDb READ emphasizedDb WRITE setEmphasizedDb NOTIFY stateChanged)
    Q_PROPERTY(int otherDb READ otherDb WRITE setOtherDb NOTIFY stateChanged)
    Q_PROPERTY(int fromMeasure READ fromMeasure WRITE setFromMeasure NOTIFY stateChanged)
    Q_PROPERTY(int toMeasure READ toMeasure WRITE setToMeasure NOTIFY stateChanged)
    Q_PROPERTY(QVariantList parts READ parts NOTIFY stateChanged)
    Q_PROPERTY(QVariantList editingPartOptions READ editingPartOptions NOTIFY stateChanged)
    Q_PROPERTY(QString editingPartId READ editingPartId WRITE setEditingPartId NOTIFY stateChanged)
    Q_PROPERTY(QString editingPartName READ editingPartName NOTIFY stateChanged)
    Q_PROPERTY(bool editingPartBuiltin READ editingPartBuiltin NOTIFY stateChanged)
    Q_PROPERTY(QString editingPartParentId READ editingPartParentId NOTIFY stateChanged)
    Q_PROPERTY(bool solo READ solo WRITE setSolo NOTIFY stateChanged)
    Q_PROPERTY(QVariantList parentOptions READ parentOptions NOTIFY stateChanged)
    Q_PROPERTY(bool canDeleteEditingPart READ canDeleteEditingPart NOTIFY stateChanged)
    Q_PROPERTY(bool debugOverrideActive READ debugOverrideActive NOTIFY stateChanged)
    Q_PROPERTY(QString selectionButtonText READ selectionButtonText NOTIFY stateChanged)

    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<muse::rcommand::ICommandDispatcher> commandDispatcher = { this };

public:
    explicit PracticeEmphasisPanelModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE void setPartSelected(const QString& partId, bool selected);
    Q_INVOKABLE void useSelectionRange();
    Q_INVOKABLE void addCustomPart();
    Q_INVOKABLE void deleteEditingPart();
    Q_INVOKABLE void resetEditingPartEdits();
    Q_INVOKABLE void applyEditingPartName(const QString& partId, const QString& name);
    Q_INVOKABLE void applyEditingPartParentId(const QString& partId, const QString& parentId);
    Q_INVOKABLE void togglePanel();

    bool hasScore() const;
    bool playbackActive() const;
    bool emphasisMode() const;
    int emphasizedDb() const;
    int otherDb() const;
    int fromMeasure() const;
    int toMeasure() const;
    QVariantList parts() const;
    QVariantList editingPartOptions() const;
    QString editingPartId() const;
    QString editingPartName() const;
    bool editingPartBuiltin() const;
    QString editingPartParentId() const;
    bool solo() const;
    QVariantList parentOptions() const;
    bool canDeleteEditingPart() const;
    bool debugOverrideActive() const;
    QString selectionButtonText() const;

    void setPlaybackActive(bool active);
    void setEmphasisMode(bool enabled);
    void setEmphasizedDb(int db);
    void setOtherDb(int db);
    void setFromMeasure(int measure);
    void setToMeasure(int measure);
    void setEditingPartId(const QString& partId);
    void setSolo(bool enabled);

signals:
    void stateChanged();

private:
    engraving::MasterScore* currentMaster() const;
    engraving::PracticeEmphasisData* data() const;
    void connectScore();
    void refresh();

    bool m_loaded = false;
};
}
