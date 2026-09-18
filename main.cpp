#include "app.h"
#include "checks.h"
#include "desktop.h"
#include <QDir>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QDBusConnection>
#include <cstdio>

class WindowKeys : public QObject {
public:
    WindowKeys(QQuickWindow &window, Player &player) : QObject(&window), window(window), player(player) { window.installEventFilter(this); }
private:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() != QEvent::KeyPress) return false;
        const auto key = static_cast<QKeyEvent *>(event);
        const auto focus = window.activeFocusItem();
        if (key->modifiers() != Qt::NoModifier
            || window.property("modalOpen").toBool() || (focus && focus->flags().testFlag(QQuickItem::ItemAcceptsInputMethod))) return false;
        if (key->key() == Qt::Key_Backspace && window.property("selectedBook").toInt()) {
            if (!key->isAutoRepeat()) QMetaObject::invokeMethod(&window, "backToGrid");
            return true;
        }
        if (key->key() == Qt::Key_Space && player.bookId()) {
            if (!key->isAutoRepeat()) player.toggle();
            return true;
        }
        if ((key->key() == Qt::Key_BracketLeft || key->key() == Qt::Key_BracketRight) && player.bookId()) {
            if (!key->isAutoRepeat()) player.skip(key->key() == Qt::Key_BracketLeft ? -30 : 30);
            return true;
        }
        return false;
    }
    QQuickWindow &window;
    Player &player;
};

