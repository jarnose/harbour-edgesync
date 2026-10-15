import QtQuick 2.0
TextInput { property string label; property string placeholderText; property int inputMethodHints: 0; color: "white"; echoMode: TextInput.Password; width: parent ? parent.width : 200; height: 60 }
