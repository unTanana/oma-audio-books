import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    objectName: "mainWindow"
    width: library.preference("width", 1100); height: library.preference("height", 800)
    minimumWidth: 820; minimumHeight: 620
    visible: true; title: "oma-audio-books"
    color: theme.background
    font.family: theme.family
    palette.window: theme.background
    palette.active.windowText: theme.foreground
    palette.inactive.windowText: theme.foreground
    palette.base: theme.surface
    palette.alternateBase: theme.background
    palette.active.text: theme.foreground
    palette.inactive.text: theme.foreground
    palette.placeholderText: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.65)
    palette.button: theme.surface
    palette.active.buttonText: theme.foreground
    palette.inactive.buttonText: theme.foreground
    palette.highlight: theme.accent
    palette.highlightedText: theme.background
    palette.light: Qt.tint(theme.surface, Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.1))
    palette.midlight: Qt.tint(theme.surface, Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.18))
    palette.mid: theme.accent
    palette.dark: theme.accent
    palette.brightText: theme.background
    palette.toolTipBase: theme.surface
    palette.toolTipText: theme.foreground
    readonly property color disabledText: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.45)
    palette.disabled.text: disabledText
    palette.disabled.windowText: disabledText
    palette.disabled.buttonText: disabledText
    property int selectedBook: 0
    onSelectedBookChanged: if (selectedBook) Qt.callLater(function() { chapterList.currentIndex = -1; chapterList.positionViewAtBeginning() })
    property int focusedBook: 0
    property bool modelResetting: false
    readonly property bool modalOpen: bookMenu.visible || restartDialog.visible || settingsDialog.visible || editor.visible || bookmarkDialog.visible || folder.visible || coverFile.visible || relinkFile.visible || relinkFolder.visible || appearance.popup.visible || filter.popup.visible || sort.popup.visible || speedSelect.popup.visible || sleepSelect.popup.visible
    readonly property bool libraryShortcutsEnabled: selectedBook === 0 && !modalOpen && !(activeFocusItem instanceof TextInput) && !(activeFocusItem instanceof TextEdit)
    property int coverSize: library.preference("coverSize", 180)
    property int detailRevision: 0
    property int chapterRevision: 0
    property int bookmarkRevision: 0
    property var detail: { detailRevision; return library.book(selectedBook) }
    function time(ms) { if (ms < 0) return "Unknown"; let s = Math.floor(ms / 1000); return Math.floor(s / 3600) + ":" + String(Math.floor(s / 60) % 60).padStart(2, "0") + ":" + String(s % 60).padStart(2, "0") }
    function openDetails(id) { focusedBook = id; selectedBook = id; back.forceActiveFocus() }
    function backToGrid() { selectedBook = 0; grid.forceActiveFocus() }
    function chooseFilter(index) { filter.currentIndex = index; filter.activated(index); grid.forceActiveFocus() }
    function chapter(track, offset) { player.open(selectedBook, false); if (player.bookId === selectedBook) player.jump(track, offset) }
    function playText(book) { return player.bookId === book.id && player.playing ? "Pause" : book.finished ? "Play again" : book.progress > 0 ? "Resume" : "Play" }
    function playBook(book) {
        if (player.bookId === book.id && player.playing) player.pause()
        else if (book.available) { if (book.finished) player.startOver(book.id); else player.open(book.id) }
    }
    function editBook(id) { editor.bookId = id; editor.open() }
    function relinkBook(id) {
        let picker = library.book(id).identity.toLowerCase().endsWith(".m4b") ? relinkFile : relinkFolder
        picker.bookId = id; picker.open()
    }
    function showBookMenu(id, item, position) {
        grid.currentIndex = library.visibleIndex(id); focusedBook = id; grid.forceActiveFocus()
        bookMenu.bookId = id
        bookMenu.popup(window.contentItem, item.mapToItem(window.contentItem, position))
        bookMenu.currentIndex = 0
    }
    Component.onCompleted: {
        library.query(search.text, filter.currentIndex, sort.currentIndex)
        grid.currentIndex = 0; focusedBook = grid.currentBook ? grid.currentBook.id : 0
    }
    onClosing: function(close) {
        close.accepted = player.persist()
        if (close.accepted) { library.setPreference("width", width); library.setPreference("height", height); library.setPreference("coverSize", coverSize) }
    }
    component PlainLabel: Label { textFormat: Text.PlainText }
    component PlainButton: Button {
        Keys.onReturnPressed: if (enabled) clicked()
        Keys.onEnterPressed: if (enabled) clicked()
        contentItem: Text { text: parent.text; textFormat: Text.PlainText; color: parent.palette.buttonText; font: parent.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
    }
    component ThemedComboBox: ComboBox {
        id: control
        implicitContentWidthPolicy: ComboBox.WidestText
        palette.buttonText: down ? window.palette.highlightedText : window.palette.buttonText
        palette.dark: down ? window.palette.highlightedText : window.palette.dark
        delegate: ItemDelegate {
            required property var model
            required property int index
            width: ListView.view.width
            text: model[control.textRole]
            palette: control.palette
            font.weight: control.currentIndex === index ? Font.DemiBold : Font.Normal
            highlighted: control.highlightedIndex === index
            hoverEnabled: control.hoverEnabled
            background: Rectangle { color: parent.highlighted ? control.palette.highlight : control.palette.window }
        }
    }
    FolderDialog {
        id: folder; title: "Choose your audiobook library"
        currentFolder: suggestedFolder
        onAccepted: library.addRoot(selectedFolder)
    }
    FileDialog { id: coverFile; property int bookId: 0; title: "Choose a local cover image"; nameFilters: ["Images (*.png *.jpg *.jpeg *.webp)"]; onAccepted: library.setCover(bookId, selectedFile) }
    FileDialog { id: relinkFile; property int bookId: 0; title: "Relink the same relocated M4B"; nameFilters: ["Audiobook (*.m4b *.M4B)"]; onAccepted: library.relink(bookId, selectedFile) }
    FolderDialog { id: relinkFolder; property int bookId: 0; title: "Relink the same relocated MP3 book folder"; onAccepted: library.relink(bookId, selectedFolder) }
    Menu {
        id: bookMenu; objectName: "bookMenu"; parent: window.contentItem; popupType: Popup.Item
        property int bookId: 0
        readonly property var book: { library.revision; return library.book(bookId) }
        onClosed: if (!modalOpen && selectedBook === 0) grid.forceActiveFocus()
        MenuItem { objectName: "menuPlay"; text: playText(bookMenu.book); enabled: !!bookMenu.book.available || (player.bookId === bookMenu.bookId && player.playing); onTriggered: playBook(bookMenu.book) }
        MenuItem { objectName: "menuFavorite"; text: bookMenu.book.favorite ? "Remove from favorites" : "Add to favorites"; onTriggered: library.setFlag(bookMenu.bookId, "favorite", !bookMenu.book.favorite) }
        MenuItem { objectName: "menuFinished"; text: bookMenu.book.finished ? "Mark unfinished" : "Mark finished"; onTriggered: library.setFlag(bookMenu.bookId, "finished", !bookMenu.book.finished) }
        MenuSeparator {}
        MenuItem { text: "View details"; onTriggered: openDetails(bookMenu.bookId) }
        MenuItem { text: "Edit details…"; onTriggered: editBook(bookMenu.bookId) }
        MenuItem { text: "Open containing folder"; onTriggered: library.openFolder(bookMenu.bookId) }
        MenuItem { objectName: "menuRelink"; text: "Relink…"; visible: !bookMenu.book.available; height: visible ? implicitHeight : 0; enabled: visible && !library.scanning; onTriggered: relinkBook(bookMenu.bookId) }
    }
    Dialog {
        id: restartDialog; objectName: "restartDialog"; title: "Start over?"; modal: true
        property int bookId: 0
        anchors.centerIn: parent; width: Math.min(440, window.width - 40)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: player.startOver(bookId)
        PlainLabel { width: parent.width; wrapMode: Text.Wrap; text: "Replace the saved position and play from the beginning? Your bookmarks and playback speed will be kept." }
    }
    Dialog {
        id: settingsDialog; title: "Settings"; modal: true
        anchors.centerIn: parent; width: Math.min(680, window.width - 40); standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            spacing: 12
            PlainLabel { text: "Library folders"; font.bold: true }
            PlainLabel { text: "Removing a folder keeps your books and personal state. Re-add it to reconnect."; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Repeater {
                model: library.roots
                RowLayout {
                    required property string modelData
                    PlainLabel { text: modelData; elide: Text.ElideMiddle; Layout.fillWidth: true; ToolTip { visible: rootMouse.containsMouse; contentItem: PlainLabel { text: modelData } } MouseArea { id: rootMouse; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton } }
                    Button { text: "Remove"; enabled: !library.scanning; onClicked: library.removeRoot(parent.modelData); Accessible.name: "Remove library folder " + parent.modelData }
                }
            }
            Button { text: "Add folder…"; enabled: !library.scanning; onClicked: folder.open() }
            RowLayout {
                PlainLabel { text: "Appearance" }
                ThemedComboBox { id: appearance; model: ["Follow Omarchy", "Dark", "Light"]; currentIndex: model.indexOf(theme.mode); onActivated: theme.mode = currentText; Accessible.name: "Appearance" }
            }
            CheckBox {
                id: seriesHeadings; objectName: "seriesHeadingsToggle"
                text: "Show series headings"
                checked: String(library.preference("seriesHeadings", true)) === "true"
                onToggled: library.setPreference("seriesHeadings", checked)
            }
            PlainLabel { text: "Audio stays local. Close the window to save and exit."; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }
    Dialog {
        id: editor; objectName: "metadataEditor"; title: "Correct book details"; modal: true
        property int bookId: 0
        anchors.centerIn: parent; width: Math.min(650, window.width - 40); height: Math.min(580, window.height - 40)
        standardButtons: Dialog.Save | Dialog.Cancel
        onAboutToShow: { let book = library.book(bookId); eTitle.text = book.title || ""; eAuthor.text = book.author || ""; eNarrator.text = book.narrator || ""; eSeries.text = book.series || ""; eOrder.text = book.seriesOrder || ""; eDescription.text = book.description || "" }
        onAccepted: library.edit(bookId, {title: eTitle.text, author: eAuthor.text, narrator: eNarrator.text, series: eSeries.text, seriesOrder: eOrder.text, description: eDescription.text})
        contentItem: ScrollView {
            clip: true
            ColumnLayout {
                width: editor.availableWidth; spacing: 8
                PlainLabel { text: "Title" }
                TextField { id: eTitle; objectName: "metadataTitle"; Layout.fillWidth: true; Accessible.name: "Title" }
                PlainLabel { text: "Author" }
                TextField { id: eAuthor; Layout.fillWidth: true; Accessible.name: "Author" }
                PlainLabel { text: "Narrator" }
                TextField { id: eNarrator; Layout.fillWidth: true; Accessible.name: "Narrator" }
                PlainLabel { text: "Series and order" }
                RowLayout {
                    TextField { id: eSeries; Layout.fillWidth: true; Accessible.name: "Series" }
                    TextField { id: eOrder; Layout.preferredWidth: 80; Accessible.name: "Series order" }
                }
                PlainLabel { text: "Description" }
                TextArea { id: eDescription; objectName: "metadataDescription"; textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap; Layout.fillWidth: true; Layout.preferredHeight: 120; Accessible.name: "Description" }
            }
        }
    }
    Dialog {
        id: bookmarkDialog; title: "Bookmark current position"; modal: true
        anchors.centerIn: parent; width: 420; standardButtons: Dialog.Save | Dialog.Cancel
        onOpened: { bookmarkName.text = ""; bookmarkName.forceActiveFocus() }
        onAccepted: player.bookmark(bookmarkName.text)
        TextField { id: bookmarkName; width: parent.width; placeholderText: "Name this moment"; Accessible.name: "Bookmark name"; onAccepted: bookmarkDialog.accept() }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 10
        RowLayout {
            PlainLabel { text: "Audiobooks"; font.pixelSize: 26; font.bold: true }
            TextField { id: search; objectName: "search"; placeholderText: "Search library"; Layout.fillWidth: true; onTextChanged: library.query(text, filter.currentIndex, sort.currentIndex); Accessible.name: "Search title, author, narrator or series" }
            Button { text: library.scanning ? "Cancel scan" : "Refresh"; onClicked: library.scanning ? library.cancelScan() : library.refresh() }
            Button { objectName: "settingsButton"; text: "Settings"; onClicked: settingsDialog.open() }
        }
        ScrollView {
            id: errorScroll; contentWidth: availableWidth
            visible: library.error.length > 0; Layout.fillWidth: true; Layout.preferredHeight: Math.min(errorText.implicitHeight + 8, 90); clip: true
            PlainLabel { id: errorText; width: errorScroll.availableWidth; text: library.error; wrapMode: Text.Wrap; color: theme.foreground }
        }
        RowLayout {
            visible: selectedBook === 0
            ThemedComboBox { id: filter; objectName: "filter"; model: ["All · Ctrl+1", "In progress · Ctrl+2", "Finished · Ctrl+3", "Favorites · Ctrl+4"]; onActivated: library.query(search.text, currentIndex, sort.currentIndex); Accessible.name: "Filter books" }
            ThemedComboBox {
                id: sort; objectName: "sort"; model: ["Title", "Author", "Recently added", "Recently listened", "Series"]
                currentIndex: library.preference("sort", 4)
                onActivated: {
                    library.setPreference("sort", currentIndex)
                    library.query(search.text, filter.currentIndex, currentIndex)
                    Qt.callLater(function() { grid.positionViewAtIndex(grid.currentIndex, ListView.Contain) })
                }
                Accessible.name: "Sort books (Alt+S)"
                ToolTip.visible: hovered; ToolTip.text: "Sort books (Alt+S). Choose with arrows and Enter."
            }
            PlainLabel { text: library.scanning ? "Scanning…" : grid.count + " books"; Layout.fillWidth: true }
            PlainLabel { text: "Cover size" }
            Slider { from: 130; to: 260; stepSize: 10; value: coverSize; Layout.preferredWidth: 110; onMoved: coverSize = value; Accessible.name: "Cover size" }
        }
        FocusScope {
            id: grid; objectName: "grid"
            visible: selectedBook === 0
            Layout.fillWidth: true; Layout.fillHeight: true
            focus: true; activeFocusOnTab: true
            readonly property int cellWidth: coverSize + 20
            readonly property int cellHeight: coverSize + 112
            readonly property int columns: Math.max(1, Math.floor(width / cellWidth))
            property int modelRevision: 0
            readonly property var bookRows: { modelRevision; return library.bookRows(columns, seriesHeadings.checked) }
            readonly property int count: bookRows.reduce((total, row) => total + row.books.length, 0)
            property int currentIndex: 0
            readonly property int currentRow: rowIndex(currentIndex)
            readonly property var currentBook: currentRow < 0 ? null : bookRows[currentRow].books[currentIndex - bookRows[currentRow].startIndex]
            readonly property var currentItem: rows.currentItem ? rows.currentItem.currentCard : null
            property alias contentY: rows.contentY
            property real savedContentY: 0
            function rowIndex(index) {
                return bookRows.findIndex(row => index >= row.startIndex && index < row.startIndex + row.books.length)
            }
            function positionViewAtIndex(index, mode) {
                let row = rowIndex(index)
                if (row >= 0) { rows.positionViewAtIndex(row, mode); rows.forceLayout() }
            }
            function selectIndex(index) {
                if (!count) return
                currentIndex = Math.max(0, Math.min(count - 1, index))
                focusedBook = currentBook.id
                positionViewAtIndex(currentIndex, ListView.Contain)
                forceActiveFocus()
            }
            function moveRow(direction) {
                let next = currentRow + direction
                if (currentRow >= 0 && next >= 0 && next < bookRows.length)
                    selectIndex(bookRows[next].startIndex + Math.min(currentIndex - bookRows[currentRow].startIndex, bookRows[next].books.length - 1))
            }
            function restoreScroll() { rows.contentY = Math.max(rows.originY, Math.min(savedContentY, rows.originY + rows.contentHeight - rows.height)) }
            onCurrentBookChanged: if (currentBook && !modelResetting) focusedBook = currentBook.id
            onColumnsChanged: Qt.callLater(function() { positionViewAtIndex(currentIndex, ListView.Contain) })
            Keys.onLeftPressed: selectIndex(currentIndex - 1)
            Keys.onRightPressed: selectIndex(currentIndex + 1)
            Keys.onUpPressed: moveRow(-1)
            Keys.onDownPressed: moveRow(1)
            Keys.onPressed: event => {
                const modifiers = event.modifiers & ~Qt.KeypadModifier
                if (currentBook && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                    && (modifiers === Qt.NoModifier || modifiers === Qt.ControlModifier)) {
                    if (!event.isAutoRepeat) {
                        if (modifiers === Qt.ControlModifier) openDetails(currentBook.id)
                        else playBook(library.book(currentBook.id))
                    }
                    event.accepted = true
                } else if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                    selectIndex(event.key === Qt.Key_Home ? 0 : count - 1); event.accepted = true
                } else if (currentBook && (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && event.modifiers === Qt.ShiftModifier))) {
                    positionViewAtIndex(currentIndex, ListView.Contain)
                    showBookMenu(currentBook.id, currentItem || grid, Qt.point(12, 12)); event.accepted = true
                }
            }
            ListView {
                id: rows; objectName: "bookRows"; anchors.fill: parent; clip: true
                model: grid.bookRows; currentIndex: grid.currentRow
                keyNavigationEnabled: false; highlightFollowsCurrentItem: false
                ScrollBar.vertical: ScrollBar { active: hovered || pressed || rows.moving || libraryWheel.active }
                WheelHandler {
                    id: libraryWheel; target: null
                    acceptedModifiers: Qt.NoModifier
                    onWheel: event => {
                        if (event.phase !== Qt.NoScrollPhase) { event.accepted = false; return }
                        rows.cancelFlick()
                        const next = rows.contentY - event.angleDelta.y / 120 * grid.cellHeight * 2
                        if (next >= rows.originY + rows.contentHeight - rows.height) rows.positionViewAtEnd()
                        else rows.contentY = Math.max(rows.originY, next)
                    }
                }
                delegate: Column {
                    id: bookRow
                    required property var modelData
                    width: rows.width
                    readonly property var currentCard: { cards.count; return cards.itemAt(grid.currentIndex - modelData.startIndex) }
                    PlainLabel {
                        text: bookRow.modelData.heading; visible: text.length > 0
                        width: parent.width; padding: 6; topPadding: 14; bottomPadding: 10
                        font.pixelSize: 20; font.bold: true; wrapMode: Text.Wrap
                    }
                    Row {
                        spacing: 8; height: grid.cellHeight
                        Repeater {
                            id: cards; model: bookRow.modelData.books
                            ItemDelegate {
                                id: card
                                required property var modelData
                                property int bookRevision: 0
                                readonly property var book: { bookRevision; return library.book(modelData.id) }
                                Connections {
                                    target: library
                                    function onBookChanged(id) { if (!id || id === card.modelData.id) card.bookRevision++ }
                                }
                                readonly property int bookIndex: modelData.visibleIndex
                                readonly property string seriesText: !(book.series || "").trim() ? "No series" : book.series.trim() + ((book.seriesOrder || "").trim() ? " · " + book.seriesOrder.trim() : "")
                                width: grid.cellWidth - 8; height: grid.cellHeight - 10; padding: 6
                                onClicked: { grid.currentIndex = bookIndex; openDetails(book.id) }
                                onActiveFocusChanged: if (activeFocus) { grid.currentIndex = bookIndex; focusedBook = book.id }
                                ContextMenu.onRequested: position => showBookMenu(book.id, card, position)
                                background: Rectangle { color: parent.hovered ? theme.surface : "transparent"; radius: 8; border.width: grid.currentIndex === card.bookIndex && grid.activeFocus ? 2 : 0; border.color: theme.accent }
                                contentItem: ColumnLayout {
                                    spacing: 5
                                    Rectangle {
                                        Layout.preferredWidth: coverSize; Layout.preferredHeight: coverSize; color: theme.surface; radius: 6
                                        PlainLabel { anchors.centerIn: parent; text: "♫"; color: theme.accent; font.pixelSize: 52 }
                                        Image { anchors.fill: parent; source: book.cover ? book.coverUrl : ""; fillMode: Image.PreserveAspectFit; asynchronous: true; sourceSize.width: coverSize * 2; sourceSize.height: coverSize * 2 }
                                    }
                                    PlainLabel { text: (book.favorite ? "★ " : "") + book.title; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                    PlainLabel { text: book.author || "Unknown author"; elide: Text.ElideRight; Layout.fillWidth: true }
                                    PlainLabel { text: card.seriesText; visible: sort.currentIndex === 4; font.pixelSize: 11; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                    ProgressBar { value: book.progress; Layout.fillWidth: true }
                                    PlainLabel { text: !book.available ? "Unavailable · reconnect or relink" : book.finished ? "Finished" : time(book.remaining) + " left at " + book.speed + "×"; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                                }
                                Accessible.name: book.title + ", " + (book.author || "Unknown author") + (sort.currentIndex === 4 ? ", " + seriesText : "") + (book.available ? "" : ", unavailable")
                                ToolTip { visible: parent.hovered; contentItem: PlainLabel { text: book.title + (sort.currentIndex === 4 ? "\n" + card.seriesText : "") } }
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                anchors.centerIn: parent; visible: grid.count === 0
                PlainLabel { text: library.scanning ? "Scanning your library…" : search.text || filter.currentIndex ? "No matching books" : "Your next chapter starts here."; font.pixelSize: 22; Layout.alignment: Qt.AlignHCenter }
                PlainLabel { text: "Local MP3 and M4B audiobooks. Your files stay untouched."; Layout.alignment: Qt.AlignHCenter }
                Button { text: "Add a library folder…"; enabled: !library.scanning; onClicked: folder.open(); Layout.alignment: Qt.AlignHCenter }
            }
        }
        ColumnLayout {
            visible: selectedBook > 0; Layout.fillWidth: true; Layout.fillHeight: true
            Button { id: back; text: "‹ Library"; onClicked: backToGrid() }
            ListView {
                id: chapterList; objectName: "chapterList"
                property real previousOrigin: 0
                onOriginYChanged: {
                    const wasAtTop = contentY <= previousOrigin + 1
                    previousOrigin = originY
                    if (wasAtTop) contentY = originY
                }
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                ScrollBar.vertical: ScrollBar {}
                activeFocusOnTab: true
                model: { chapterRevision; return selectedBook ? library.chapters(selectedBook) : [] }
                spacing: 12
                header: ColumnLayout {
                    objectName: "detailHeader"; width: chapterList.width; spacing: 12
                    RowLayout {
                        spacing: 18
                        Rectangle {
                            Layout.preferredWidth: 150; Layout.preferredHeight: 150; Layout.alignment: Qt.AlignTop; color: theme.surface; radius: 6
                            PlainLabel { anchors.centerIn: parent; text: "♫"; color: theme.accent; font.pixelSize: 50 }
                            Image { anchors.fill: parent; source: detail.cover ? detail.coverUrl : ""; fillMode: Image.PreserveAspectFit; asynchronous: true; sourceSize.width: 300 }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            PlainLabel { text: detail.title || ""; font.pixelSize: 24; font.bold: true; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            PlainLabel { text: detail.author || "Unknown author"; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            PlainLabel { text: "Narrated by " + (detail.narrator || "Unknown narrator"); wrapMode: Text.Wrap; Layout.fillWidth: true }
                            PlainLabel { text: (detail.series || "") + (detail.seriesOrder ? " · " + detail.seriesOrder : ""); visible: text.length > 0; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            PlainLabel { text: "Duration " + (detail.duration ? time(detail.duration) : "unknown") + " · " + time(detail.remaining === undefined ? -1 : detail.remaining) + " remaining" }
                            RowLayout {
                                Button { text: playText(detail); enabled: !!detail.available || (player.bookId === selectedBook && player.playing); onClicked: playBook(detail) }
                                Button { text: detail.favorite ? "★ Favorite" : "☆ Favorite"; onClicked: library.setFlag(selectedBook, "favorite", !detail.favorite) }
                                Button { text: detail.finished ? "Mark unfinished" : "Mark finished"; onClicked: library.setFlag(selectedBook, "finished", !detail.finished) }
                            }
                        }
                    }
                    RowLayout {
                        Button { text: "Edit details…"; onClicked: editBook(selectedBook) }
                        Button { text: "Choose cover…"; onClicked: { coverFile.bookId = selectedBook; coverFile.open() } }
                        Button { objectName: "startOverButton"; text: "Start over…"; visible: !detail.finished; enabled: !!detail.available; onClicked: { restartDialog.bookId = selectedBook; restartDialog.open() } }
                        Button { text: "Relink…"; enabled: !library.scanning; onClicked: relinkBook(selectedBook) }
                        PlainLabel { visible: !detail.available; text: "Unavailable — reconnect and Refresh"; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    }
                    PlainLabel { text: detail.description || "No description available."; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    PlainLabel { text: "Chapters & files"; font.bold: true; font.pixelSize: 18 }
                }
                delegate: PlainButton {
                    required property var modelData
                    required property int index
                    width: chapterList.width; focus: ListView.isCurrentItem
                    text: modelData.title + "  ·  " + time(modelData.start); enabled: !!detail.available
                    onClicked: { chapterList.currentIndex = index; chapter(modelData.track, modelData.start) }
                }
                footer: ColumnLayout {
                    width: chapterList.width; spacing: 12
                    PlainLabel { text: "Bookmarks"; font.bold: true; font.pixelSize: 18 }
                    Repeater {
                        model: { bookmarkRevision; return selectedBook ? library.bookmarks(selectedBook) : [] }
                        RowLayout {
                            required property var modelData
                            PlainButton { text: parent.modelData.label + "  ·  " + parent.modelData.file + "  ·  " + time(parent.modelData.offset); Layout.fillWidth: true; enabled: !!detail.available; onClicked: chapter(parent.modelData.track, parent.modelData.offset) }
                            Button { text: "Remove"; Accessible.name: "Remove bookmark " + parent.modelData.label; onClicked: library.removeBookmark(parent.modelData.id) }
                        }
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: theme.accent; opacity: 0.4 }
        RowLayout {
            PlainLabel { text: player.title || "Nothing playing"; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
            PlainLabel { text: time(player.position) + " / " + time(player.duration) }
        }
        Slider {
            id: seekSlider; objectName: "seekSlider"
            Layout.fillWidth: true; from: 0; to: Math.max(1, player.duration)
            enabled: player.bookId > 0; Accessible.name: "Seek within current file"
            property real seekTarget: 0
            Binding on value { value: player.position; when: !seekSlider.pressed; restoreMode: Binding.RestoreNone }
            onMoved: { seekTarget = value; if (!pressed) player.seek(seekTarget) }
            onPressedChanged: { if (pressed) seekTarget = value; else player.seek(seekTarget) }
        }
        RowLayout {
            Button { text: "|‹"; Layout.preferredWidth: 44; enabled: player.bookId > 0; onClicked: player.previous(); Accessible.name: "Previous chapter" }
            Button { text: "−30"; Layout.preferredWidth: 54; enabled: player.bookId > 0; onClicked: player.skip(-30); Accessible.name: "Back 30 seconds" }
            Button {
                id: playButton; objectName: "playButton"; Layout.preferredWidth: 74
                text: player.playing ? "Pause" : "Play"; display: AbstractButton.IconOnly
                icon.name: player.playing ? "media-playback-pause" : "media-playback-start"
                icon.source: player.playing
                    ? "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'><path d='M6 4h4v16H6zM14 4h4v16h-4z'/></svg>"
                    : "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'><path d='M6 3v18l15-9z'/></svg>"
                icon.width: 22; icon.height: 22; icon.color: palette.buttonText
                enabled: player.bookId > 0; onClicked: player.toggle()
                Accessible.name: text
                ToolTip.visible: hovered; ToolTip.text: text + " (Space)"
            }
            Button { text: "+30"; Layout.preferredWidth: 54; enabled: player.bookId > 0; onClicked: player.skip(30); Accessible.name: "Forward 30 seconds" }
            Button { text: "›|"; Layout.preferredWidth: 44; enabled: player.bookId > 0; onClicked: player.next(); Accessible.name: "Next chapter" }
            Button { text: "Bookmark"; Layout.preferredWidth: 92; enabled: player.bookId > 0; onClicked: bookmarkDialog.open() }
            Item { Layout.fillWidth: true }
            ThemedComboBox { id: speedSelect; model: ["0.5×", "0.75×", "1×", "1.25×", "1.5×", "1.75×", "2×", "2.5×", "3×"]; currentIndex: [0.5, 0.75, 1, 1.25, 1.5, 1.75, 2, 2.5, 3].indexOf(player.speed); onActivated: player.speed = parseFloat(currentText); Accessible.name: "Playback speed"; Layout.preferredWidth: 88 }
            Slider { objectName: "volumeSlider"; from: 0; to: 1; value: player.volume; Layout.preferredWidth: 95; onMoved: player.volume = value; Accessible.name: "Volume" }
        }
        RowLayout {
            PlainLabel { text: "Sleep" }
            ThemedComboBox { id: sleepSelect; model: ["Off", "15 minutes", "30 minutes", "60 minutes", "End of chapter"]; onActivated: player.sleep([0, 15, 30, 60, -1][currentIndex]); Accessible.name: "Sleep timer" }
            PlainLabel { text: player.sleepLabel; Layout.fillWidth: true }
            PlainLabel { text: selectedBook ? "Backspace back · Space play/pause · Ctrl+F search" : "Enter play/pause · Ctrl+Enter details · Ctrl+F search\nCtrl+1–4 filters · Ctrl+Tab cycle · Alt+S sort"; horizontalAlignment: Text.AlignRight; font.pixelSize: 11 }
        }
    }
    Connections {
        target: library
        function onBookChanged(id) { if (!id || id === selectedBook) detailRevision++ }
        function onChaptersChanged() { chapterRevision++ }
        function onBookmarksChanged(id) { if (!id || id === selectedBook) bookmarkRevision++ }
        function onModelAboutToBeReset() { modelResetting = true; grid.savedContentY = grid.contentY }
        function onModelReset() {
            grid.modelRevision++
            Qt.callLater(function() {
                let index = library.visibleIndex(focusedBook)
                grid.currentIndex = index >= 0 ? index : Math.max(0, Math.min(grid.currentIndex, grid.count - 1))
                focusedBook = grid.currentBook ? grid.currentBook.id : 0
                grid.restoreScroll()
                modelResetting = false
            })
        }
    }
    Shortcut { sequence: "Ctrl+F"; enabled: !modalOpen; onActivated: { selectedBook = 0; search.forceActiveFocus() } }
    Shortcut { sequence: "Escape"; enabled: !modalOpen; onActivated: backToGrid() }
    Shortcut { sequence: "Ctrl+1"; enabled: libraryShortcutsEnabled; autoRepeat: false; onActivated: chooseFilter(0) }
    Shortcut { sequence: "Ctrl+2"; enabled: libraryShortcutsEnabled; autoRepeat: false; onActivated: chooseFilter(1) }
    Shortcut { sequence: "Ctrl+3"; enabled: libraryShortcutsEnabled; autoRepeat: false; onActivated: chooseFilter(2) }
    Shortcut { sequence: "Ctrl+4"; enabled: libraryShortcutsEnabled; autoRepeat: false; onActivated: chooseFilter(3) }
    Shortcut { sequence: "Ctrl+Tab"; enabled: libraryShortcutsEnabled; autoRepeat: false; onActivated: chooseFilter((filter.currentIndex + 1) % filter.count) }
    Shortcut { sequence: "Ctrl+Shift+Tab"; enabled: libraryShortcutsEnabled; autoRepeat: false; onActivated: chooseFilter((filter.currentIndex + filter.count - 1) % filter.count) }
    Shortcut { sequence: "Alt+S"; enabled: libraryShortcutsEnabled; autoRepeat: false; onActivated: { sort.forceActiveFocus(); sort.popup.open() } }
}
