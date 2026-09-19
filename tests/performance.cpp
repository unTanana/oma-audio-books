#include "app.h"
#include "checks.h"
#include "desktop.h"
#include <QGuiApplication>
#include <QDateTime>
#include <QQuickItem>
#include <QKeyEvent>
#include <QPointer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <cstdio>
#ifdef __GLIBC__
#include <malloc.h>
#endif

int main(int argc, char **argv) {
    QTemporaryDir tmp(QDir::tempPath() + "/oma-audio-books-synthetic-performance-XXXXXX");
    if (!tmp.isValid()) return 1;
    for (const auto name : {"XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"})
        qputenv(name, (tmp.path() + '/' + name).toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_QPA_PLATFORMTHEME", "offscreen"); qputenv("QT_NO_XDG_DESKTOP_PORTAL", "1"); qputenv("OMA_HEADLESS", "1");
    QGuiApplication app(argc, argv);
    app.setOrganizationName("oma-audio-books"); app.setApplicationName("performance-check");
    QQuickStyle::setStyle("Basic");
    try {
        const int count = qBound(2, app.arguments().value(1, "1000").toInt(), 10000);
        const bool benchmarkOnly = app.arguments().contains("--benchmark");
        Library lib;
        const auto root = tmp.path() + "/media"; require(QDir().mkpath(root), "media directory");
        QJsonArray chapters;
        for (int i = 0; i < 200; ++i) chapters.append(QJsonObject{{"start_time", QString::number(i * 60)}, {"end_time", QString::number((i + 1) * 60)}, {"tags", QJsonObject{{"title", QString("Chapter %1").arg(i + 1)}}}});
        const auto probe = QString::fromUtf8(QJsonDocument(QJsonObject{{"chapters", chapters}, {"format", QJsonObject{{"tags", QJsonObject{{"title", "Book"}}}}}}).toJson(QJsonDocument::Compact));
        lib.sql("BEGIN");
        for (int id = 1; id <= count; ++id) {
            const auto path = root + '/' + QString::number(id) + ".m4b";
            QFile file(path); require(file.open(QIODevice::WriteOnly), "synthetic file"); file.close();
            const auto metadata = QString::fromUtf8(QJsonDocument(QJsonObject{{"title", QString("Book %1").arg(id, 5, 10, QChar('0'))}, {"author", "Synthetic author"}, {"series", QString("Series %1").arg(id / 5)}, {"seriesOrder", QString::number(id % 5)}}).toJson(QJsonDocument::Compact));
            lib.sql("INSERT INTO books(id,identity,imported,added) VALUES(?,?,?,?)", {id, path, metadata, id});
            lib.sql("INSERT INTO tracks(id,book_id,path,ordinal,duration,size,mtime,probe) VALUES(?,?,?,0,12000000,0,?,?)", {id, id, path, QFileInfo(path).lastModified().toMSecsSinceEpoch(), probe});
        }
        lib.sql("COMMIT");
        auto measure = [&](const char *name, int repeats, const auto &action) {
            QList<double> times;
            for (int i = 0; i < repeats; ++i) { QElapsedTimer clock; clock.start(); action(); times << clock.nsecsElapsed() / 1e6; }
            std::sort(times.begin(), times.end());
            fprintf(stdout, "%s median_ms=%.3f max_ms=%.3f books=%d\n", name, times[times.size()/2], times.last(), count);
        };
        measure("catalog_reload", 3, [&] { lib.reload(); });
        measure("series_query", 5, [&] { lib.query("", 0, 4); });
        measure("search", 5, [&] { lib.query("book 00", 0, 4); }); lib.query("", 0, 4);
        measure("10000_book_lookups", 3, [&] { for (int i = 0; i < 10000; ++i) require(!lib.book(i % count + 1).isEmpty(), "book lookup"); });
        measure("track_navigation_data", 5, [&] { lib.tracks(count); });
        measure("chapter_navigation_data", 5, [&] { lib.chapters(count); });
        if (!benchmarkOnly) {
            const auto tracks = lib.tracks(count); const auto track = tracks.first().toMap();
            require(tracks.size() == 1 && track["id"].toInt() == count && track["available"].toBool()
                && track["duration"].toLongLong() == 12000000 && track["path"].toString().endsWith(QString::number(count) + ".m4b")
                && !track.contains("probe"), "navigation tracks must retain lean playback fields without probe metadata");
            const auto cs = lib.chapters(count);
            require(cs.size() == 200 && cs.first().toMap()["track"].toInt() == count
                && cs.last().toMap()["start"].toLongLong() == 11940000 && cs.last().toMap()["end"].toLongLong() == 12000000
                && cs.last().toMap()["title"] == "Chapter 200", "direct chapter parsing changed navigation data");
            for (const auto json : {"{}", "not json", "{\"chapters\":42}", R"({"chapters":"[{\"start_time\":\"1\"}]"})"}) {
                lib.sql("UPDATE tracks SET probe=? WHERE id=?", {json, count});
                const auto fallback = lib.chapters(count);
                require(fallback.size() == 1 && fallback.first().toMap()["end"].toLongLong() == 12000000
                    && fallback.first().toMap()["title"] == QString::number(count) + ".m4b", "missing/malformed chapter fallback changed");
            }
            lib.sql("UPDATE tracks SET probe=? WHERE id=?", {probe, count});
        }
        int saves = 0, resets = 0, updatedRows = 0;
        QObject::connect(&lib, &QAbstractItemModel::modelReset, &app, [&] { ++resets; });
        QObject::connect(&lib, &QAbstractItemModel::dataChanged, &app, [&](const QModelIndex &first, const QModelIndex &last) { updatedRows += last.row() - first.row() + 1; });
        measure("progress_save", 5, [&] { require(lib.save(count, count, ++saves * 1000, 1, true), "save"); });
        if (!benchmarkOnly) require(resets == 0 && updatedRows == 5, "saving one book must update only that row without resetting the model");
        lib.settings.remove("currentBook");
        Player player(lib); Theme theme;
        QQmlApplicationEngine engine;
        bool warnings = false;
        QObject::connect(&engine, &QQmlEngine::warnings, &app, [&](const QList<QQmlError> &) { warnings = true; });
        engine.rootContext()->setContextProperty("library", &lib); engine.rootContext()->setContextProperty("player", &player);
        engine.rootContext()->setContextProperty("theme", &theme); engine.rootContext()->setContextProperty("suggestedFolder", QUrl::fromLocalFile(tmp.path()));
        measure("ui_load", 1, [&] { engine.load(QUrl("qrc:/Main.qml")); });
        require(!engine.rootObjects().isEmpty(), "window");
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->grabWindow();
        measure("ui_progress_save", 5, [&] { require(lib.save(count, count, ++saves * 1000, 1, true), "UI save"); QCoreApplication::processEvents(); });
        measure("open_200_chapters", 1, [&] { window->setProperty("selectedBook", count); window->grabWindow(); });
        measure("details_progress_save", 5, [&] { require(lib.save(count, count, ++saves * 1000, 1, true), "details save"); QCoreApplication::processEvents(); });
        if (!benchmarkOnly) {
            lib.edit(count, {{"description", QString("A long book description that must stay reachable. ").repeated(60)}});
            window->setProperty("selectedBook", 0); window->resize(820, 620); window->grabWindow();
            require(QMetaObject::invokeMethod(window, "openDetails", Q_ARG(QVariant, count)), "open long details");
            QCoreApplication::processEvents(); window->grabWindow();
            auto header = window->findChild<QQuickItem *>("detailHeader");
            auto list = window->findChild<QQuickItem *>("chapterList");
            require(header && list && qAbs(header->mapToItem(list, QPointF()).y()) < 1, "long details opened scrolled past the title");
            require(list && list->property("count").toInt() == 200, "chapter list");
            auto content = list->property("contentItem").value<QQuickItem *>();
            require(content && content->childItems().size() < 50, "chapter delegates must be virtualized");
            require(QMetaObject::invokeMethod(list, "positionViewAtEnd"), "last chapter navigation");
            window->grabWindow();
            require(list->property("atYEnd").toBool(), "last chapter inaccessible");
            list->forceActiveFocus(); list->setProperty("currentIndex", 0);
            for (int i = 0; i < 199; ++i) {
                QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier); QCoreApplication::sendEvent(window, &down);
            }
            window->grabWindow();
            require(list->property("currentIndex").toInt() == 199, "keyboard cannot reach virtualized chapters");
            auto current = list->property("currentItem").value<QQuickItem *>();
            require(current && current->hasActiveFocus(), "keyboard chapter selection lost button focus");
            list->setProperty("currentIndex", -1); list->setProperty("contentY", 300);
            lib.edit(count, {{"description", QString("A growing book description. ").repeated(180)}});
            window->grabWindow();
            require(qAbs(list->property("contentY").toReal() - 300) < 1, "header growth moved manually scrolled details");
            const auto position = list->property("contentY");
            QList<QPointer<QQuickItem>> children;
            for (auto item : content->childItems()) children << item;
            lib.save(count, count, ++saves * 1000, 1, true); QCoreApplication::processEvents();
            require(list->property("contentY") == position, "progress save moved chapter scroll");
            for (const auto &child : children) require(child, "progress save recreated chapter delegates");
            lib.query("", 1, 4); require(lib.rowCount() == 1, "progress filter lost scoped save");
            lib.setFlag(count, "finished", true); require(lib.rowCount() == 0, "completion failed to update filter");
            lib.query("", 2, 4); require(lib.rowCount() == 1, "finished filter");
            lib.setFlag(count, "finished", false); require(lib.rowCount() == 0, "unfinished failed to update filter");
            lib.query("", 0, 3); lib.save(1, 1, 1234, 1.5, true);
            require(lib.book(1)["offset"].toInt() == 1234 && lib.book(1)["speed"].toDouble() == 1.5, "scoped save cache stale");
            lib.query("", 0, 4);
        }
        window->setProperty("selectedBook", 0);
        auto memory = [](const char *label) {
#ifdef __GLIBC__
            const auto m = mallinfo2();
            fprintf(stdout, "%s allocated_bytes=%zu\n", label, m.uordblks + m.hblkhd);
#else
            Q_UNUSED(label);
#endif
        };
        memory("before_scan");
        auto waitScan = [&] {
            QElapsedTimer total; total.start(); double longest = 0;
            while (lib.scanning() && total.elapsed() < 30000) {
                QElapsedTimer event; event.start(); QCoreApplication::processEvents();
                longest = qMax(longest, event.nsecsElapsed() / 1e6); QThread::msleep(1);
            }
            require(!lib.scanning(), "refresh timeout");
            fprintf(stdout, "scan_largest_event_ms=%.3f\n", longest);
            memory("after_scan");
        };
        lib.sql("INSERT INTO roots VALUES(?)", {root});
        measure("refresh_dispatch", 1, [&] { lib.refresh(); });
        waitScan();
        require(lib.error().isEmpty(), "synthetic cached refresh failed");
        auto changes = [&] { auto q = lib.sql("SELECT total_changes()"); q.next(); return q.value(0).toInt(); };
        const auto before = changes();
        window->setProperty("selectedBook", count); QCoreApplication::processEvents(); window->grabWindow();
        auto list = window->findChild<QQuickItem *>("chapterList");
        require(list, "chapter list during refresh");
        list->setProperty("highlightMoveDuration", 0);
        list->setProperty("currentIndex", 50); window->grabWindow();
        const auto position = list->property("contentY");
        QPointer<QQuickItem> chapter = list->property("currentItem").value<QQuickItem *>();
        require(chapter, "selected chapter during refresh");
        int notifications = 0;
        QObject::connect(&lib, &Library::bookChanged, &app, [&](int) { ++notifications; });
        QObject::connect(&lib, &Library::chaptersChanged, &app, [&] { ++notifications; });
        QObject::connect(&lib, &Library::bookmarksChanged, &app, [&](int) { ++notifications; });
        measure("unchanged_refresh", 3, [&] { lib.refresh(); waitScan(); });
        if (!benchmarkOnly) {
            require(changes() == before, "unchanged scan rewrote catalog rows");
            fprintf(stdout, "unchanged_scan_notifications=%d chapter_retained=%d scroll_before=%.3f scroll_after=%.3f\n",
                notifications, !chapter.isNull(), position.toReal(), list->property("contentY").toReal());
            require(notifications == 0 && chapter && list->property("contentY") == position,
                "unchanged scan invalidated details or moved chapter selection");
            lib.sql("UPDATE tracks SET available=0 WHERE id=?", {count});
            lib.refresh(); waitScan();
            const auto recovered = lib.tracks(count).first().toMap();
            require(recovered["available"].toBool() && notifications > 0, "changed scan must restore availability and notify the UI");
        }
        require(!warnings, "QML warnings");
        fprintf(stdout, "PASS: isolated performance workload\n");
    } catch (const std::exception &e) { fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}
