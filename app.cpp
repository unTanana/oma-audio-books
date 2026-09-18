#include "app.h"
#include <QtConcurrent>
#include <QAudioBufferOutput>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QSqlError>
#include <QStandardPaths>
#include <QUuid>
#include <cmath>
#include <stdexcept>

static QString json(const QJsonObject &o) { return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)); }
static QJsonObject object(const QVariant &s) { return QJsonDocument::fromJson(s.toString().toUtf8()).object(); }
QSqlQuery Library::sql(const QString &text, const QVariantList &args) const {
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (!q.prepare(text)) throw std::runtime_error(q.lastError().text().toStdString());
    for (const auto &arg : args) q.addBindValue(arg);
    if (!q.exec()) throw std::runtime_error(q.lastError().text().toStdString());
    return q;
}
Library::Library(QObject *parent) : QAbstractListModel(parent) {
    dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/covers";
    if (!QDir().mkpath(dataDir) || !QDir().mkpath(cacheDir)) throw std::runtime_error("Cannot create application data/cache directories");
    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(dataDir + "/library.sqlite");
    if (!db.open()) throw std::runtime_error(db.lastError().text().toStdString());
    sql("PRAGMA foreign_keys=ON");
    sql("PRAGMA busy_timeout=3000");
    sql("PRAGMA journal_mode=WAL");
    auto version = sql("PRAGMA user_version"); version.next();
    if (version.value(0).toInt() > 1) throw std::runtime_error("Catalog is newer than this application; refusing to modify it");
    sql("BEGIN IMMEDIATE");
    try {
        sql("CREATE TABLE IF NOT EXISTS roots(path TEXT PRIMARY KEY)");
        sql("CREATE TABLE IF NOT EXISTS books(id INTEGER PRIMARY KEY, identity TEXT UNIQUE NOT NULL, imported TEXT NOT NULL, overrides TEXT NOT NULL DEFAULT '{}', favorite INTEGER NOT NULL DEFAULT 0, finished INTEGER NOT NULL DEFAULT 0, added INTEGER NOT NULL, listened INTEGER NOT NULL DEFAULT 0, current_track INTEGER, offset INTEGER NOT NULL DEFAULT 0, speed REAL NOT NULL DEFAULT 1)");
        sql("CREATE TABLE IF NOT EXISTS tracks(id INTEGER PRIMARY KEY, book_id INTEGER NOT NULL REFERENCES books(id), path TEXT UNIQUE NOT NULL, ordinal INTEGER NOT NULL, duration INTEGER NOT NULL, size INTEGER NOT NULL, mtime INTEGER NOT NULL, probe TEXT NOT NULL, available INTEGER NOT NULL DEFAULT 1)");
        sql("CREATE INDEX IF NOT EXISTS book_tracks ON tracks(book_id, ordinal)");
        sql("CREATE TABLE IF NOT EXISTS bookmarks(id INTEGER PRIMARY KEY, book_id INTEGER NOT NULL REFERENCES books(id), track_id INTEGER NOT NULL REFERENCES tracks(id), offset INTEGER NOT NULL, label TEXT NOT NULL)");
        sql("PRAGMA user_version=1"); sql("COMMIT");
    } catch (...) { sql("ROLLBACK"); throw; }
    connect(&worker, &QFutureWatcher<ScanResult>::finished, this, [this] {
        apply(worker.future().takeResult()); scanActive = false; emit scanChanged();
    });
    reload();
}
Library::~Library() { *cancel = true; worker.waitForFinished(); db.close(); }
void Library::fail(const QString &text) { message = text; emit errorChanged(); }
QStringList Library::roots() const {
    QStringList result;
    auto q = sql("SELECT path FROM roots ORDER BY path");
    while (q.next()) result << q.value(0).toString();
    return result;
}
void Library::addRoot(const QUrl &url) {
    if (scanning()) { fail("Finish or cancel the scan before adding a root."); return; }
    try {
        const QFileInfo fi(url.toLocalFile());
        if (!url.isLocalFile() || !fi.isDir() || !fi.isReadable()) { fail("Choose a readable local library folder."); return; }
        sql("INSERT OR IGNORE INTO roots VALUES(?)", {fi.canonicalFilePath()});
        emit rootsChanged(); refresh();
    } catch (const std::exception &e) { fail(e.what()); }
}
void Library::removeRoot(const QString &path) {
    if (scanning()) { fail("Cancel or finish the scan before removing a root."); return; }
    try {
        sql("BEGIN IMMEDIATE");
        sql("DELETE FROM roots WHERE path=?", {path});
        const auto active = roots();
        auto q = sql("SELECT id,path FROM tracks");
        while (q.next()) {
            bool covered = false;
            for (const auto &root : active) covered |= within(q.value(1).toString(), root);
            if (!covered) sql("UPDATE tracks SET available=0 WHERE id=?", {q.value(0)});
        }
        sql("COMMIT"); reload(); emit rootsChanged();
    } catch (const std::exception &e) { db.rollback(); fail(e.what()); }
}
void Library::refresh() {
    if (scanning()) return;
    try {
        QList<QVariantList> cached;
        auto q = sql("SELECT path,size,mtime,duration,probe FROM tracks");
        while (q.next()) cached << QVariantList{q.value(0), q.value(1), q.value(2), q.value(3), q.value(4)};
        *cancel = false; scanActive = true; fail({});
        worker.setFuture(QtConcurrent::run([cached = std::move(cached), roots = roots(), cacheDir = cacheDir, cancel = cancel]() mutable {
            QMap<QString, MediaFile> files;
            while (!cached.isEmpty()) {
                if (*cancel) { ScanResult result; result.canceled = true; return result; }
                const auto v = cached.takeLast();
                files[v[0].toString()] = {v[0].toString(), {}, v[1].toLongLong(), v[2].toLongLong(), v[3].toLongLong(), object(v[4])};
            }
            auto result = scanMedia(roots, files, cacheDir, cancel);
            // Unchanged probes stay in SQLite; release their large JSON on this worker.
            for (auto &file : result.files) if (!file.probeChanged) file.probe = {};
            return result;
        }));
        emit scanChanged();
    } catch (const std::exception &e) { fail(e.what()); }
}
void Library::apply(const ScanResult &r) {
    if (r.canceled) { fail("Scan canceled. Catalog and personal state retained."); return; }
    try {
        auto before = sql("SELECT total_changes()"); before.next();
        sql("BEGIN IMMEDIATE");
        QSet<QString> found;
        for (const auto &f : r.files) found.insert(f.path);
        auto old = sql("SELECT id,path FROM tracks WHERE available<>0");
        while (old.next()) if (!found.contains(old.value(1).toString())) for (const auto &root : r.reconciled)
            if (within(old.value(1).toString(), root)) { sql("UPDATE tracks SET available=0 WHERE id=?", {old.value(0)}); break; }
        QMap<QString, QList<MediaFile>> groups;
        for (const auto &f : r.files) groups[f.identity] << f;
        for (auto i = groups.begin(); i != groups.end(); ++i) {
            sql("INSERT INTO books(identity,imported,added) VALUES(?,?,?) ON CONFLICT(identity) DO UPDATE SET imported=excluded.imported WHERE books.imported<>excluded.imported",
                {i.key(), json(r.books.value(i.key())), QDateTime::currentSecsSinceEpoch()});
            auto b = sql("SELECT id FROM books WHERE identity=?", {i.key()}); b.next();
            int ordinal = 0;
            for (const auto &f : i.value()) {
                // A canonical path has one owner. Regrouping cannot silently move personal state.
                auto existing = sql("SELECT book_id,id,ordinal,available FROM tracks WHERE path=?", {f.path});
                if (existing.next()) {
                    if (existing.value(0) != b.value(0)) throw std::runtime_error("Layout changed a book's grouping; restore the layout or use Relink.");
                    if (!f.probeChanged) {
                        if (existing.value(2).toInt() != ordinal || !existing.value(3).toBool())
                            sql("UPDATE tracks SET ordinal=?,available=1 WHERE id=?", {ordinal, existing.value(1)});
                        ++ordinal; continue;
                    }
                }
                sql("INSERT INTO tracks(book_id,path,ordinal,duration,size,mtime,probe,available) VALUES(?,?,?,?,?,?,?,1) ON CONFLICT(path) DO UPDATE SET ordinal=excluded.ordinal,duration=excluded.duration,size=excluded.size,mtime=excluded.mtime,probe=excluded.probe,available=1",
                    {b.value(0), f.path, ordinal++, f.duration, f.size, f.mtime, json(f.probe)});
            }
        }
        sql("COMMIT");
        auto after = sql("SELECT total_changes()"); after.next();
        if (after.value(0) != before.value(0)) reload();
        fail(r.errors.join('\n'));
    } catch (const std::exception &e) { db.rollback(); fail(QString("Catalog update failed; previous catalog retained: ") + e.what()); }
}
void Library::reload(int id) {
    const auto previous = all.value(id);
    if (!id) all.clear();
    auto q = sql("SELECT id,identity,imported,overrides,favorite,finished,added,listened,current_track,offset,speed FROM books"
                 + QString(id ? " WHERE id=?" : "") + " ORDER BY id", id ? QVariantList{id} : QVariantList{});
    // Progress needs durations and availability, never the (potentially huge) probe JSON.
    auto ts = sql("SELECT book_id,id,duration,available FROM tracks" + QString(id ? " WHERE book_id=?" : "")
                  + " ORDER BY book_id,ordinal,path", id ? QVariantList{id} : QVariantList{});
    bool track = ts.next();
    while (q.next()) {
        auto m = object(q.value(2)).toVariantMap();
        const auto overrides = object(q.value(3)).toVariantMap();
        for (auto i = overrides.begin(); i != overrides.end(); ++i) m[i.key()] = i.value();
        m["id"] = q.value(0); m["identity"] = q.value(1); m["favorite"] = q.value(4).toBool(); m["finished"] = q.value(5).toBool();
        m["added"] = q.value(6); m["listened"] = q.value(7); m["currentTrack"] = q.value(8); m["offset"] = q.value(9); m["speed"] = q.value(10);
        qint64 total = 0, before = 0; bool available = track && ts.value(0) == q.value(0), known = true;
        while (track && ts.value(0) == q.value(0)) {
            const auto d = ts.value(2).toLongLong(); total += d; known &= d > 0; available &= ts.value(3).toBool();
            if (ts.value(1) == m["currentTrack"]) before = total - d + qBound<qint64>(0, m["offset"].toLongLong(), d);
            track = ts.next();
        }
        m["available"] = available; m["duration"] = known ? total : 0;
        m["progress"] = m["finished"].toBool() ? 1.0 : (total > 0 ? double(before) / total : 0.0);
        m["remaining"] = known ? qint64((total - (m["finished"].toBool() ? total : before)) / m["speed"].toDouble()) : -1;
        m["coverUrl"] = QUrl::fromLocalFile(m["cover"].toString());
        all.insert(q.value(0).toInt(), m);
    }
    const auto current = all.value(id);
    if (id && previous == current) return;
    bool reordered = !id || previous.isEmpty() || matches(previous) != matches(current);
    const auto sortKey = selectedSort == 1 ? "author" : selectedSort == 2 ? "added" : selectedSort == 3 ? "listened" : "title";
    reordered |= previous[sortKey] != current[sortKey] || previous["title"] != current["title"];
    if (selectedSort == 4) reordered |= previous["series"] != current["series"] || previous["seriesOrder"] != current["seriesOrder"];
    if (reordered) query(search, selectedFilter, selectedSort);
    else {
        const auto row = visibleIndex(id);
        if (row >= 0) { rows[row] = current; emit dataChanged(index(row), index(row), {Qt::UserRole}); }
    }
    ++generation; emit changed(); emit bookChanged(id);
    if (!id) { emit chaptersChanged(); emit bookmarksChanged(0); }
}
QVariant Library::data(const QModelIndex &index, int role) const {
    return role == Qt::UserRole && index.row() >= 0 && index.row() < rows.size() ? QVariant(rows[index.row()]) : QVariant();
}
bool Library::matches(const QVariantMap &m) const {
    if (!search.isEmpty()) {
        QString searchable;
        for (const auto &field : {"title", "author", "narrator", "series"}) searchable += m[field].toString() + '\n';
        if (!searchable.contains(search, Qt::CaseInsensitive)) return false;
    }
    if (selectedFilter == 1 && (m["progress"].toDouble() <= 0 || m["finished"].toBool())) return false;
    if (selectedFilter == 2 && !m["finished"].toBool()) return false;
    if (selectedFilter == 3 && !m["favorite"].toBool()) return false;
    return true;
}
void Library::query(const QString &text, int filter, int sort) {
    const bool sortChanged = selectedSort != sort;
    search = text; selectedFilter = filter; selectedSort = sort;
    QList<QVariantMap> next;
    for (const auto &m : all) if (matches(m)) next << m;
    std::stable_sort(next.begin(), next.end(), [sort](const auto &a, const auto &b) {
        if (sort == 4) {
            const auto sa = a["series"].toString().trimmed().toCaseFolded(), sb = b["series"].toString().trimmed().toCaseFolded();
            if (sa.isEmpty() != sb.isEmpty()) return !sa.isEmpty();
            if (sa != sb) {
                const auto order = QString::localeAwareCompare(sa, sb);
                return order == 0 ? sa < sb : order < 0;
            }
            if (!sa.isEmpty()) {
                bool validA, validB;
                const double na = a["seriesOrder"].toString().toDouble(&validA), nb = b["seriesOrder"].toString().toDouble(&validB);
                validA = validA && std::isfinite(na) && na >= 0;
                validB = validB && std::isfinite(nb) && nb >= 0;
                if (validA != validB) return validA;
                if (validA && na != nb) return na < nb;
            }
        }
        if (sort == 2 || sort == 3) { const auto key = sort == 2 ? "added" : "listened"; if (a[key] != b[key]) return a[key].toLongLong() > b[key].toLongLong(); }
        const auto key = sort == 1 ? "author" : "title";
        const auto order = QString::localeAwareCompare(a[key].toString(), b[key].toString());
        return order == 0 ? a["id"].toInt() < b["id"].toInt() : order < 0;
    });
    bool layoutChanged = sortChanged || rows.size() != next.size();
    for (int i = 0; !layoutChanged && i < rows.size(); ++i)
        layoutChanged = rows[i]["id"] != next[i]["id"] || (sort == 4 && rows[i]["series"] != next[i]["series"]);
    if (layoutChanged) {
        beginResetModel(); rows = next; endResetModel();
    } else if (rows != next) {
        rows = next;
        emit dataChanged(index(0), index(rows.size() - 1), {Qt::UserRole});
    }
}
QVariantMap Library::book(int id) const { return all.value(id); }
QVariantList Library::bookRows(int columns, bool groupSeries) const {
    columns = qMax(1, columns);
    groupSeries = groupSeries && selectedSort == 4;
    QVariantList result, books;
    QString previousSeries, heading;
    int start = 0;
    auto append = [&] {
        if (books.isEmpty()) return;
        result << QVariantMap{{"heading", heading}, {"books", books}, {"startIndex", start}};
        books.clear(); heading.clear();
    };
    for (int i = 0; i < rows.size(); ++i) {
        const auto &book = rows[i];
        const auto series = book["series"].toString().trimmed();
        const auto key = groupSeries ? series.toCaseFolded() : QString();
        if (books.size() == columns || key != previousSeries) append();
        if (groupSeries && (i == 0 || key != previousSeries)) heading = series.isEmpty() ? "No series" : series;
        if (books.isEmpty()) start = i;
        books << QVariantMap{{"id", book["id"]}, {"visibleIndex", i}};
        previousSeries = key;
    }
    append(); return result;
}
QVariantList Library::tracks(int id) const {
    QVariantList result;
    auto q = sql("SELECT id,path,duration,available FROM tracks WHERE book_id=? ORDER BY ordinal,path", {id});
    while (q.next()) result << QVariantMap{{"id", q.value(0)}, {"path", q.value(1)}, {"duration", q.value(2)}, {"available", q.value(3).toBool()}};
    return result;
}
QVariantList Library::chapters(int id) const {
    QVariantList result;
    auto q = sql("SELECT id,path,duration,CASE WHEN json_valid(probe) THEN json_quote(json_extract(probe,'$.chapters')) END FROM tracks WHERE book_id=? ORDER BY ordinal,path", {id});
    while (q.next()) {
        const auto cs = QJsonDocument::fromJson(q.value(3).toString().toUtf8()).array();
        if (cs.isEmpty()) result << QVariantMap{{"track", q.value(0)}, {"start", 0}, {"end", q.value(2)}, {"title", QFileInfo(q.value(1).toString()).fileName()}};
        for (const auto &c : cs) {
            const auto chapter = c.toObject();
            result << QVariantMap{{"track", q.value(0)}, {"start", qRound64(chapter["start_time"].toString().toDouble() * 1000)},
                {"end", qRound64(chapter["end_time"].toString().toDouble() * 1000)}, {"title", chapter["tags"].toObject().value("title").toString("Chapter")}};
        }
    }
    return result;
}
bool Library::save(int id, int track, qint64 offset, double speed, bool listened, bool fromBeginning) {
    if (!id || !track) return true;
    try {
        sql("UPDATE books SET current_track=?,offset=?,speed=?,listened=CASE WHEN ? THEN ? ELSE listened END,finished=CASE WHEN ? THEN 0 ELSE finished END WHERE id=?",
            {track, qMax<qint64>(0, offset), speed, listened, QDateTime::currentSecsSinceEpoch(), fromBeginning, id});
        settings.setValue("currentBook", id); settings.sync();
        if (settings.status() != QSettings::NoError) throw std::runtime_error("Cannot save preferences");
        reload(id); return true;
    } catch (const std::exception &e) { fail(QString("Progress was NOT saved: ") + e.what()); return false; }
}
void Library::setFlag(int id, const QString &flag, bool value) {
    if (flag != "favorite" && flag != "finished") return;
    try {
        sql("UPDATE books SET " + flag + "=? WHERE id=?", {value, id});
        reload(id);
    } catch (const std::exception &e) { fail(e.what()); }
}
void Library::openFolder(int id) {
    const auto identity = book(id).value("identity").toString();
    if (identity.isEmpty()) return;
    const auto path = identity.endsWith(".m4b", Qt::CaseInsensitive) ? QFileInfo(identity).absolutePath() : identity;
    if (!QFileInfo(path).isDir()) { fail("Book folder unavailable. Reconnect its root or Relink the book."); return; }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) fail("Could not open the book folder.");
}
void Library::edit(int id, const QVariantMap &values) {
    try {
        auto q = sql("SELECT overrides FROM books WHERE id=?", {id}); if (!q.next()) return;
        auto o = object(q.value(0));
        for (const auto &key : {"title", "author", "narrator", "series", "seriesOrder", "description"})
            if (values.contains(key)) o[key] = values[key].toString().left(65536);
        sql("UPDATE books SET overrides=? WHERE id=?", {json(o), id}); reload(id);
    } catch (const std::exception &e) { fail(e.what()); }
}
void Library::setCover(int id, const QUrl &url) {
    if (!url.isLocalFile()) { fail("Choose a local image."); return; }
    try {
        QImageReader reader(url.toLocalFile());
        const auto size = reader.size();
        if (!size.isValid() || qint64(size.width()) * size.height() > 40000000) throw std::runtime_error("Image is invalid or exceeds 40 megapixels");
        reader.setScaledSize(size.scaled(1200, 1200, Qt::KeepAspectRatio));
        const auto image = reader.read();
        if (image.isNull()) throw std::runtime_error("Cannot decode cover image");
        QDir().mkpath(dataDir + "/artwork");
        const auto path = dataDir + "/artwork/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png";
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) throw std::runtime_error("Cannot save artwork copy");
        auto q = sql("SELECT overrides FROM books WHERE id=?", {id}); if (!q.next()) return;
        auto o = object(q.value(0)); o["cover"] = path;
        sql("UPDATE books SET overrides=? WHERE id=?", {json(o), id}); reload(id);
    } catch (const std::exception &e) { fail(e.what()); }
}
QVariantList Library::bookmarks(int id) const {
    QVariantList result;
    auto q = sql("SELECT b.id,b.track_id,b.offset,b.label,t.path FROM bookmarks b JOIN tracks t ON t.id=b.track_id WHERE b.book_id=? ORDER BY b.id", {id});
    while (q.next()) result << QVariantMap{{"id", q.value(0)}, {"track", q.value(1)}, {"offset", q.value(2)}, {"label", q.value(3)}, {"file", QFileInfo(q.value(4).toString()).fileName()}};
    return result;
}
void Library::addBookmark(int id, int track, qint64 offset, const QString &label) {
    try {
        sql("INSERT INTO bookmarks(book_id,track_id,offset,label) SELECT ?,?,?,? WHERE EXISTS(SELECT 1 FROM tracks WHERE id=? AND book_id=?)", {id, track, offset, label.trimmed().isEmpty() ? "Bookmark" : label.left(500), track, id});
        emit bookmarksChanged(id);
    } catch (const std::exception &e) { fail(e.what()); }
}
void Library::removeBookmark(int id) {
    try {
        auto q = sql("SELECT book_id FROM bookmarks WHERE id=?", {id}); if (!q.next()) return;
        sql("DELETE FROM bookmarks WHERE id=?", {id}); emit bookmarksChanged(q.value(0).toInt());
    } catch (const std::exception &e) { fail(e.what()); }
}
void Library::relink(int id, const QUrl &location) {
    if (scanning()) { fail("Finish or cancel the scan before relinking."); return; }
    const auto b = book(id); const auto ts = tracks(id);
    if (!location.isLocalFile() || b.isEmpty() || ts.isEmpty()) return;
    const auto target = QFileInfo(location.toLocalFile()).canonicalFilePath();
    const auto old = b["identity"].toString();
    const bool single = old.endsWith(".m4b", Qt::CaseInsensitive);
    if (target.isEmpty() || (single ? !QFileInfo(target).isFile() : !QFileInfo(target).isDir())) { fail("Select the relocated M4B file or MP3 book folder."); return; }
    try {
        sql("BEGIN IMMEDIATE");
        QMap<int, QString> relocated;
        QSet<int> duplicates;
        for (const auto &v : ts) {
            auto t = v.toMap();
            const auto path = single ? target : QDir(target).filePath(QDir(old).relativeFilePath(t["path"].toString()));
            const QFileInfo f(path);
            // Explicit relocation only: relative layout and sizes must match. No title-based edition matching.
            auto size = sql("SELECT size FROM tracks WHERE id=?", {t["id"]}); size.next();
            if (!f.isFile() || f.isSymLink() || f.size() != size.value(0).toLongLong() || (!single && !within(f.canonicalFilePath(), target)))
                throw std::runtime_error("Relink requires the same files and relative layout (matching sizes); original identity and state retained");
            relocated[t["id"].toInt()] = f.canonicalFilePath();
            auto conflict = sql("SELECT book_id FROM tracks WHERE path=? AND book_id<>?", {f.canonicalFilePath(), id});
            if (conflict.next()) duplicates.insert(conflict.value(0).toInt());
        }
        for (int duplicate : duplicates) {
            auto state = sql("SELECT 1 FROM books WHERE id=? AND favorite=0 AND finished=0 AND listened=0 AND current_track IS NULL AND offset=0 AND speed=1 AND overrides='{}' AND NOT EXISTS(SELECT 1 FROM bookmarks WHERE book_id=?)", {duplicate, duplicate});
            if (!state.next()) throw std::runtime_error("Relink target already has personal state. Both entries were retained; choose an unindexed copy or restore the original path.");
            for (const auto &v : tracks(duplicate)) if (!relocated.values().contains(v.toMap()["path"].toString()))
                throw std::runtime_error("Relink target belongs to a different grouping. Put this book in its own folder.");
            // Consolidate only a freshly imported destination; never discard another book's personal state.
            sql("DELETE FROM tracks WHERE book_id=?", {duplicate});
            sql("DELETE FROM books WHERE id=?", {duplicate});
        }
        for (auto i = relocated.begin(); i != relocated.end(); ++i)
            sql("UPDATE tracks SET path=?,mtime=0,available=1 WHERE id=?", {i.value(), i.key()});
        sql("UPDATE books SET identity=? WHERE id=?", {target, id});
        sql("INSERT OR IGNORE INTO roots VALUES(?)", {single ? QFileInfo(target).absolutePath() : target});
        sql("COMMIT"); reload(); emit rootsChanged(); refresh();
    } catch (const std::exception &e) { db.rollback(); fail(e.what()); }
}

