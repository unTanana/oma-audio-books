#include "app.h"
#include "checks.h"
#include "desktop.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QFileInfo>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWheelEvent>
#include <cmath>
#include <cstdio>

void browseShortcutsSmoke(QQuickWindow &window, Library &lib, Player &player, const QString &screenshot) {
    auto filter = window.findChild<QQuickItem *>("filter"), sort = window.findChild<QQuickItem *>("sort");
    auto grid = window.findChild<QQuickItem *>("grid"), search = window.findChild<QQuickItem *>("search");
    require(filter && sort && grid && search && !player.playing(), "browse shortcut setup");
    const int playingBook = player.bookId(), originalSort = sort->property("currentIndex").toInt();
    QList<QVariantMap> books;
    for (int i = 0; i < lib.rowCount(); ++i) books << lib.data(lib.index(i), Qt::UserRole).toMap();
    auto key = [&](int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier, bool repeat = false) {
        QKeyEvent press(QEvent::KeyPress, code, modifiers, "", repeat), release(QEvent::KeyRelease, code, modifiers);
        QCoreApplication::sendEvent(&window, &press); QCoreApplication::sendEvent(&window, &release); QCoreApplication::processEvents();
    };
    auto checkFilter = [&](int index) {
        require(filter->property("currentIndex").toInt() == index && grid->hasActiveFocus(), "filter shortcut did not select its view and focus the grid");
        QList<int> expected, actual;
        for (const auto &b : books) {
            if (index == 0 || (index == 1 && b["progress"].toDouble() > 0 && !b["finished"].toBool())
                || (index == 2 && b["finished"].toBool()) || (index == 3 && b["favorite"].toBool())) expected << b["id"].toInt();
        }
        for (int i = 0; i < lib.rowCount(); ++i) actual << lib.data(lib.index(i), Qt::UserRole).toMap()["id"].toInt();
        require(actual == expected && player.bookId() == playingBook && !player.playing(), "filter shortcut changed playback, order or membership");
    };
    grid->forceActiveFocus();
    for (int i = 1; i <= 4; ++i) { key(Qt::Key_1 + i % 4, Qt::ControlModifier); checkFilter(i % 4); }
    for (int i = 1; i <= 4; ++i) { key(Qt::Key_Tab, Qt::ControlModifier); checkFilter(i % 4); }
    key(Qt::Key_Backtab, Qt::ControlModifier | Qt::ShiftModifier); checkFilter(3);
    key(Qt::Key_Tab, Qt::ControlModifier | Qt::ShiftModifier); checkFilter(2);
    key(Qt::Key_Tab, Qt::ControlModifier, true); checkFilter(2);
    key(Qt::Key_1, Qt::ControlModifier); checkFilter(0);
    search->forceActiveFocus();
    key(Qt::Key_2, Qt::ControlModifier); key(Qt::Key_S, Qt::AltModifier);
    require(filter->property("currentIndex").toInt() == 0 && !window.property("modalOpen").toBool()
        && search->hasActiveFocus() && search->property("text").toString().isEmpty(), "browse shortcuts interfered with text entry");
    grid->forceActiveFocus(); key(Qt::Key_S, Qt::AltModifier);
    auto popup = sort->property("popup").value<QObject *>();
    require(waitFor([&] { return popup->property("opened").toBool(); }), "Alt+S did not open sorting");
    key(Qt::Key_2, Qt::ControlModifier); key(Qt::Key_Tab, Qt::ControlModifier);
    require(filter->property("currentIndex").toInt() == 0 && popup->property("visible").toBool(), "filter shortcuts escaped the sort popup");
    key(Qt::Key_Home); key(Qt::Key_Down); key(Qt::Key_Return);
    require(waitFor([&] { return !popup->property("visible").toBool(); }) && sort->property("currentIndex").toInt() == 1
        && lib.preference("sort", -1).toInt() == 1 && !player.playing(), "keyboard sort did not apply/persist Author");
    key(Qt::Key_S, Qt::AltModifier); require(waitFor([&] { return popup->property("opened").toBool(); }), "reopen sorting");
    key(Qt::Key_Home); for (int i = 0; i < originalSort; ++i) key(Qt::Key_Down);
    key(Qt::Key_Return); require(waitFor([&] { return !popup->property("visible").toBool(); }), "close restored sort");
    grid->forceActiveFocus();
    require(QMetaObject::invokeMethod(window.findChild<QQuickItem *>("settingsButton"), "clicked"), "open settings for shortcut guard");
    auto headings = window.findChild<QQuickItem *>("seriesHeadingsToggle");
    require(waitFor([&] { return headings->isVisible(); }), "settings opened"); headings->forceActiveFocus();
    key(Qt::Key_3, Qt::ControlModifier); key(Qt::Key_Tab, Qt::ControlModifier); key(Qt::Key_S, Qt::AltModifier);
    require(filter->property("currentIndex").toInt() == 0 && !popup->property("visible").toBool() && headings->isVisible(), "browse shortcuts escaped a dialog");
    key(Qt::Key_Escape); require(waitFor([&] { return !window.property("modalOpen").toBool(); }), "close settings");
    require(QMetaObject::invokeMethod(&window, "openDetails", Q_ARG(QVariant, playingBook)), "open details for shortcut guard");
    key(Qt::Key_4, Qt::ControlModifier); key(Qt::Key_Tab, Qt::ControlModifier); key(Qt::Key_S, Qt::AltModifier);
    require(window.property("selectedBook").toInt() == playingBook && filter->property("currentIndex").toInt() == 0
        && !popup->property("visible").toBool(), "library shortcuts changed hidden controls in details");
    key(Qt::Key_Backspace);
    auto filterPopup = filter->property("popup").value<QObject *>(); filter->forceActiveFocus();
    require(QMetaObject::invokeMethod(filterPopup, "open") && waitFor([&] { return filterPopup->property("opened").toBool(); }), "open shortcut labels");
    require(filter->property("availableWidth").toReal() >= filter->property("implicitContentWidth").toReal(), "filter shortcut labels truncated");
    require(window.grabWindow().save(screenshot + ".filter-shortcuts.png"), "filter shortcut screenshot");
    key(Qt::Key_Escape); grid->forceActiveFocus();
    require(sort->property("currentIndex").toInt() == originalSort && lib.preference("sort", -1).toInt() == originalSort, "restore original sort");
    fprintf(stdout, "PASS: direct/cycling filters and wrapping, repeat guard, Alt+S sorting/persistence, text/dialog/details guards and shortcut labels\n");
}

