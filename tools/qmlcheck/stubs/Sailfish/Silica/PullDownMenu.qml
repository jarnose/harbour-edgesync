import QtQuick 2.0
Item { property bool busy: false; default property alias items: col.data; Column { id: col; visible: false } }