Player::Player(Library &library, QObject *parent) : QObject(parent), lib(library) {
    if (qEnvironmentVariableIsSet("OMA_HEADLESS")) media.setAudioBufferOutput(new QAudioBufferOutput(&media));
    else { output = std::make_unique<QAudioOutput>(); media.setAudioOutput(output.get()); output->setVolume(lib.settings.value("volume", 0.8).toDouble()); }
    connect(&media, &QMediaPlayer::mediaStatusChanged, this, [this](auto s) {
        if (s == QMediaPlayer::LoadedMedia || s == QMediaPlayer::BufferedMedia) loaded();
        if (s == QMediaPlayer::EndOfMedia && !switching)
            QTimer::singleShot(0, this, [this] { if (media.mediaStatus() == QMediaPlayer::EndOfMedia) ended(); });
    });
    connect(&media, &QMediaPlayer::seekableChanged, this, [this] { loaded(); });
    connect(&media, &QMediaPlayer::positionChanged, this, [this](qint64 p) {
        emit tick();
        if (sleepBoundary >= 0 && p >= sleepBoundary) { sleepBoundary = -1; stoppedAtBoundary = true; pause(); emit changed(); }
    });
    connect(&media, &QMediaPlayer::durationChanged, this, [this] { loaded(); emit tick(); });
    connect(&media, &QMediaPlayer::playbackStateChanged, this, [this] { if (playing()) listenedSinceSave = true; emit changed(); });
    connect(&media, &QMediaPlayer::errorOccurred, this, [this](auto, const QString &text) { autoPlay = false; lib.fail("Playback: " + text); emit changed(); });
    connect(&saveTimer, &QTimer::timeout, this, [this] { if (playing()) persist(); });
    saveTimer.start(5000);
    sleepTimer.setSingleShot(true);
    connect(&sleepTimer, &QTimer::timeout, this, [this] { stoppedAtBoundary = true; pause(); emit changed(); });
    connect(&lib, &Library::bookChanged, this, [this](int id) { if (!id || id == currentBook) emit changed(); });
    const auto id = lib.settings.value("currentBook").toInt();
    if (id) open(id, false);
}
Player::~Player() { persist(); }
bool Player::persist() {
    const bool saved = lib.save(currentBook, currentTrack, position(), speed(), playing() || listenedSinceSave);
    if (saved) listenedSinceSave = false;
    return saved;
}
void Player::open(int id, bool autoplay) {
    if (id == currentBook) { if (autoplay) play(); return; }
    auto b = lib.book(id); auto ts = lib.tracks(id);
    if (b.isEmpty() || ts.isEmpty()) return;
    if (!persist()) return;
    int track = b["currentTrack"].toInt();
    if (!track) track = ts.first().toMap()["id"].toInt();
    if (!lib.save(id, track, b["offset"].toLongLong(), b["speed"].toDouble(), false)) return;
    currentBook = id;
    media.setPlaybackRate(b["speed"].toDouble());
    load(track, b["offset"].toLongLong(), autoplay);
}
void Player::startOver(int id) {
    const auto b = lib.book(id); const auto ts = lib.tracks(id);
    if (!b.value("available").toBool() || ts.isEmpty()) return;
    if (id != currentBook && !persist()) return;
    const int track = ts.first().toMap()["id"].toInt();
    if (!lib.save(id, track, 0, b["speed"].toDouble(), false, true)) return;
    currentBook = id;
    media.setPlaybackRate(b["speed"].toDouble());
    load(track, 0, true);
}
void Player::load(int track, qint64 offset, bool autoplay) {
    auto ts = lib.tracks(currentBook);
    for (const auto &v : ts) {
        auto t = v.toMap(); if (t["id"].toInt() != track) continue;
        sleepBoundary = -1; stoppedAtBoundary = false;
        if (pending < 0 && t["available"].toBool() && media.source() == QUrl::fromLocalFile(t["path"].toString())
            && media.isSeekable() && media.duration() > 0 && media.error() == QMediaPlayer::NoError && QFileInfo::exists(t["path"].toString())) {
            currentTrack = track; pending = -1; autoPlay = false;
            media.setPosition(qBound<qint64>(0, offset, media.duration()));
            if (autoplay) media.play(); else media.pause();
            emit seeked(position()); emit changed(); emit tick(); return;
        }
        switching = true; autoPlay = autoplay; currentTrack = track; pending = qMax<qint64>(0, offset);
        media.stop(); media.setSource({});
        if (!t["available"].toBool() || !QFileInfo::exists(t["path"].toString())) {
            autoPlay = false; lib.fail("File unavailable. Reconnect its root, Refresh, or Relink from book details.");
        } else media.setSource(QUrl::fromLocalFile(t["path"].toString()));
        switching = false; emit changed(); emit tick(); return;
    }
}
void Player::loaded() {
    if (switching || pending < 0 || !media.isSeekable() || media.duration() <= 0 ||
        (media.mediaStatus() != QMediaPlayer::LoadedMedia && media.mediaStatus() != QMediaPlayer::BufferedMedia)) return;
    const auto target = qBound<qint64>(0, pending, media.duration());
    pending = -1; media.setPosition(target);
    emit seeked(target);
    if (autoPlay) { autoPlay = false; media.play(); }
    emit tick();
}
void Player::play() {
    if (!currentBook) return;
    stoppedAtBoundary = false;
    for (const auto &v : lib.tracks(currentBook)) {
        const auto t = v.toMap();
        if (t["id"].toInt() == currentTrack && media.source().toLocalFile() != t["path"].toString()) {
            load(currentTrack, position(), true); return;
        }
    }
    if (media.source().isEmpty() || media.mediaStatus() == QMediaPlayer::InvalidMedia) { load(currentTrack, position(), true); return; }
    if (pending >= 0) { autoPlay = true; loaded(); return; }
    if (media.mediaStatus() == QMediaPlayer::EndOfMedia) seek(0);
    media.play();
}
void Player::pause() { autoPlay = false; media.pause(); persist(); }
void Player::toggle() { if (playing()) pause(); else play(); }
void Player::seek(qint64 offset) {
    if (pending >= 0) pending = qMax<qint64>(0, offset);
    else if (media.isSeekable()) media.setPosition(qBound<qint64>(0, offset, media.duration()));
    emit seeked(position()); emit tick(); persist();
}
void Player::skip(int seconds) {
    auto ts = lib.tracks(currentBook); if (ts.isEmpty()) return;
    int i = 0; while (i < ts.size() && ts[i].toMap()["id"].toInt() != currentTrack) ++i;
    if (i == ts.size()) return;
    qint64 target = position() + qint64(seconds) * 1000;
    while (target < 0 && i > 0) target += ts[--i].toMap()["duration"].toLongLong();
    while (i + 1 < ts.size() && ts[i].toMap()["duration"].toLongLong() > 0 && target >= ts[i].toMap()["duration"].toLongLong()) target -= ts[i++].toMap()["duration"].toLongLong();
    if (ts[i].toMap()["id"].toInt() == currentTrack) seek(target);
    else jump(ts[i].toMap()["id"].toInt(), target, playing());
}
void Player::jump(int track, qint64 offset, bool autoplay) { if (!persist()) return; load(track, offset, autoplay); persist(); }
void Player::next() {
    const auto cs = lib.chapters(currentBook);
    bool found = false;
    for (const auto &v : cs) {
        auto c = v.toMap();
        if (c["track"].toInt() == currentTrack) {
            found = true;
            if (c["start"].toLongLong() <= position() + 100) continue;
        } else if (!found) continue;
        jump(c["track"].toInt(), c["start"].toLongLong(), playing()); return;
    }
}
void Player::previous() {
    const auto cs = lib.chapters(currentBook); QVariantMap previous;
    bool inCurrent = false;
    for (const auto &v : cs) {
        auto c = v.toMap();
        if (inCurrent && c["track"].toInt() != currentTrack) break;
        if (c["track"].toInt() == currentTrack) inCurrent = true;
        if (c["track"].toInt() == currentTrack && c["end"].toLongLong() > position()) {
            if (position() - c["start"].toLongLong() > 3000 || previous.isEmpty()) previous = c;
            jump(previous["track"].toInt(), previous["start"].toLongLong(), playing()); return;
        }
        previous = c;
    }
    if (inCurrent && !previous.isEmpty()) jump(previous["track"].toInt(), previous["start"].toLongLong(), playing());
}
void Player::ended() {
    if (stoppedAtBoundary) return;
    if (sleepBoundary >= 0) { sleepBoundary = -1; stoppedAtBoundary = true; pause(); emit changed(); return; }
    auto ts = lib.tracks(currentBook);
    for (int i = 0; i < ts.size(); ++i) if (ts[i].toMap()["id"].toInt() == currentTrack) {
        if (i + 1 < ts.size()) jump(ts[i + 1].toMap()["id"].toInt(), 0, true);
        else { persist(); lib.setFlag(currentBook, "finished", true); }
        return;
    }
}
void Player::setSpeed(double value) { if (!std::isfinite(value)) return; media.setPlaybackRate(qBound(0.5, value, 3.0)); persist(); emit changed(); }
void Player::setVolume(double value) { if (!std::isfinite(value)) return; if (output) output->setVolume(qBound(0.0, value, 1.0)); lib.settings.setValue("volume", qBound(0.0, value, 1.0)); emit changed(); }
void Player::sleep(int minutes) {
    sleepTimer.stop(); sleepBoundary = -1;
    if (minutes > 0) sleepTimer.start(qMin(minutes, 120) * 60000);
    if (minutes == -1) for (const auto &v : lib.chapters(currentBook)) { auto c = v.toMap(); if (c["track"].toInt() == currentTrack && c["end"].toLongLong() > position()) { sleepBoundary = c["end"].toLongLong(); break; } }
    emit changed();
}
QString Player::sleepLabel() const { return sleepBoundary >= 0 ? "End of chapter" : sleepTimer.isActive() ? QString("%1 min").arg((sleepTimer.remainingTime() + 59999) / 60000) : "Off"; }
void Player::bookmark(const QString &label) { if (currentBook && currentTrack) lib.addBookmark(currentBook, currentTrack, position(), label); }