void seekSliderSmoke(QQuickWindow &window, Library &lib, Player &player) {
    auto slider = window.findChild<QQuickItem *>("seekSlider");
    require(slider && player.media.isSeekable() && player.duration() > 0 && !player.playing(), "seek slider setup");
    const auto saved = player.position();
    auto focus = window.activeFocusItem();
    player.seek(player.duration() / 5); QCoreApplication::processEvents();
    int seeks = 0;
    const auto connection = QObject::connect(&player, &Player::seeked, &window, [&](qint64) { ++seeks; });
    auto changes = [&] { auto q = lib.sql("SELECT total_changes()"); q.next(); return q.value(0).toInt(); };
    const auto before = changes();
    auto mouse = [&](QEvent::Type type, double fraction, Qt::MouseButton button, Qt::MouseButtons buttons) {
        const auto point = slider->mapToScene(QPointF(slider->width() * fraction, slider->height() / 2));
        QMouseEvent event(type, point, window.mapToGlobal(point), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &event); QCoreApplication::processEvents();
    };
    mouse(QEvent::MouseButtonPress, 0.2, Qt::LeftButton, Qt::LeftButton);
    for (int i = 3; i <= 7; ++i) mouse(QEvent::MouseMove, i / 10.0, Qt::NoButton, Qt::LeftButton);
    require(slider->property("pressed").toBool() && seeks == 0 && changes() == before, "dragging seek slider repeatedly sought or saved");
    const auto preview = slider->property("position").toDouble();
    player.media.setPosition(player.duration() / 4); QCoreApplication::processEvents();
    require(qAbs(slider->property("position").toDouble() - preview) < 0.001, "playback tick moved the seek preview");
    mouse(QEvent::MouseButtonRelease, 0.7, Qt::LeftButton, Qt::NoButton);
    require(seeks == 1 && changes() == before + 1 && qAbs(player.position() - player.duration() * preview) < 100,
        "seek release must commit the preview once");
    const auto mouseTarget = player.position();
    slider->forceActiveFocus();
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier), release(QEvent::KeyRelease, Qt::Key_Left, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &press); QCoreApplication::sendEvent(&window, &release); QCoreApplication::processEvents();
    require(seeks == 2 && changes() == before + 2 && player.position() < mouseTarget, "keyboard seek did not commit once");
    QObject::disconnect(connection);
    player.seek(saved); if (focus) focus->forceActiveFocus();
    fprintf(stdout, "PASS: seek drag previews without decoder/SQL work; release and keyboard commit once; playback ticks preserve preview\n");
}

