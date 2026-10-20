// Sound2Light Multitrack
// Multitrack extension (c) 2026 - released under the MIT license.

import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Layouts 1.2

import "style"  // import all files in style dir

// ------------- Track bar: one tile per track, click to edit the track -------------
Rectangle {
	id: root
	height: 150
	color: "#1f1f1f"

	// incremented to refresh the meters (bindings depend on it)
	property int tick: 0
	property var channelPeaks: []
	// incremented when names / channels changed in C++
	property int namesRev: 0
	property int channelsRev: 0

	// band colors, same as in the trigger settings:
	readonly property var bandColors: [Qt.rgba(1, 0.1, 0.1, 1), Qt.rgba(1, 0.8, 0.1, 1),
		Qt.rgba(0.1, 1.0, 0.1, 1), Qt.rgba(0.2, 0.4, 1, 1), Qt.rgba(0.55, 0.6, 1, 1), Qt.rgba(0.6, 0.6, 0.6, 1)]
	readonly property var bandLabels: ["B", "LM", "HM", "H", "Lv", "S"]

	Timer {
		interval: 50  // 20 Hz meter refresh
		running: root.visible
		repeat: true
		onTriggered: {
			root.channelPeaks = controller.getChannelPeaks()
			root.tick++
		}
	}

	Connections {
		target: controller
		onTrackNamesChanged: root.namesRev++
		onTrackChannelsChanged: root.channelsRev++
		onInputChanged: root.channelsRev++
	}

	// ---------------------------- Header ----------------------------
	RowLayout {
		id: header
		anchors.top: parent.top
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.margins: 6
		height: 24
		spacing: 10

		GreyText {
			text: "PISTES"
			font.bold: true
			font.letterSpacing: 2
		}

		DarkSpinBox {
			id: trackCountSpin
			minimumValue: 1
			maximumValue: controller.maxTracks
			value: controller.trackCount
			implicitWidth: 64
			Layout.preferredHeight: 22
			onValueChanged: if (value !== controller.trackCount) controller.trackCount = value
			Connections {
				target: controller
				onTrackCountChanged: trackCountSpin.value = controller.trackCount
			}
		}

		GreyText {
			text: controller.inputInfo
			color: controller.inputChannelCount > 0 ? "#888" : "#FF6633"
			elide: Text.ElideRight
			Layout.fillWidth: true
		}

		GreyText {
			text: "MULTIPISTE"
			font.bold: true
			font.letterSpacing: 3
			color: "#FF6633"
		}

		DarkCheckBox {
			text: "OSC multipiste (/s2l/chN/…)"
			checked: controller.multitrackOscEnabled
			onClicked: controller.multitrackOscEnabled = checked
		}
	}

	// ---------------------------- Tiles ----------------------------
	Flickable {
		id: flick
		anchors.top: header.bottom
		anchors.topMargin: 4
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.bottom: parent.bottom
		anchors.leftMargin: 6
		anchors.rightMargin: 6
		anchors.bottomMargin: 6
		contentWidth: tileRow.width
		contentHeight: height
		flickableDirection: Flickable.HorizontalFlick
		boundsBehavior: Flickable.StopAtBounds
		clip: true

		Row {
			id: tileRow
			height: flick.height
			spacing: 6

			Repeater {
				model: controller.trackCount

				delegate: Rectangle {
					id: tile
					property int trackIndex: index
					property bool selected: controller.currentTrack === trackIndex
					property var levels: { root.tick; return controller.getTrackLevels(trackIndex) }
					property var actives: { root.tick; return controller.getTrackActives(trackIndex) }
					property int channel: { root.channelsRev; return controller.getTrackChannel(trackIndex) }
					property real inputPeak: {
						if (channel < 0) {
							var m = 0
							for (var i=0; i<root.channelPeaks.length; ++i) m = Math.max(m, root.channelPeaks[i])
							return m
						}
						return channel < root.channelPeaks.length ? root.channelPeaks[channel] : 0
					}

					width: 176
					height: tileRow.height
					radius: 4
					color: selected ? "#3a3a3a" : "#2a2a2a"
					border.width: selected ? 2 : 1
					border.color: selected ? "#FF6633" : "#444"

					// click anywhere on the tile to select the track:
					MouseArea {
						anchors.fill: parent
						onClicked: controller.currentTrack = tile.trackIndex
						onDoubleClicked: {
							controller.currentTrack = tile.trackIndex
							nameEdit.text = controller.getTrackName(tile.trackIndex)
							nameEdit.visible = true
							nameEdit.forceActiveFocus()
							nameEdit.selectAll()
						}
					}

					// ---- name ----
					GreyText {
						id: nameText
						anchors.top: parent.top
						anchors.left: parent.left
						anchors.right: bpmText.left
						anchors.margins: 5
						text: { root.namesRev; return (tile.trackIndex + 1) + " · " + controller.getTrackName(tile.trackIndex) }
						font.bold: tile.selected
						color: tile.selected ? "#FFFFFF" : "#b5b7ba"
						elide: Text.ElideRight
						visible: !nameEdit.visible
					}
					DarkTextField {
						id: nameEdit
						visible: false
						anchors.top: parent.top
						anchors.left: parent.left
						anchors.right: parent.right
						anchors.margins: 3
						height: 20
						onAccepted: {
							controller.setTrackName(tile.trackIndex, text)
							visible = false
						}
						onActiveFocusChanged: if (!activeFocus) visible = false
						Keys.onEscapePressed: visible = false
					}
					GreyText {
						id: bpmText
						anchors.top: parent.top
						anchors.right: parent.right
						anchors.margins: 5
						font.pointSize: 8
						color: "#888"
						text: { root.tick; var b = controller.getTrackBpm(tile.trackIndex); return b > 0 ? b + " bpm" : "" }
					}

					// ---- input channel ----
					DarkComboBox {
						id: channelCombo
						anchors.top: nameText.bottom
						anchors.topMargin: 4
						anchors.left: parent.left
						anchors.leftMargin: 5
						width: 92
						height: 20
						model: {
							root.channelsRev
							var n = Math.max(controller.inputChannelCount, tile.channel + 1)
							var m = ["Mix"]
							for (var i=0; i<n; ++i) {
								m.push("Entrée " + (i + 1) + (i >= controller.inputChannelCount ? " (absente)" : ""))
							}
							return m
						}
						// the ComboBox resets its index when the model changes,
						// so the wanted index is re-applied after every model / channel change:
						property int wantedIndex: tile.channel + 1
						function applyWanted() { if (currentIndex !== wantedIndex) currentIndex = wantedIndex }
						Component.onCompleted: applyWanted()
						onWantedIndexChanged: applyWanted()
						onModelChanged: Qt.callLater(applyWanted)
						onCountChanged: Qt.callLater(applyWanted)
						onActivated: controller.setTrackChannel(tile.trackIndex, index - 1)
					}

					// ---- input peak meter ----
					Rectangle {
						id: peakBg
						anchors.left: channelCombo.right
						anchors.leftMargin: 6
						anchors.right: parent.right
						anchors.rightMargin: 6
						anchors.verticalCenter: channelCombo.verticalCenter
						height: 6
						radius: 2
						color: "#151515"
						Rectangle {
							anchors.left: parent.left
							anchors.top: parent.top
							anchors.bottom: parent.bottom
							radius: 2
							width: parent.width * Math.min(1, tile.inputPeak)
							color: tile.inputPeak > 0.98 ? "#FF6633" : "#8fd18f"
						}
					}

					// ---- 6 band meters ----
					Row {
						anchors.left: parent.left
						anchors.right: parent.right
						anchors.bottom: parent.bottom
						anchors.top: channelCombo.bottom
						anchors.margins: 5
						anchors.topMargin: 6
						spacing: 3

						Repeater {
							model: 6
							delegate: Item {
								width: (tile.width - 10 - 5*3) / 6
								height: parent.height
								// level bar:
								Rectangle {
									anchors.bottom: bandLabel.top
									anchors.bottomMargin: 2
									anchors.horizontalCenter: parent.horizontalCenter
									width: parent.width
									height: Math.max(1, (parent.height - bandLabel.height - 2) * Math.min(1, tile.levels.length > index ? tile.levels[index] : 0))
									color: root.bandColors[index]
									opacity: (tile.actives.length > index && tile.actives[index]) ? 1.0 : 0.45
								}
								// trigger indicator:
								Rectangle {
									anchors.top: parent.top
									anchors.horizontalCenter: parent.horizontalCenter
									width: 6; height: 6; radius: 3
									color: (tile.actives.length > index && tile.actives[index]) ? "lightgreen" : "#444"
								}
								GreyText {
									id: bandLabel
									anchors.bottom: parent.bottom
									anchors.horizontalCenter: parent.horizontalCenter
									text: root.bandLabels[index]
									font.pointSize: 7
									color: "#777"
								}
							}
						}
					}
				}
			}
		}
	}
}
