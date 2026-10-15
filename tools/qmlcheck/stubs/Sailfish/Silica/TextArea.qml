import QtQuick 2.0
TextEdit { property bool labelVisible: true; property string placeholderText; property string label; color: "white"; wrapMode: TextEdit.Wrap; height: Math.max(contentHeight, 40) }