void appearanceSmoke(QQuickWindow &window, Library &lib, Player &player, Theme &theme, const QString &screenshot) {
    require(waitFor([&] { return !lib.scanning(); }), "appearance scan setup");
    auto key = [&](int code) {
        QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier, code == Qt::Key_Space ? " " : "");
        QKeyEvent release(QEvent::KeyRelease, code, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &press); QCoreApplication::sendEvent(&window, &release);
        QCoreApplication::processEvents();
    };
    auto contrast = [](QColor a, QColor b) {
        auto luminance = [](QColor c) {
            auto linear = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
            return 0.2126 * linear(c.redF()) + 0.7152 * linear(c.greenF()) + 0.0722 * linear(c.blueF());
        };
        const double x = luminance(a), y = luminance(b);
        return (qMax(x, y) + 0.05) / (qMin(x, y) + 0.05);
    };
    auto sort = window.findChild<QQuickItem *>("sort");
    auto popup = sort->property("popup").value<QObject *>();
    auto headings = window.findChild<QQuickItem *>("seriesHeadingsToggle");
    require(headings && headings->property("checked").toBool() == lib.preference("seriesHeadings", true).toBool(), "restore heading visibility");
    const bool shown = headings->property("checked").toBool();
    for (const auto mode : {"Follow Omarchy", "Dark", "Light"}) {
        theme.setMode(mode);
        sort->setProperty("currentIndex", 1); sort->forceActiveFocus();
        require(QMetaObject::invokeMethod(popup, "open") && waitFor([&] { return popup->property("opened").toBool(); }), "open themed sort menu");
        key(Qt::Key_End);
        auto list = popup->property("contentItem").value<QQuickItem *>();
        auto item = list->property("currentItem").value<QQuickItem *>();
        require(item && item->property("highlighted").toBool(), "highlight final sort option");
        const auto name = QString(mode).toLower().replace(' ', '-');
        require(window.grabWindow().save(screenshot + ".sort-" + name + ".png"), "sort screenshot");
        auto text = item->property("contentItem").value<QQuickItem *>();
        auto background = item->property("background").value<QQuickItem *>();
        require(text && background, "dropdown text/background");
        const double ratio = contrast(text->property("color").value<QColor>(), background->property("color").value<QColor>());
        require(ratio >= 4.5, "highlighted dropdown text contrast");
        auto display = sort->property("contentItem").value<QQuickItem *>();
        auto button = sort->property("background").value<QQuickItem *>();
        require(display && button && contrast(display->property("color").value<QColor>(), button->property("color").value<QColor>()) >= 4.5, "open dropdown button contrast");
        fprintf(stdout, "PASS: %s highlighted dropdown text contrast %.2f:1\n", mode, ratio);
        require(sort->property("availableWidth").toReal() >= sort->property("implicitContentWidth").toReal(), "sort labels truncated");
        key(Qt::Key_Space);
        require(waitFor([&] { return !popup->property("visible").toBool(); }) && sort->property("currentIndex").toInt() == 4 && !player.playing(), "Space must select dropdown option without background playback");
    }
    theme.setMode("Follow Omarchy");
    const auto focusedBook = window.property("focusedBook");
    auto settings = window.findChild<QQuickItem *>("settingsButton");
    require(QMetaObject::invokeMethod(settings, "clicked") && waitFor([&] { return headings->isVisible(); }), "open heading setting");
    require(window.grabWindow().save(screenshot + ".settings.png"), "settings screenshot");
    headings->forceActiveFocus(); key(Qt::Key_Space);
    require(headings->property("checked").toBool() != shown && lib.preference("seriesHeadings", shown).toBool() != shown && !player.playing(), "heading toggle must save without playing");
    require(window.property("focusedBook") == focusedBook, "heading toggle changed selected book");
    key(Qt::Key_Escape);
    require(waitFor([&] { return !window.property("modalOpen").toBool(); }), "close settings");
    auto grid = window.findChild<QQuickItem *>("grid");
    grid->forceActiveFocus(); key(Qt::Key_Home);
    auto card = grid->property("currentItem").value<QQuickItem *>();
    require(card && waitFor([&] { return (card->parentItem()->y() > 0) == !shown; }), "heading visibility or collapsed spacing");
    if (shown) {
        const auto size = window.property("coverSize");
        window.setProperty("coverSize", 260); QCoreApplication::processEvents();
        const int columns = grid->property("columns").toInt();
        for (int i = 0; i < lib.rowCount(); ++i) {
            grid->setProperty("currentIndex", i);
            require(grid->property("currentRow").toInt() == i / columns, "hidden headings left gaps between series");
        }
        window.setProperty("coverSize", size); key(Qt::Key_Home);
    }
    require(window.grabWindow().save(screenshot + (shown ? ".headings-hidden.png" : ".headings-shown.png")), "heading visibility screenshot");
    auto rows = window.findChild<QQuickItem *>("bookRows");
    require(rows, "library rows");
    auto wheel = [&](int angle, int pixels = 0) {
        const auto point = grid->mapToScene(QPointF(40, 40));
        QWheelEvent event(point, window.mapToGlobal(point), QPoint(0, pixels), QPoint(0, angle),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&window, &event);
        QCoreApplication::processEvents();
        require(waitFor([&] { return !rows->property("moving").toBool(); }, 1000), "wheel did not settle");
    };
    const auto size = window.property("coverSize");
    for (int coverSize : {180, 260}) {
        window.setProperty("coverSize", coverSize); window.grabWindow(); key(Qt::Key_Home);
        const auto top = rows->property("originY").toReal();
        wheel(-120);
        const auto distance = rows->property("contentY").toReal() - top;
        fprintf(stdout, "Wheel distance at cover size %d: %.1f px\n", coverSize, distance);
        require(qAbs(distance - (coverSize + 112) * 2) < 1 || rows->property("atYEnd").toBool(), "mouse wheel must move two cover rows or reach the end");
        const auto scrolled = rows->property("contentY").toReal();
        wheel(120);
        require(rows->property("contentY").toReal() < scrolled || (distance < 1 && rows->property("atYBeginning").toBool()), "reverse wheel did not move upward");
        // Virtualized section heights can update originY after a large scroll.
        wheel(120);
        require(qAbs(rows->property("contentY").toReal() - rows->property("originY").toReal()) < 1, "wheel did not reach top");
        wheel(120);
        require(qAbs(rows->property("contentY").toReal() - rows->property("originY").toReal()) < 1, "wheel overscrolled top");
        for (int i = 0; i < rows->property("count").toInt() * 3 && !rows->property("atYEnd").toBool(); ++i) {
            wheel(-120); window.grabWindow();
        }
        require(rows->property("atYEnd").toBool(), "wheel did not stop at bottom");
        const auto bottom = rows->property("contentY"); wheel(-120);
        require(rows->property("contentY") == bottom, "wheel overscrolled bottom");
        key(Qt::Key_Home); const auto pixelStart = rows->property("contentY").toReal(); wheel(-120, -15);
        require(qAbs(rows->property("contentY").toReal() - pixelStart - (coverSize + 112) * 2) < 1 || rows->property("atYEnd").toBool(),
                "pixel-bearing mouse wheel bypassed the two-row step");
        key(Qt::Key_Home); const auto fractionalStart = rows->property("contentY").toReal(); wheel(-60);
        require(qAbs(rows->property("contentY").toReal() - fractionalStart - (coverSize + 112)) < 1 || rows->property("atYEnd").toBool(),
                "high-resolution wheel lost fractional steps");
    }
    window.setProperty("coverSize", size); window.grabWindow(); key(Qt::Key_Home);
    const auto top = rows->property("contentY").toReal();
    const auto point = grid->mapToScene(QPointF(40, 40));
    int timestamp = 10000;
    for (auto phase : {Qt::ScrollBegin, Qt::ScrollUpdate, Qt::ScrollUpdate, Qt::ScrollUpdate,
                       Qt::ScrollUpdate, Qt::ScrollUpdate, Qt::ScrollUpdate, Qt::ScrollEnd}) {
        const bool update = phase == Qt::ScrollUpdate;
        QWheelEvent event(point, window.mapToGlobal(point), QPoint(0, update ? -20 : 0), QPoint(0, update ? -120 : 0),
                          Qt::NoButton, Qt::NoModifier, phase, false, Qt::MouseEventSynthesizedBySystem);
        event.setTimestamp(timestamp += 20);
        QCoreApplication::sendEvent(&window, &event); QCoreApplication::processEvents();
    }
    require(waitFor([&] { return !rows->property("moving").toBool(); }, 1000), "pixel scrolling did not settle");
    const auto pixelDistance = rows->property("contentY").toReal() - top;
    fprintf(stdout, "Pixel scrolling distance: %.1f px\n", pixelDistance);
    require((pixelDistance > 0 && pixelDistance <= 120) || (qAbs(pixelDistance) < 1 && rows->property("contentHeight").toReal() <= rows->height()), "pixel scrolling was blocked or amplified");
    key(Qt::Key_Home);
    fprintf(stdout, "PASS: cover-scaled wheel distance, reverse direction and library bounds\n");
    fprintf(stdout, "PASS: dropdown contrast in Omarchy/dark/light, full labels, modal Space, persistent headings and collapsed spacing\n");
}