int smoke(Library &, Player &, const QStringList &);
void menuSmoke(QQuickWindow &, Library &, Player &, const QString &);
void appearanceSmoke(QQuickWindow &, Library &, Player &, Theme &, const QString &);
void seekSliderSmoke(QQuickWindow &, Library &, Player &, const QString &);
void skipKeysSmoke(QQuickWindow &, Library &, Player &);
void browseShortcutsSmoke(QQuickWindow &, Library &, Player &, const QString &);
int main(int argc, char **argv) {
    qputenv("QT_FFMPEG_PROTOCOL_WHITELIST", "file,crypto,data");
#ifdef Q_OS_LINUX
    // Qt 6.11's native PipeWire streams omit application.name and forbid moves.
    // PulseAudio (including pipewire-pulse) publishes the name and supports moves.
    if (!qEnvironmentVariableIsSet("QT_AUDIO_BACKEND")) qputenv("QT_AUDIO_BACKEND", "pulseaudio");
#endif
    QGuiApplication app(argc, argv);
    app.setOrganizationName("oma-audio-books");
    app.setApplicationName("oma-audio-books");
    app.setApplicationVersion("0.1.0");
    app.setDesktopFileName("oma-audio-books");
    QQuickStyle::setStyle("Basic");
    const auto args = app.arguments();
    if (args.value(1) == "--media-probe") return mediaProbe(args.mid(2));
    try {
        const auto data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (args.value(1) == "--smoke" || args.value(1) == "--ui-smoke") {
            const auto base = qEnvironmentVariable("OMA_SMOKE_BASE");
            require(!base.isEmpty() && base.startsWith(QDir::tempPath() + "/oma-audio-books-synthetic-") && within(QDir::cleanPath(data), base), "smoke requires temporary XDG state before opening any catalog");
            for (const auto name : {"XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"})
                require(within(QDir::cleanPath(qEnvironmentVariable(name)), base), "all smoke XDG paths must be temporary");
            if (args.value(1) == "--smoke")
                require(within(QFileInfo(args.value(3)).canonicalFilePath(), base) && QFileInfo::exists(args.value(3) + "/.oma-synthetic"), "smoke requires marked synthetic media");
            else require(within(QDir::cleanPath(args.value(2)), base), "UI smoke screenshot must be temporary");
        }
        if (!QDir().mkpath(data)) throw std::runtime_error("Cannot create application data directory");
        const auto endpoint = data + "/instance.sock";
        QLockFile lock(data + "/instance.lock");
        if (!lock.tryLock(0)) {
            QLocalSocket socket;
            socket.connectToServer(endpoint);
            if (socket.waitForConnected(1500)) return 0;
            fprintf(stderr, "oma-audio-books is already running; focus request could not reach it.\n");
            return 1;
        }
        Library library;
        Player player(library);
        if (args.value(1) == "--smoke") return smoke(library, player, args.mid(2));
        Theme theme;
        QQmlApplicationEngine engine;
        bool qmlError = false;
        QObject::connect(&engine, &QQmlEngine::warnings, &app, [&](const QList<QQmlError> &) { qmlError = true; });
        engine.rootContext()->setContextProperty("library", &library);
        engine.rootContext()->setContextProperty("player", &player);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("suggestedFolder", QUrl::fromLocalFile(QDir::homePath() + "/Audiobooks"));
        engine.load(QUrl("qrc:/Main.qml"));
        if (engine.rootObjects().isEmpty()) return 1;
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!window) throw std::runtime_error("No native application window");
        new WindowKeys(*window, player);
        QLocalServer server;
        server.setSocketOptions(QLocalServer::UserAccessOption);
        QLocalServer::removeServer(endpoint); // Only after owning the per-catalog lock.
        if (!server.listen(endpoint) && args.value(1) != "--ui-smoke") library.fail("Second-instance focus unavailable: " + server.errorString());
        QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
            while (auto socket = server.nextPendingConnection()) { window->showNormal(); window->raise(); window->requestActivate(); socket->disconnectFromServer(); socket->deleteLater(); }
        });
        QObject busObject;
        new MprisRoot(&busObject, window);
        new MprisPlayer(&busObject, player, library);
        auto bus = QDBusConnection::sessionBus();
        const bool mpris = bus.registerService("org.mpris.MediaPlayer2.oma_audio_books") && bus.registerObject("/org/mpris/MediaPlayer2", &busObject, QDBusConnection::ExportAdaptors);
        if (!mpris && args.value(1) != "--ui-smoke") library.fail("MPRIS unavailable: cannot register with the desktop session bus.");
        library.refresh();
        if (args.value(1) == "--ui-smoke") {
            require(qEnvironmentVariableIsSet("OMA_SMOKE_BASE") && within(data, qEnvironmentVariable("OMA_SMOKE_BASE")), "UI smoke requires temporary XDG data");
            QTimer::singleShot(1000, &app, [&] {
                try {
                    require(!qmlError, "QML warnings on startup");
                    auto sort = window->findChild<QQuickItem *>("sort");
                    require(sort && sort->property("currentIndex") == library.preference("sort", 4), "restore selected sort, defaulting to Series");
                    require(window->grabWindow().save(args.value(2) + ".library.png"), "initial library rendering");
                    window->resize(820, 620);
                    appearanceSmoke(*window, library, player, theme, args.value(2));
                    browseShortcutsSmoke(*window, library, player, args.value(2));
                    QKeyEvent key(QEvent::KeyPress, Qt::Key_F, Qt::ControlModifier);
                    QCoreApplication::sendEvent(window, &key);
                    require(window->activeFocusItem() && window->activeFocusItem()->objectName() == "search", "Ctrl+F focus");
                    auto pressSpace = [&](bool repeat = false) {
                        QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ", repeat);
                        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, " ", repeat);
                        QCoreApplication::sendEvent(window, &press); QCoreApplication::sendEvent(window, &release);
                    };
                    auto search = window->activeFocusItem();
                    pressSpace();
                    require(search->property("text") == " " && !player.playing(), "Space must type in search without playing");
                    QKeyEvent backspace(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
                    QCoreApplication::sendEvent(window, &backspace);
                    require(search->property("text").toString().isEmpty() && search->hasActiveFocus(), "Backspace must delete search text without navigating");
                    search->setProperty("text", "");
                    for (const auto name : {"volumeSlider", "grid"}) {
                        auto item = window->findChild<QQuickItem *>(name); require(item, "keyboard focus target");
                        item->forceActiveFocus(); pressSpace();
                        require(waitFor([&] { return player.playing(); }, 2000), "Space outside text input must play");
                        pressSpace(true); require(player.playing(), "holding Space toggled playback repeatedly");
                        pressSpace(); require(!player.playing(), "Space outside text input must pause");
                    }
                    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                    QCoreApplication::sendEvent(window, &escape);
                    QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
                    QCoreApplication::sendEvent(window, &right);
                    const auto focusedBook = window->property("focusedBook").toInt();
                    require(sort->setProperty("currentIndex", 0) && QMetaObject::invokeMethod(sort, "activated", Q_ARG(int, 0)), "activate flat title view");
                    QCoreApplication::processEvents();
                    require(window->property("focusedBook").toInt() == focusedBook, "title view lost focused book");
                    require(sort && sort->setProperty("currentIndex", 4) && sort->property("currentText") == "Series", "Series sort option");
                    require(QMetaObject::invokeMethod(sort, "activated", Q_ARG(int, 4)), "activate Series sort");
                    library.reload(); QCoreApplication::processEvents();
                    require(window->property("focusedBook").toInt() == focusedBook, "series sort or refresh changed focused book");
                    require(library.preference("sort", -1).toInt() == 4, "Series selection not saved");
                    auto grid = window->findChild<QQuickItem *>("grid");
                    auto navigate = [&](int code) {
                        QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier);
                        QCoreApplication::sendEvent(window, &press); QCoreApplication::processEvents();
                    };
                    grid->forceActiveFocus(); navigate(Qt::Key_Home);
                    require(grid->property("currentIndex").toInt() == 0, "Home selects first book");
                    const auto rows = library.bookRows(grid->property("columns").toInt(), library.preference("seriesHeadings", true).toBool());
                    if (rows.size() > 1) {
                        navigate(Qt::Key_Down);
                        require(grid->property("currentIndex").toInt() == rows[1].toMap()["startIndex"].toInt(), "Down navigates between cover rows/series");
                        navigate(Qt::Key_Up);
                        require(grid->property("currentIndex").toInt() == 0, "Up navigates between cover rows/series");
                    }
                    navigate(Qt::Key_End);
                    require(grid->property("currentIndex").toInt() == library.rowCount() - 1, "End selects final book");
                    const auto scroll = grid->property("contentY").toDouble();
                    const auto lastBook = window->property("focusedBook");
                    library.reload(); QCoreApplication::processEvents();
                    require(window->property("focusedBook") == lastBook && qAbs(grid->property("contentY").toDouble() - scroll) < 1, "refresh moved grouped view or selection");
                    auto selectedCard = grid->property("currentItem").value<QQuickItem *>();
                    require(selectedCard && selectedCard->property("bookIndex") == grid->property("currentIndex"), "refresh lost selected cover");
                    window->setProperty("coverSize", 260); QCoreApplication::processEvents();
                    require(grid->property("columns").toInt() == 2 && window->property("focusedBook") == lastBook, "resizing covers lost grouped selection");
                    window->setProperty("coverSize", 180); QCoreApplication::processEvents();
                    require(grid->property("columns").toInt() == 3 && window->property("focusedBook") == lastBook, "restoring cover size lost selection");
                    grid->setProperty("currentIndex", library.visibleIndex(focusedBook));
                    grid->forceActiveFocus();
                    const int previousBook = player.bookId(), previousTrack = player.trackId();
                    const auto previousPosition = player.position();
                    for (const auto code : {Qt::Key_Return, Qt::Key_Enter}) {
                        const auto modifiers = code == Qt::Key_Enter ? Qt::KeypadModifier : Qt::NoModifier;
                        auto card = grid->property("currentItem").value<QQuickItem *>();
                        if (code == Qt::Key_Enter && card) card->forceActiveFocus();
                        QKeyEvent play(QEvent::KeyPress, code, modifiers), release(QEvent::KeyRelease, code, modifiers), held(QEvent::KeyPress, code, modifiers, "", true);
                        QCoreApplication::sendEvent(window, &play); QCoreApplication::sendEvent(window, &release);
                        require(waitFor([&] { return player.bookId() == focusedBook && player.playing(); })
                            && window->property("selectedBook").toInt() == 0, "Enter must play the highlighted book without opening details");
                        QCoreApplication::sendEvent(window, &held);
                        require(player.playing(), "holding Enter toggled playback repeatedly");
                        QCoreApplication::sendEvent(window, &play); QCoreApplication::sendEvent(window, &release);
                        require(!player.playing(), "Enter must pause the highlighted playing book");
                    }
                    player.open(previousBook, false); player.jump(previousTrack, previousPosition, false);
                    require(waitFor([&] { return player.media.isSeekable() && !player.playing() && qAbs(player.position() - previousPosition) < 100; }), "restore player after Enter check");
                    grid->forceActiveFocus();
                    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier);
                    QKeyEvent enterRelease(QEvent::KeyRelease, Qt::Key_Return, Qt::ControlModifier);
                    QCoreApplication::sendEvent(window, &enter); QCoreApplication::sendEvent(window, &enterRelease);
                    require(window->property("selectedBook").toInt() == focusedBook && !player.playing(), "Ctrl+Enter must open the highlighted book without starting playback");
                    const auto detailsId = window->property("selectedBook");
                    QCoreApplication::processEvents(); window->grabWindow();
                    auto header = window->findChild<QQuickItem *>("detailHeader");
                    auto detailsList = window->findChild<QQuickItem *>("chapterList");
                    require(header && detailsList && qAbs(header->mapToItem(detailsList, QPointF()).y()) < 1, "opening details hid the book header");
                    const int savedBook = player.bookId(), savedTrack = player.trackId();
                    const auto savedPosition = player.position();
                    auto detailsFocus = window->activeFocusItem();
                    auto chapterList = window->findChild<QQuickItem *>("chapterList");
                    const auto chapters = library.chapters(focusedBook);
                    require(chapterList && !chapters.isEmpty() && chapterList->setProperty("currentIndex", 0), "chapter keyboard setup");
                    chapterList->forceActiveFocus();
                    if (chapters.size() > 1) navigate(Qt::Key_Down);
                    const auto chapter = chapters[chapters.size() > 1 ? 1 : 0].toMap();
                    navigate(Qt::Key_Return);
                    require(waitFor([&] {
                        return player.bookId() == focusedBook && player.trackId() == chapter["track"].toInt() && player.playing()
                            && player.position() >= chapter["start"].toLongLong() && player.position() < chapter["start"].toLongLong() + 1000;
                    }), "Return must play the keyboard-selected chapter");
                    player.pause(); player.open(savedBook, false); player.jump(savedTrack, savedPosition, false);
                    require(waitFor([&] { return player.media.isSeekable() && !player.playing() && qAbs(player.position() - savedPosition) < 100; }), "restore player after chapter keyboard check");
                    if (detailsFocus) detailsFocus->forceActiveFocus();
                    pressSpace();
                    require(waitFor([&] { return player.playing(); }, 2000) && window->property("selectedBook") == detailsId, "Space in details must play without activating Back");
                    window->findChild<QQuickItem *>("playButton")->forceActiveFocus(); pressSpace();
                    require(!player.playing(), "Space on play button toggled more than once");
                    QCoreApplication::sendEvent(window, &backspace);
                    require(window->property("selectedBook").toInt() == 0 && grid->hasActiveFocus()
                        && window->property("focusedBook") == detailsId && !player.playing(), "Backspace must return to the selected book without changing playback");
                    seekSliderSmoke(*window, library, player, args.value(2));
                    skipKeysSmoke(*window, library, player);
                    menuSmoke(*window, library, player, args.value(2) + ".menu.png");
                    theme.setMode("Light");
                    require(theme.background.lightness() > 128, "light theme");
                    theme.setMode("Dark");
                    require(theme.background.lightness() < 128, "dark theme");
                    QTimer::singleShot(300, &app, [&] {
                        try {
                            for (const auto name : {"settingsButton", "volumeSlider"}) {
                                auto item = window->findChild<QQuickItem *>(name);
                                require(item && item->mapToScene(QPointF(item->width(), item->height())).x() <= window->width(), "controls overflow minimum window");
                            }
                            require(!window->grabWindow().isNull() && window->grabWindow().save(args.value(2)) && !qmlError, "dark grid rendering");
                            require(QMetaObject::invokeMethod(window, "openDetails", Q_ARG(QVariant, player.bookId())), "open player details");
                            auto editor = window->findChild<QObject *>("metadataEditor");
                            require(editor && editor->setProperty("bookId", player.bookId()), "set metadata editor target");
                            require(editor && QMetaObject::invokeMethod(editor, "open"), "open metadata editor");
                            QCoreApplication::processEvents();
                            QKeyEvent ctrlF(QEvent::KeyPress, Qt::Key_F, Qt::ControlModifier);
                            QCoreApplication::sendEvent(window, &ctrlF);
                            require(window->property("selectedBook").toInt() == player.bookId(), "Ctrl+F changed editor target");
                            for (const auto name : {"metadataTitle", "metadataDescription"}) {
                                auto field = window->findChild<QQuickItem *>(name); require(field, "editor text field");
                                field->setProperty("text", "test"); field->setProperty("cursorPosition", 4); field->forceActiveFocus();
                                QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ");
                                QCoreApplication::sendEvent(window, &space);
                                require(field->property("text") == "test " && !player.playing(), "Space must type in editors without playing");
                                QKeyEvent backspace(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
                                QCoreApplication::sendEvent(window, &backspace);
                                require(field->property("text") == "test" && window->property("selectedBook").toInt() == player.bookId()
                                    && editor->property("visible").toBool(), "Backspace must edit text without leaving the editor/details");
                            }
                            QMetaObject::invokeMethod(editor, "reject");
                            theme.setMode("Light");
                            QTimer::singleShot(300, &app, [&] {
                                auto header = window->findChild<QQuickItem *>("detailHeader");
                                auto list = window->findChild<QQuickItem *>("chapterList");
                                if (!header || !list || qAbs(header->mapToItem(list, QPointF()).y()) >= 1 || !window->grabWindow().save(args.value(2) + ".details.png") || qmlError) { fprintf(stderr, "FAIL: details rendering\n"); app.exit(1); }
                                else { fprintf(stdout, "PASS: native QML grid/details render at 820x620, Space playback/text entry/repeat guards, modal keyboard guard, Ctrl+F, light/dark switch\n"); app.quit(); }
                            });
                        } catch (const std::exception &e) { fprintf(stderr, "FAIL: %s\n", e.what()); app.exit(1); }
                    });
                } catch (const std::exception &e) { fprintf(stderr, "FAIL: %s\n", e.what()); app.exit(1); }
            });
        }
        return app.exec();
    } catch (const std::exception &e) { fprintf(stderr, "oma-audio-books: %s\n", e.what()); return 1; }
}
