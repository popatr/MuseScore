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

import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

import MuseScore.Playback

StyledPopupView {
    id: root

    required property PracticeEmphasisPanelModel emphasisModel

    contentWidth: 440
    contentHeight: contentColumn.implicitHeight

    NavigationPanel {
        id: navPanel
        name: "PracticeEmphasisPopup"
        section: root.navigationSection
        direction: NavigationPanel.Vertical
        accessible.name: qsTrc("playback", "Practice emphasis")
    }

    ColumnLayout {
        id: contentColumn

        width: root.contentWidth
        spacing: 8

        StyledTextLabel {
            Layout.fillWidth: true
            text: qsTrc("playback", "Practice emphasis")
            horizontalAlignment: Text.AlignLeft
            font: ui.theme.bodyBoldFont
        }

        StyledTextLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            text: qsTrc("playback", "Checked parts are loud. A note both parts sing stays loud until every one of those parts is unchecked. Click a red or black note while highlight is on to force it off or on. This is not written into the notation.")
        }

        StyledTextLabel {
            Layout.fillWidth: true
            visible: root.emphasisModel.debugOverrideActive
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            text: qsTrc("playback", "MUSESCORE_PRACTICE_EMPHASIS is set, so it overrides this score until you quit and unset it.")
        }

        CheckBox {
            text: qsTrc("playback", "Use for playback and export")
            checked: root.emphasisModel.playbackActive
            enabled: root.emphasisModel.hasScore
            navigation.panel: navPanel
            navigation.order: 1
            onClicked: root.emphasisModel.playbackActive = !checked
        }

        CheckBox {
            text: qsTrc("playback", "Highlight notes and click to override")
            checked: root.emphasisModel.emphasisMode
            enabled: root.emphasisModel.hasScore
            navigation.panel: navPanel
            navigation.order: 2
            onClicked: root.emphasisModel.emphasisMode = !checked
        }

        RowLayout {
            spacing: 12

            StyledTextLabel {
                text: qsTrc("playback", "Emphasized")
            }

            IncrementalPropertyControl {
                Layout.preferredWidth: 88
                currentValue: root.emphasisModel.emphasizedDb
                minValue: -24
                maxValue: 24
                step: 1
                decimals: 0
                measureUnitsSymbol: qsTrc("playback", "dB")
                enabled: root.emphasisModel.hasScore
                navigation.panel: navPanel
                navigation.order: 3
                onValueEdited: function(newValue) {
                    root.emphasisModel.emphasizedDb = newValue
                }
            }

            StyledTextLabel {
                text: qsTrc("playback", "Others")
            }

            IncrementalPropertyControl {
                Layout.preferredWidth: 88
                currentValue: root.emphasisModel.otherDb
                minValue: -40
                maxValue: 12
                step: 1
                decimals: 0
                measureUnitsSymbol: qsTrc("playback", "dB")
                enabled: root.emphasisModel.hasScore
                navigation.panel: navPanel
                navigation.order: 4
                onValueEdited: function(newValue) {
                    root.emphasisModel.otherDb = newValue
                }
            }
        }

        RowLayout {
            spacing: 12

            StyledTextLabel {
                text: qsTrc("playback", "Measures")
            }

            IncrementalPropertyControl {
                Layout.preferredWidth: 72
                currentValue: root.emphasisModel.fromMeasure
                minValue: 0
                maxValue: 999
                step: 1
                decimals: 0
                enabled: root.emphasisModel.hasScore
                navigation.panel: navPanel
                navigation.order: 5
                onValueEdited: function(newValue) {
                    root.emphasisModel.fromMeasure = newValue
                }
            }

            StyledTextLabel {
                text: qsTrc("playback", "to")
            }

            IncrementalPropertyControl {
                Layout.preferredWidth: 72
                currentValue: root.emphasisModel.toMeasure
                minValue: 0
                maxValue: 999
                step: 1
                decimals: 0
                enabled: root.emphasisModel.hasScore
                navigation.panel: navPanel
                navigation.order: 6
                onValueEdited: function(newValue) {
                    root.emphasisModel.toMeasure = newValue
                }
            }

            FlatButton {
                text: qsTrc("playback", "Selection")
                enabled: root.emphasisModel.hasScore
                navigation.panel: navPanel
                navigation.order: 7
                onClicked: root.emphasisModel.useSelectionRange()
            }

            FlatButton {
                text: qsTrc("playback", "All")
                enabled: root.emphasisModel.hasScore
                navigation.panel: navPanel
                navigation.order: 8
                onClicked: {
                    root.emphasisModel.fromMeasure = 0
                    root.emphasisModel.toMeasure = 0
                }
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            text: qsTrc("playback", "0 means the whole score. On a closed SATB score, S and A are the upper staff and T and B the lower. The 1st part is the top note of a chord; the 2nd part is every note below it. A chord of one note belongs to both. Three or more notes are not split into a middle part.")
        }

        Flow {
            Layout.fillWidth: true
            spacing: 4

            Repeater {
                model: root.emphasisModel.parts

                CheckBox {
                    required property var modelData
                    text: modelData.name
                    checked: modelData.selected
                    enabled: modelData.available
                    onClicked: root.emphasisModel.setPartSelected(modelData.id, !checked)
                }
            }
        }

        FlatButton {
            text: qsTrc("playback", "Clear note overrides")
            enabled: root.emphasisModel.hasScore
            navigation.panel: navPanel
            navigation.order: 9
            onClicked: root.emphasisModel.clearOverrides()
        }
    }
}