void menuSmoke(QQuickWindow &window, Library &lib, Player &player, const QString &screenshot) {
    require(waitFor([&] { return !lib.scanning(); }), "menu scan setup");
    auto menu = window.findChild<QObject *>("bookMenu");
    auto grid = window.findChild<QQuickItem *>("grid");
    require(menu && grid, "book menu and grid");
    const int sort = window.findChild<QQuickItem *>("sort")->property("currentIndex").toInt();
    const int originalBook = player.bookId();
    int target = originalBook;
    for (int i = 0; i < lib.rowCount(); ++i) {
        const auto b = lib.data(lib.index(i), Qt::UserRole).toMap();
        if (b["available"].toBool() && b["id"].toInt() != originalBook) { target = b["id"].toInt(); break; }
    }
    const auto before = lib.book(target);
    auto key = [&](int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QKeyEvent press(QEvent::KeyPress, code, modifiers, code == Qt::Key_Space ? " " : "");
        QKeyEvent release(QEvent::KeyRelease, code, modifiers);
        QCoreApplication::sendEvent(&window, &press); QCoreApplication::sendEvent(&window, &release);
    };
    auto open = [&](bool mouse = false) {
        window.setProperty("focusedBook", target);
        grid->setProperty("currentIndex", lib.visibleIndex(target)); grid->forceActiveFocus();
        QCoreApplication::processEvents();
        if (mouse) {
            require(QMetaObject::invokeMethod(grid, "positionViewAtIndex", Q_ARG(QVariant, lib.visibleIndex(target)), Q_ARG(QVariant, 0)), "scroll context target into view"); // Beginning
            auto card = grid->property("currentItem").value<QQuickItem *>(); require(card, "context card");
            const auto point = card->mapToScene(QPointF(20, 20)).toPoint();
            QContextMenuEvent event(QContextMenuEvent::Mouse, point, window.mapToGlobal(point));
            QCoreApplication::sendEvent(&window, &event);
        } else key(Qt::Key_Menu);
        require(waitFor([&] { return menu->property("opened").toBool(); }, 1000), mouse ? "mouse context menu did not open" : "keyboard context menu did not open");
        require(menu->property("bookId").toInt() == target && window.property("selectedBook").toInt() == 0, "menu targeted wrong book or opened details");
    };
    auto close = [&] { key(Qt::Key_Escape); require(waitFor([&] { return !menu->property("visible").toBool(); }, 1000), "Escape did not close menu"); };
    player.seek(0); player.play(); require(waitFor([&] { return player.playing(); }), "menu playback setup");
    open(true);
    require(player.bookId() == originalBook && player.playing(), "opening context menu interrupted playback");
    require(waitFor([&] { return player.position() >= 250; }), "playback progress before save");
    QPointer<QQuickItem> stableCard = grid->property("currentItem").value<QQuickItem *>();
    const auto scroll = grid->property("contentY");
    player.persist(); QCoreApplication::processEvents();
    require(stableCard && grid->property("currentItem").value<QQuickItem *>() == stableCard && grid->property("contentY") == scroll,
            "saving playback rebuilt the cover grid");
    require(menu->property("visible").toBool() && menu->property("bookId").toInt() == target, "progress refresh lost menu target");
    require(window.grabWindow().save(screenshot), "menu screenshot");
    key(Qt::Key_Down);
    require(menu->property("currentIndex").toInt() == 1, "menu arrow navigation");
    key(Qt::Key_Space);
    require(waitFor([&] { return !menu->property("visible").toBool(); }, 1000) && player.playing() && player.bookId() == originalBook,
            "menu Space affected background playback");
    require(lib.book(target)["favorite"].toBool() != before["favorite"].toBool(), "menu Space did not toggle target favorite");
    require(stableCard && stableCard->property("book").toMap()["favorite"] == lib.book(target)["favorite"], "existing cover did not update after favorite change");
    player.pause();
    lib.setFlag(target, "favorite", true); lib.query("", 3, sort);
    open(); key(Qt::Key_Down); key(Qt::Key_Return);
    require(waitFor([&] { return !menu->property("visible").toBool(); }, 1000) && lib.visibleIndex(target) == -1 && !lib.book(target)["favorite"].toBool(), "favorite removal under Favorites filter");
    lib.query("", 0, sort);
    open(); close();
    key(Qt::Key_F10, Qt::ShiftModifier);
    require(waitFor([&] { return menu->property("opened").toBool(); }, 1000), "Shift+F10 did not open menu");
    menu->setProperty("currentIndex", 5); key(Qt::Key_Return);
    auto editor = window.findChild<QObject *>("metadataEditor");
    require(editor && waitFor([&] { return editor->property("opened").toBool(); }, 1000) && editor->property("bookId").toInt() == target, "menu metadata target");
    lib.reload(); QCoreApplication::processEvents();
    auto title = window.findChild<QQuickItem *>("metadataTitle");
    title->setProperty("text", "Menu smoke correction");
    QMetaObject::invokeMethod(editor, "accept");
    require(waitFor([&] { return !editor->property("visible").toBool(); }, 1000) && lib.book(target)["title"] == "Menu smoke correction" && window.property("selectedBook").toInt() == 0, "menu edit lost target or navigated");
    lib.edit(target, {{"title", before["title"]}});
    lib.sql("UPDATE tracks SET available=0 WHERE book_id=?", {target}); lib.reload();
    open();
    require(!window.findChild<QObject *>("menuPlay")->property("enabled").toBool() && window.findChild<QObject *>("menuRelink")->property("visible").toBool(), "unavailable menu playback/repair state");
    auto disabledText = window.findChild<QObject *>("menuPlay")->property("contentItem").value<QQuickItem *>();
    require(disabledText && waitFor([&] { return disabledText->property("color").value<QColor>().alphaF() < 0.6; }), "disabled menu text lost its state after theme changes");
    require(window.grabWindow().save(screenshot + ".unavailable.png"), "unavailable menu screenshot");
    close(); lib.sql("UPDATE tracks SET available=1 WHERE book_id=?", {target}); lib.reload();
    open(); key(Qt::Key_Return);
    require(waitFor([&] { return player.bookId() == target && player.playing(); }), "menu play did not select target");
    open(); require(window.findChild<QObject *>("menuPlay")->property("text") == "Pause", "playing book lacks Pause");
    key(Qt::Key_Return); require(!player.playing(), "menu Pause did not pause target");
    player.seek(1800);
    open(); menu->setProperty("currentIndex", 2); key(Qt::Key_Return);
    require(lib.book(target)["finished"].toBool() && player.position() >= 1800, "mark finished moved playback");
    open(); require(window.findChild<QObject *>("menuPlay")->property("text") == "Play again", "finished book lacks Play again");
    menu->setProperty("currentIndex", 2); key(Qt::Key_Return);
    require(!lib.book(target)["finished"].toBool() && player.position() >= 1800, "menu mark unfinished discarded progress");
    lib.setFlag(target, "finished", true); open(); key(Qt::Key_Return);
    require(waitFor([&] { return player.playing() && player.trackId() == lib.tracks(target).first().toMap()["id"].toInt() && player.position() < 400; }), "menu Play again did not restart book");
    player.pause(); player.seek(1800);
    window.setProperty("selectedBook", target);
    auto restart = window.findChild<QObject *>("restartDialog");
    auto startOver = window.findChild<QObject *>("startOverButton");
    require(startOver && QMetaObject::invokeMethod(startOver, "clicked"), "Start over button");
    require(restart && waitFor([&] { return restart->property("opened").toBool(); }, 1000) && player.position() >= 1800 && !player.playing(), "Start over reset before confirmation");
    key(Qt::Key_Escape);
    require(waitFor([&] { return !restart->property("visible").toBool(); }, 1000) && player.position() >= 1800, "canceling Start over discarded progress");
    QMetaObject::invokeMethod(startOver, "clicked");
    require(waitFor([&] { return restart->property("opened").toBool(); }, 1000), "reopen Start over");
    QMetaObject::invokeMethod(restart, "accept");
    require(waitFor([&] { return player.playing() && player.position() < 400; }), "confirmed Start over did not restart");
    player.pause(); window.setProperty("selectedBook", 0);
    lib.setFlag(target, "finished", before["finished"].toBool());
    lib.setFlag(target, "favorite", before["favorite"].toBool());
    player.open(originalBook, false);
    fprintf(stdout, "PASS: context menu mouse/keyboard targeting, refresh, favorites removal, editor, unavailable state, Space/Enter/Escape and playback\n");
}

