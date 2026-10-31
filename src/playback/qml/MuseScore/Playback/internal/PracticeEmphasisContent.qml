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

Item {
    id: root

    required property PracticeEmphasisPanelModel emphasisModel
    property NavigationPanel navigationPanel: null
    property int navigationOrderStart: 1
    property bool showTitle: true

    readonly property bool narrow: width > 0 && width < 480

    implicitWidth: 560
    implicitHeight: contentColumn.implicitHeight

    ColumnLayout {
        id: contentColumn

        width: root.width
        spacing: 8

        StyledTextLabel {
            Layout.fillWidth: true
            visible: root.showTitle
            text: qsTrc("playback", "Practice emphasis")
            horizontalAlignment: Text.AlignLeft
            font: ui.theme.bodyBoldFont
        }

        StyledTextLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            text: qsTrc("playback", "Checked parts are loud. A shared note stays loud until every checked part that sings it is cleared. Highlight is red for the part being edited; other loud notes are faded. Click a note to add or remove it on that part only.")
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
            navigation.panel: root.navigationPanel
            navigation.order: root.navigationOrderStart
            onClicked: root.emphasisModel.playbackActive = !checked
        }

        CheckBox {
            text: qsTrc("playback", "Highlight notes and click to edit")
            checked: root.emphasisModel.emphasisMode
            enabled: root.emphasisModel.hasScore
            toolTipTitle: qsTrc("playback", "Highlight notes and click to edit")
            toolTipDescription: qsTrc("playback", "Color the edited part red. Click a note to add or remove it on that part only.")
            navigation.panel: root.navigationPanel
            navigation.order: root.navigationOrderStart + 1
            onClicked: root.emphasisModel.emphasisMode = !checked
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.narrow ? 2 : 4
            columnSpacing: 12
            rowSpacing: 8

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
                toolTipTitle: qsTrc("playback", "Emphasized")
                toolTipDescription: qsTrc("playback", "Gain in decibels for notes that belong to a checked part.")
                navigation.panel: root.navigationPanel
                navigation.order: root.navigationOrderStart + 2
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
                toolTipTitle: qsTrc("playback", "Others")
                toolTipDescription: qsTrc("playback", "Gain in decibels for notes that do not belong to any checked part.")
                navigation.panel: root.navigationPanel
                navigation.order: root.navigationOrderStart + 3
                onValueEdited: function(newValue) {
                    root.emphasisModel.otherDb = newValue
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.narrow ? 2 : 4
            columnSpacing: 12
            rowSpacing: 8

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
                navigation.panel: root.navigationPanel
                navigation.order: root.navigationOrderStart + 4
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
                navigation.panel: root.navigationPanel
                navigation.order: root.navigationOrderStart + 5
                onValueEdited: function(newValue) {
                    root.emphasisModel.toMeasure = newValue
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.narrow ? 1 : 2
            columnSpacing: 8
            rowSpacing: 8

            FlatButton {
                Layout.fillWidth: true
                text: qsTrc("playback", "Selection")
                enabled: root.emphasisModel.hasScore
                toolTipTitle: qsTrc("playback", "Selection")
                toolTipDescription: qsTrc("playback", "Set the measure range from the current score selection.")
                navigation.panel: root.navigationPanel
                navigation.order: root.navigationOrderStart + 6
                onClicked: root.emphasisModel.useSelectionRange()
            }

            FlatButton {
                Layout.fillWidth: true
                text: qsTrc("playback", "All")
                enabled: root.emphasisModel.hasScore
                toolTipTitle: qsTrc("playback", "All")
                toolTipDescription: qsTrc("playback", "Clear the measure range so emphasis uses the whole score.")
                navigation.panel: root.navigationPanel
                navigation.order: root.navigationOrderStart + 7
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
            text: qsTrc("playback", "0 is the whole score. Closed SATB: S/A upper staff, T/B lower. 1st part = top of a chord; 2nd = notes below. One note belongs to both.")
        }

        Flow {
            Layout.fillWidth: true
            spacing: 8

            Repeater {
                model: root.emphasisModel.parts

                CheckBox {
                    required property var modelData
                    text: modelData.label
                    checked: modelData.selected
                    enabled: modelData.available
                    toolTipTitle: modelData.label
                    toolTipDescription: modelData.tooltip
                    onClicked: root.emphasisModel.setPartSelected(modelData.id, !checked)
                }
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            text: qsTrc("playback", "Editing")
            horizontalAlignment: Text.AlignLeft
            font: ui.theme.bodyBoldFont
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.narrow ? 1 : 3
            columnSpacing: 8
            rowSpacing: 8

            StyledDropdown {
                id: editingDropdown
                Layout.fillWidth: true
                enabled: root.emphasisModel.hasScore
                model: root.emphasisModel.editingPartOptions
                currentIndex: {
                    var index = indexOfValue(root.emphasisModel.editingPartId)
                    return index >= 0 ? index : 0
                }
                toolTipTitle: qsTrc("playback", "Editing")
                toolTipDescription: qsTrc("playback", "Clicks in highlight mode add or remove notes on this part only.")
                navigation.panel: root.navigationPanel
                navigation.order: root.navigationOrderStart + 8
                navigation.name: "EditingPartDropdown"
                onActivated: function(index, value) {
                    root.emphasisModel.editingPartId = value
                }
            }

            FlatButton {
                Layout.fillWidth: root.narrow
                text: qsTrc("playback", "New")
                enabled: root.emphasisModel.hasScore
                toolTipTitle: qsTrc("playback", "New")
                toolTipDescription: qsTrc("playback", "Create a custom part. Its parent starts as the part you were editing.")
                navigation.panel: root.navigationPanel
                navigation.order: root.navigationOrderStart + 9
                onClicked: root.emphasisModel.addCustomPart()
            }

            FlatButton {
                Layout.fillWidth: root.narrow
                text: qsTrc("playback", "Delete")
                enabled: root.emphasisModel.canDeleteEditingPart
                toolTipTitle: qsTrc("playback", "Delete")
                toolTipDescription: qsTrc("playback", "Delete this custom part. Built-in SATB parts cannot be deleted.")
                navigation.panel: root.navigationPanel
                navigation.order: root.navigationOrderStart + 10
                onClicked: root.emphasisModel.deleteEditingPart()
            }
        }

        Repeater {
            model: (root.emphasisModel.hasScore && !root.emphasisModel.editingPartBuiltin)
                   ? [root.emphasisModel.editingPartId] : []

            delegate: ColumnLayout {
                id: customPartEditors

                required property string modelData

                Layout.fillWidth: true
                spacing: 8

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.narrow ? 1 : 2
                    columnSpacing: 8
                    rowSpacing: 8

                    StyledTextLabel {
                        text: qsTrc("playback", "Name")
                    }

                    TextInputField {
                        Layout.fillWidth: true
                        currentText: root.emphasisModel.editingPartName
                        navigation.panel: root.navigationPanel
                        navigation.order: root.navigationOrderStart + 11
                        onTextEditingFinished: function(newTextValue) {
                            root.emphasisModel.applyEditingPartName(customPartEditors.modelData, newTextValue)
                        }
                    }

                    StyledTextLabel {
                        text: qsTrc("playback", "Parent")
                    }

                    StyledDropdown {
                        Layout.fillWidth: true
                        model: root.emphasisModel.parentOptions
                        currentIndex: {
                            var index = indexOfValue(root.emphasisModel.editingPartParentId)
                            return index >= 0 ? index : 0
                        }
                        toolTipTitle: qsTrc("playback", "Parent")
                        toolTipDescription: qsTrc("playback", "Notes this part inherits before its own added and removed clicks.")
                        navigation.panel: root.navigationPanel
                        navigation.order: root.navigationOrderStart + 12
                        navigation.name: "ParentPartDropdown"
                        onActivated: function(index, value) {
                            root.emphasisModel.applyEditingPartParentId(customPartEditors.modelData, value)
                        }
                    }
                }
            }
        }

        FlatButton {
            id: resetButton
            Layout.fillWidth: true
            Layout.minimumWidth: implicitWidth
            text: qsTrc("playback", "Reset this part’s edits")
            enabled: root.emphasisModel.hasScore
            toolTipTitle: qsTrc("playback", "Reset this part’s edits")
            toolTipDescription: qsTrc("playback", "Remove only this part’s added and removed clicks. Notes from the rule or parent come back.")
            navigation.panel: root.navigationPanel
            navigation.order: root.navigationOrderStart + 13
            onClicked: root.emphasisModel.resetEditingPartEdits()
        }
    }
}