int smoke(Library &lib, Player &player, const QStringList &args) {
    try {
        const auto phase = args.value(0);
        const auto root = args.value(1);
        const auto base = qEnvironmentVariable("OMA_SMOKE_BASE");
        require(!base.isEmpty() && within(root, base) && within(lib.dataDir, base) && QFileInfo::exists(root + "/.oma-synthetic"), "smoke requires marked synthetic media and temporary XDG state");
        if (phase == "import") {
            lib.addRoot(QUrl::fromLocalFile(root));
            require(waitFor([&] { return !lib.scanning() && lib.rowCount() == 2; }, 20000), "import must produce two books");
            require(lib.error().isEmpty(), qPrintable(lib.error()));
            int mp3 = 0, m4b = 0;
            for (int i = 0; i < lib.rowCount(); ++i) {
                auto b = lib.data(lib.index(i), Qt::UserRole).toMap();
                (b["identity"].toString().endsWith(".m4b") ? m4b : mp3) = b["id"].toInt();
            }
            const auto ts = lib.tracks(mp3);
            require(ts.size() == 2 && QFileInfo(ts[0].toMap()["path"].toString()).fileName() == "2.mp3", "natural MP3 ordering");
            require(lib.chapters(m4b).size() == 2, "embedded chapters");
            player.open(mp3, true);
            require(waitFor([&] { return player.playing() && player.position() > 500; }), "app MP3 playback");
            player.seek(player.duration() - 300);
            const bool transitioned = waitFor([&] { return player.trackId() == ts[1].toMap()["id"].toInt() && player.position() > 100; });
            if (!transitioned) fprintf(stderr, "transition: track=%d expected=%d position=%lld duration=%lld state=%d status=%d error=%s\n", player.trackId(), ts[1].toMap()["id"].toInt(), player.position(), player.duration(), player.media.playbackState(), player.media.mediaStatus(), qPrintable(player.media.errorString()));
            require(transitioned, "automatic file transition");
            player.open(m4b, true);
            require(waitFor([&] { return player.playing() && player.duration() > 5000; }), "app M4B playback");
            player.pause(); player.seek(1800); player.setSpeed(1.5);
            require(player.persist(), "save progress");
            lib.setFlag(m4b, "favorite", true);
            lib.edit(m4b, {{"title", "My corrected synthetic title"}, {"narrator", "Synthetic Narrator"}});
            player.bookmark("Synthetic bookmark");
            lib.refresh();
            require(waitFor([&] { return !lib.scanning(); }), "rescan timeout");
            require(lib.book(m4b)["title"] == "My corrected synthetic title", "override survived rescan");
        } else if (phase == "restart") {
            require(player.bookId() > 0 && !player.playing(), "restart restores selection paused");
            require(waitFor([&] { return player.media.isSeekable(); }), "restart loaded");
            require(qAbs(player.position() - 1800) < 100 && player.speed() == 1.5, "restart position and speed");
            auto b = lib.book(player.bookId());
            require(b["favorite"].toBool() && b["title"] == "My corrected synthetic title", "personal state after restart");
            require(lib.bookmarks(player.bookId()).size() == 1, "bookmark after restart");
        } else if (phase == "features") {
            const int id = player.bookId();
            require(id > 0 && waitFor([&] { return player.media.isSeekable(); }), "feature book loaded");
            const auto snapshot = lib.book(id);
            auto refresh = [&] { lib.refresh(); require(waitFor([&] { return !lib.scanning(); }, 20000), "refresh timeout"); };
            lib.query("Synthetic Narrator", 3, 0); require(lib.rowCount() == 1, "narrator search and favorites");
            lib.query("", 0, 0);
            int mp3 = 0;
            for (int i = 0; i < lib.rowCount(); ++i) { auto b = lib.data(lib.index(i), Qt::UserRole).toMap(); if (!b["identity"].toString().endsWith(".m4b")) mp3 = b["id"].toInt(); }
            lib.addRoot(QUrl::fromLocalFile(root + "/Synthetic MP3 book"));
            require(waitFor([&] { return !lib.scanning(); }), "overlapping root scan");
            require(lib.rowCount() == 2, "overlapping roots duplicated books");
            const auto blocked = root + "/unreadable";
            require(QDir().mkdir(blocked) && QFile::setPermissions(blocked, {}), "unreadable directory fixture");
            refresh();
            const bool childAvailable = lib.book(mp3)["available"].toBool();
            require(QFile::setPermissions(blocked, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner) && QDir().rmdir(blocked), "restore directory permissions");
            require(childAvailable && lib.error().contains("not reconciled"), "failed parent scan hid healthy child root");
            lib.removeRoot(root + "/Synthetic MP3 book");
            require(lib.book(id)["available"].toBool(), "overlapping root removal lost availability");
            const auto artwork = lib.book(mp3)["cover"].toString();
            require(!artwork.isEmpty() && QFileInfo::exists(artwork), "embedded artwork import");
            require(QDir(lib.cacheDir).removeRecursively(), "clear generated cover cache");
            lib.sql("UPDATE tracks SET probe=json_remove(probe,'$.oma_cover') WHERE id=?", {lib.tracks(mp3).first().toMap()["id"]});
            refresh(); require(QFileInfo::exists(artwork), "unchanged media did not regenerate artwork");
            auto selectedArt = lib.sql("SELECT json_extract(probe,'$.oma_cover') FROM tracks WHERE book_id=? ORDER BY ordinal,path LIMIT 1", {mp3});
            require(selectedArt.next() && QFileInfo::exists(selectedArt.value(0).toString()), "selected artwork without extraction history was not retried");
            require(lib.book(mp3)["seriesOrder"] == "2", "Libation PART series order");
            const auto mp3Tracks = lib.tracks(mp3);
            player.open(mp3, true); require(waitFor([&] { return player.playing() && player.duration() > 0; }), "MP3 sleep setup");
            const int firstTrack = mp3Tracks.first().toMap()["id"].toInt();
            player.jump(firstTrack, 3600, true);
            require(waitFor([&] { return player.playing() && player.position() >= 3600; }), "MP3 EOF sleep load");
            player.sleep(-1);
            require(waitFor([&] { return !player.playing() && player.position() >= 3900; }), "MP3 EOF sleep");
            QElapsedTimer settle; settle.start(); waitFor([&] { return settle.elapsed() > 400; }, 500);
            require(player.trackId() == firstTrack && !player.playing(), "MP3 EOF sleep auto-advanced");
            player.previous();
            require(waitFor([&] { return player.trackId() == firstTrack && player.position() < 100; }), "Previous at intermediate file EOF jumped ahead");
            player.open(id, false); require(waitFor([&] { return player.media.isSeekable(); }), "return to M4B");
            player.seek(2700); player.sleep(-1); player.play();
            require(waitFor([&] { return !player.playing() && player.position() >= 3000; }), "end-of-chapter sleep");
            require(player.position() < 3400, "sleep overshot chapter");
            player.seek(3500); player.next();
            require(player.position() >= 3500, "next on last chapter went backward");
            player.previous(); require(waitFor([&] { return player.position() < 100; }), "previous chapter navigation");
            int sourceChanges = 0;
            const auto sourceConnection = QObject::connect(&player.media, &QMediaPlayer::sourceChanged, [&] { ++sourceChanges; });
            player.seek(1200); player.next(); require(waitFor([&] { return player.media.isSeekable() && qAbs(player.media.position() - 3000) < 100; }), "next embedded chapter");
            require(sourceChanges == 0, "jumping within the same file reopened the decoder");
            QObject::disconnect(sourceConnection);
            player.seek(player.duration()); player.previous();
            require(waitFor([&] { return qAbs(player.position() - 3000) < 100; }), "previous at exact EOF");
            lib.setFlag(id, "finished", true); lib.query("", 2, 0); require(lib.rowCount() == 1, "finished filter");
            lib.setFlag(id, "finished", false); lib.query("", 0, 0);
            require(qAbs(player.position() - 3000) < 100 && lib.book(id)["offset"].toLongLong() >= 2900, "mark unfinished discarded progress");
            player.startOver(id);
            require(waitFor([&] { return player.playing() && player.position() < 300; }), "start over did not restart active book");
            player.pause();
            require(lib.book(id)["favorite"] == snapshot["favorite"] && lib.bookmarks(id).size() == 1 && player.speed() == 1.5, "start over lost personal state");
            player.open(mp3, false);
            player.jump(mp3Tracks.last().toMap()["id"].toInt(), 1800, false);
            require(waitFor([&] { return player.media.isSeekable(); }), "replay setup");
            lib.setFlag(mp3, "finished", true);
            lib.sql("PRAGMA query_only=ON"); player.startOver(mp3); lib.sql("PRAGMA query_only=OFF");
            require(player.trackId() == mp3Tracks.last().toMap()["id"].toInt() && player.position() >= 1800 && lib.book(mp3)["finished"].toBool(), "failed restart discarded progress");
            player.open(id, false); player.seek(1800);
            player.startOver(mp3);
            require(waitFor([&] { return player.playing() && player.trackId() == firstTrack && player.position() < 300; }), "replay did not start first file");
            require(!lib.book(mp3)["finished"].toBool() && lib.book(id)["offset"].toLongLong() >= 1800, "replay failed to clear completion or save outgoing book");
            player.open(id, false);
            QImage cover(32, 32, QImage::Format_RGB32); cover.fill(Qt::blue);
            require(cover.save(base + "/cover.png"), "synthetic cover creation");
            lib.setCover(id, QUrl::fromLocalFile(base + "/cover.png"));
            require(lib.book(id)["cover"].toString().startsWith(lib.dataDir), "cover stored as owned copy");
            QFile::remove(base + "/cover.png");
            require(QFileInfo::exists(lib.book(id)["cover"].toString()), "cover depends on external original");
            QFile malformed(root + "/broken.mp3"); require(malformed.open(QIODevice::WriteOnly), "malformed fixture"); malformed.write("SYNTHETIC invalid media"); malformed.close();
            refresh(); require(!lib.error().isEmpty() && lib.rowCount() == 2, "malformed media reporting");
            QFile::remove(malformed.fileName());
            lib.refresh(); lib.cancelScan(); require(waitFor([&] { return !lib.scanning(); }), "cancel timeout");
            require(lib.rowCount() == 2, "cancel erased catalog");
            player.pause();
            require(QDir().rename(root, root + " offline"), "synthetic disconnect");
            refresh(); require(!lib.book(id)["available"].toBool(), "missing root availability");
            require(lib.book(id)["favorite"] == snapshot["favorite"] && lib.bookmarks(id).size() == 1, "disconnect lost personal state");
            require(QDir().rename(root + " offline", root), "synthetic reconnect");
            refresh(); require(lib.book(id)["available"].toBool(), "reconnected root unavailable");
            const auto missingFile = mp3Tracks.first().toMap()["path"].toString();
            require(QFile::rename(missingFile, missingFile + ".offline"), "individual missing fixture");
            refresh(); require(!lib.book(mp3)["available"].toBool() && lib.tracks(mp3).size() == 2, "missing individual track not retained");
            require(QFile::rename(missingFile + ".offline", missingFile), "individual file reconnect");
            refresh(); require(lib.book(mp3)["available"].toBool(), "individual file not recovered");
            require(QFile::rename(missingFile, missingFile + ".offline"), "failed-load fixture backup");
            QFile invalid(missingFile); require(invalid.open(QIODevice::WriteOnly), "failed-load fixture"); invalid.write("SYNTHETIC invalid media"); invalid.close();
            player.open(mp3, false); player.seek(1200);
            require(waitFor([&] { return player.media.mediaStatus() == QMediaPlayer::InvalidMedia; }), "invalid media not rejected");
            require(QFile::remove(missingFile) && QFile::rename(missingFile + ".offline", missingFile), "repair failed-load fixture");
            player.play(); require(waitFor([&] { return player.playing() && player.position() >= 1200; }), "Resume did not reload repaired media");
            player.open(id, false); require(waitFor([&] { return player.media.isSeekable(); }), "return after failed-load recovery");
            lib.removeRoot(root); require(!lib.book(id)["available"].toBool() && lib.rowCount() == 2, "root removal did not retain unavailable catalog");
            lib.addRoot(QUrl::fromLocalFile(root)); require(waitFor([&] { return !lib.scanning(); }), "root re-add");
            require(lib.book(id)["available"].toBool() && lib.book(id)["title"] == snapshot["title"], "re-added root lost state");
            const auto original = lib.book(id)["identity"].toString();
            const auto moved = root + "/Relocated synthetic.m4b";
            require(QFile::rename(original, moved), "synthetic relocation");
            refresh(); require(lib.rowCount() == 3, "relocated target discovered before repair");
            int targetId = 0;
            for (int i = 0; i < lib.rowCount(); ++i) { auto b = lib.data(lib.index(i), Qt::UserRole).toMap(); if (b["identity"] == moved) targetId = b["id"].toInt(); }
            lib.setFlag(targetId, "favorite", true);
            lib.relink(id, QUrl::fromLocalFile(moved));
            require(lib.book(id)["identity"] == original && lib.book(targetId)["favorite"].toBool() && lib.bookmarks(id).size() == 1, "relink conflict lost personal state");
            lib.setFlag(targetId, "favorite", false);
            lib.relink(id, QUrl::fromLocalFile(moved)); require(waitFor([&] { return !lib.scanning(); }), "relink rescan");
            require(lib.book(id)["identity"] == moved && lib.bookmarks(id).size() == 1 && lib.rowCount() == 2, "relink lost identity/state or failed to reconcile scanned target");
            player.open(id); require(waitFor([&] { return player.playing() && player.media.source().toLocalFile() == moved; }), "Resume after relink used stale source");
            player.pause();
            require(QFile::rename(moved, original), "restore synthetic path");
            lib.relink(id, QUrl::fromLocalFile(original)); require(waitFor([&] { return !lib.scanning(); }), "relink restore");
            Theme theme;
            const auto themeDir = qEnvironmentVariable("XDG_STATE_HOME") + "/omarchy/current/theme";
            QDir().mkpath(themeDir);
            auto palette = [&](const QByteArray &text) { QFile f(themeDir + "/colors.toml"); require(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "write synthetic theme"); f.write(text); };
            theme.setMode("Follow Omarchy");
            palette("background = \"#fafafa\"\nforeground = \"#101010\"\naccent = \"#003388\"\n");
            require(waitFor([&] { return theme.background == QColor("#fafafa"); }), "live light theme");
            QFile::remove(themeDir + "/colors.toml");
            palette("background = \"#101010\"\nforeground = \"#fafafa\"\naccent = \"#88aaff\"\n");
            require(waitFor([&] { return theme.background == QColor("#101010"); }), "replaced theme watch");
            palette("invalid theme"); require(waitFor([&] { return theme.background == QColor("#15191e"); }), "invalid theme fallback");
            require(lib.book(id)["title"] == snapshot["title"], "override lost during feature checks");
            player.jump(lib.tracks(id).first().toMap()["id"].toInt(), 1800, false);
            require(waitFor([&] { return player.media.isSeekable() && qAbs(player.position() - 1800) < 100; }), "post-relink resume");
            require(player.persist(), "feature final save");
            QObject busObject;
            MprisPlayer mpris(&busObject, player, lib);
            mpris.SetPosition(mpris.trackPath(), 2000000);
            require(qAbs(player.position() - 2000) < 100, "MPRIS SetPosition adapter");
            mpris.SetPosition(QDBusObjectPath("/stale"), 4000000);
            require(qAbs(player.position() - 2000) < 100, "MPRIS stale track guard");
            mpris.Seek(-500000); require(qAbs(player.position() - 1500) < 100, "MPRIS microsecond Seek");
            require(mpris.metadata()["mpris:length"].toLongLong() == player.duration() * 1000, "MPRIS metadata units");
            mpris.Stop(); require(mpris.status() == "Stopped" && player.position() == 0, "MPRIS Stop");
            lib.sql("PRAGMA query_only=ON");
            require(!player.persist() && lib.error().contains("NOT saved"), "database write failure not surfaced");
            player.open(mp3); require(player.bookId() == id, "book switch discarded unsaved state");
            lib.sql("PRAGMA query_only=OFF"); player.seek(1800);
        } else if (phase == "layouts") {
            lib.refresh(); require(waitFor([&] { return !lib.scanning(); }, 20000), "layout scan");
            require(lib.rowCount() == 5, "separate M4Bs, disc grouping, ambiguous album rejection, symlink exclusion");
            require(lib.error().contains("ambiguous MP3 grouping"), "ambiguous layout not explained");
            for (int i = 0; i < lib.rowCount(); ++i) {
                auto b = lib.data(lib.index(i), Qt::UserRole).toMap();
                auto ts = lib.tracks(b["id"].toInt());
                if (b["title"] == "Partial discs") require(ts.first().toMap()["path"].toString().endsWith("Disc 1/1.mp3"), "partial disc tags must fall back to natural order");
                if (b["title"] == "Tagged discs") require(ts.size() == 3 && ts.first().toMap()["path"].toString().endsWith("Disc 1/10.mp3") && ts.last().toMap()["path"].toString().endsWith("Disc 2/1.mp3"), "complete disc/track order must override filenames");
            }
            QList<int> ids;
            for (int i = 0; i < lib.rowCount(); ++i) ids << lib.data(lib.index(i), Qt::UserRole).toMap()["id"].toInt();
            lib.sql("SAVEPOINT series_smoke");
            const QStringList numbers = {"10", "2", "2.5", "1", "0"};
            for (int i = 0; i < ids.size(); ++i) {
                lib.edit(ids[i], {{"series", i % 2 ? "  THE SAGA  " : "The Saga"}, {"seriesOrder", numbers[i]}, {"title", QString::number(i)}});
                lib.setFlag(ids[i], "favorite", i == 1 || i == 2);
                lib.setFlag(ids[i], "finished", i == 0 || i == 3);
            }
            auto expectOrder = [&](const QList<int> &expected) {
                QList<int> actual;
                for (int i = 0; i < lib.rowCount(); ++i) actual << lib.data(lib.index(i), Qt::UserRole).toMap()["id"].toInt();
                require(actual == expected, "series order, grouping or filter mismatch");
            };
            lib.query("", 0, 4); expectOrder({ids[4], ids[3], ids[1], ids[2], ids[0]});
            auto grouped = lib.bookRows(2, true);
            require(grouped.size() == 3 && grouped[0].toMap()["heading"] == "The Saga" && grouped[1].toMap()["heading"].toString().isEmpty()
                    && grouped[2].toMap()["startIndex"] == 4, "series must span cover rows under one heading");
            require(lib.bookRows(0, true).size() == 5, "narrow grouped view must retain all books");
            lib.reload(); expectOrder({ids[4], ids[3], ids[1], ids[2], ids[0]});
            lib.query("saga", 3, 4); expectOrder({ids[1], ids[2]});
            lib.query("SAGA", 2, 4); expectOrder({ids[3], ids[0]});
            lib.query("absent series", 0, 4); expectOrder({});
            require(lib.bookRows(3, true).isEmpty() && lib.bookRows(3, false).isEmpty(), "empty filter left series headings behind");
            lib.query("", 0, 4);
            lib.edit(ids[2], {{"seriesOrder", "20"}});
            for (const auto &number : {"", "Book 2", "nan", "inf", "-1", "1,5"}) {
                lib.edit(ids[0], {{"seriesOrder", number}});
                expectOrder({ids[4], ids[3], ids[1], ids[2], ids[0]});
            }
            lib.edit(ids[1], {{"seriesOrder", "unknown"}});
            expectOrder({ids[4], ids[3], ids[2], ids[0], ids[1]}); // Unknown numbers sort by title.
            lib.edit(ids[0], {{"series", "Zulu"}});
            lib.edit(ids[1], {{"series", " alpha "}, {"seriesOrder", "2"}});
            lib.edit(ids[2], {{"series", "ALPHA"}, {"seriesOrder", "1"}});
            lib.edit(ids[3], {{"series", "   "}});
            lib.edit(ids[4], {{"series", ""}});
            expectOrder({ids[2], ids[1], ids[0], ids[3], ids[4]}); // No series ignores stray order tags.
            grouped = lib.bookRows(3, true);
            require(grouped.size() == 3 && grouped[0].toMap()["heading"] == "ALPHA" && grouped[1].toMap()["heading"] == "Zulu"
                    && grouped[2].toMap()["heading"] == "No series", "series must start new rows, with No series last");
            const auto lastBooks = grouped[2].toMap()["books"].toList();
            require(lastBooks.size() == 2 && lastBooks[0].toMap()["visibleIndex"] == 3 && lastBooks[1].toMap()["id"] == ids[4], "grouped cards lost flat book indices");
            const auto continuous = lib.bookRows(3, false);
            require(continuous.size() == 2 && continuous[0].toMap()["heading"].toString().isEmpty() && continuous[1].toMap()["heading"].toString().isEmpty(), "hidden headings must pack series into continuous rows");
            QList<int> continuousIds;
            for (const auto &row : continuous) for (const auto &book : row.toMap()["books"].toList()) continuousIds << book.toMap()["id"].toInt();
            require(continuous[0].toMap()["books"].toList().size() == 3 && continuousIds == QList<int>{ids[2], ids[1], ids[0], ids[3], ids[4]}, "continuous rows lost series order or left empty slots");
            lib.edit(ids[1], {{"seriesOrder", "1"}, {"title", "Same title"}});
            lib.edit(ids[2], {{"title", "Same title"}});
            expectOrder({qMin(ids[1], ids[2]), qMax(ids[1], ids[2]), ids[0], ids[3], ids[4]});
            lib.sql("ROLLBACK TO series_smoke"); lib.sql("RELEASE series_smoke"); lib.reload(); lib.query("", 0, 0);
            grouped = lib.bookRows(3, true);
            require(grouped.size() == 2 && grouped[0].toMap()["heading"].toString().isEmpty() && grouped[1].toMap()["heading"].toString().isEmpty(), "other sort modes must remain a flat cover grid");
            fprintf(stdout, "PASS: series numeric/decimal ordering, normalized names, missing/invalid metadata, filters, stable ties and refresh\n");
        } else throw std::runtime_error("unknown smoke phase");
        fprintf(stdout, "PASS: application integration %s\n", qPrintable(phase));
        return 0;
    } catch (const std::exception &e) { fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}
