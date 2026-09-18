#include "scanner.h"
#include <QCollator>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <stdexcept>

bool within(const QString &path, const QString &root) {
    return path == root || path.startsWith(root + (root.endsWith('/') ? "" : "/"));
}
static QByteArray process(const QString &program, const QStringList &args,
                          const std::shared_ptr<std::atomic_bool> &cancel) {
    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(3000)) throw std::runtime_error((program + ": " + p.errorString()).toStdString());
    QElapsedTimer timer;
    timer.start();
    QByteArray out, err;
    while (!p.waitForFinished(50)) {
        out += p.readAllStandardOutput();
        err += p.readAllStandardError();
        if (*cancel || timer.elapsed() > 15000 || out.size() + err.size() > 16 * 1024 * 1024) {
            p.kill(); p.waitForFinished(3000);
            throw std::runtime_error(*cancel ? "Canceled" : "probe timed out or returned excessive metadata");
        }
    }
    out += p.readAllStandardOutput(); err += p.readAllStandardError();
    if (out.size() + err.size() > 16 * 1024 * 1024)
        throw std::runtime_error("probe returned excessive metadata");
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        throw std::runtime_error((program + ": " + QString::fromUtf8(err).left(600)).toStdString());
    return out;
}
static QJsonObject tags(const MediaFile &f) {
    QJsonObject result;
    auto add = [&](QJsonObject t) { for (auto i = t.begin(); i != t.end(); ++i) result[i.key().toLower()] = i.value(); };
    for (auto s : f.probe["streams"].toArray())
        if (s.toObject()["codec_type"] == "audio") add(s.toObject()["tags"].toObject());
    add(f.probe["format"].toObject()["tags"].toObject());
    return result;
}
QJsonObject importedBook(const QList<MediaFile> &files) {
    const auto &f = files.first();
    auto t = tags(f);
    const bool m4b = f.path.endsWith(".m4b", Qt::CaseInsensitive);
    auto pick = [&](QStringList names, QString fallback = {}) {
        for (const auto &name : names) if (!t[name].toString().trimmed().isEmpty()) return t[name].toString();
        return fallback;
    };
    QString cover;
    const auto dir = QFileInfo(f.identity).isDir() ? f.identity : QFileInfo(f.path).absolutePath();
    for (const auto &name : {"cover.jpg", "cover.png", "folder.jpg", "folder.png", "Cover.jpg"}) {
        const QFileInfo art(dir + "/" + name);
        if (art.isFile() && !art.isSymLink()) { cover = art.absoluteFilePath(); break; }
    }
    if (cover.isEmpty()) cover = f.probe["oma_cover"].toString();
    return {{"title", pick(m4b ? QStringList{"title", "album"} : QStringList{"album"},
                           m4b ? QFileInfo(f.path).completeBaseName() : QFileInfo(f.identity).fileName())},
            {"author", pick({"artist", "album_artist", "author"})},
            {"narrator", pick({"narrator", "composer"})}, {"series", pick({"series"})},
            {"seriesOrder", pick({"series-part", "series_order", "part"})},
            {"description", pick({"description", "comment", "synopsis"})}, {"cover", cover}};
}
ScanResult scanMedia(const QStringList &roots, const QMap<QString, MediaFile> &cached,
                     const QString &cacheDir, const std::shared_ptr<std::atomic_bool> &cancel) {
    ScanResult result;
    QDir().mkpath(cacheDir);
    QSet<QString> seen;
    for (const auto &root : roots) {
        if (*cancel) break;
        if (!QFileInfo::exists(root)) { result.reconciled << root; result.errors << root + ": root unavailable; catalog retained"; continue; }
        QList<MediaFile> found;
        bool readable = true;
        std::function<void(QString)> visit = [&](const QString &dir) {
            if (*cancel) return;
            if (!QFileInfo(dir).isReadable()) { readable = false; return; }
            const auto entries = QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
            for (const auto &fi : entries) {
                if (*cancel) return;
                if (fi.isSymLink()) continue;
                if (fi.isDir()) { visit(fi.absoluteFilePath()); continue; }
                const auto suffix = fi.suffix().toLower();
                if (suffix != "mp3" && suffix != "m4b") continue;
                const auto path = fi.canonicalFilePath();
                if (path.isEmpty() || !within(path, root) || seen.contains(path)) continue;
                MediaFile f{path, path, fi.size(), fi.lastModified().toMSecsSinceEpoch(), 0, {}};
                if (suffix == "mp3") {
                    f.identity = fi.absolutePath();
                    if (QRegularExpression("^(disc|disk|cd)[ _-]*[0-9]+$", QRegularExpression::CaseInsensitiveOption)
                        .match(QFileInfo(f.identity).fileName()).hasMatch()) f.identity = QFileInfo(f.identity).absolutePath();
                }
                try {
                    if (cached.contains(path) && cached[path].size == f.size && cached[path].mtime == f.mtime) {
                        f.probe = cached[path].probe;
                        f.duration = cached[path].duration;
                        f.probeChanged = false;
                    } else {
                        auto bytes = process("ffprobe", {"-v", "error", "-protocol_whitelist", "file,crypto,data", "-show_format", "-show_streams", "-show_chapters", "-of", "json", path}, cancel);
                        QJsonParseError error;
                        f.probe = QJsonDocument::fromJson(bytes, &error).object();
                        if (error.error != QJsonParseError::NoError) throw std::runtime_error("invalid ffprobe JSON");
                        bool audio = false;
                        for (auto s : f.probe["streams"].toArray()) {
                            auto stream = s.toObject();
                            audio |= stream["codec_type"] == "audio";
                        }
                        if (!audio) throw std::runtime_error("no readable audio stream; supply a completed DRM-free MP3/M4B");
                        f.duration = qRound64(f.probe["format"].toObject()["duration"].toString().toDouble() * 1000);
                    }
                    found << f;
                } catch (const std::exception &e) { result.errors << path + ": " + e.what(); }
            }
        };
        visit(root);
        if (!readable) result.errors << root + ": unreadable directory; scan not reconciled";
        else {
            result.reconciled << root; result.files << found;
            for (const auto &f : found) seen.insert(f.path);
        }
    }
    result.canceled = *cancel;
    if (result.canceled) return result;
    QMap<QString, QList<MediaFile>> groups;
    for (const auto &f : result.files) groups[f.identity] << f;
    result.files.clear();
    QSet<QString> nested;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        if (it.value().first().path.endsWith(".m4b", Qt::CaseInsensitive)) continue;
        QString parent = QFileInfo(it.key()).absolutePath();
        while (true) {
            if (groups.contains(parent)) nested.insert(parent);
            const auto next = QFileInfo(parent).absolutePath();
            if (next == parent) break;
            parent = next;
        }
    }
    QCollator natural(QLocale("en_US"));
    natural.setNumericMode(true);
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        if (*cancel) break;
        auto &files = it.value();
        QSet<QString> albums, positions;
        bool tagged = true, anyDisc = false, missingDisc = false;
        for (const auto &f : files) {
            auto t = tags(f);
            if (!t["album"].toString().isEmpty()) albums.insert(t["album"].toString());
            const auto track = t["track"].toString().section('/', 0, 0).toInt();
            const auto disc = t["disc"].toString().section('/', 0, 0).toInt();
            anyDisc |= disc > 0; missingDisc |= disc <= 0;
            const auto pos = QString::number(disc) + ":" + QString::number(track);
            if (track <= 0 || positions.contains(pos)) tagged = false;
            positions.insert(pos);
        }
        if (anyDisc && missingDisc) tagged = false;
        if (albums.size() > 1 || nested.contains(it.key())) {
            result.errors << it.key() + ": ambiguous MP3 grouping; put each book in its own folder, with only Disc N subfolders";
            continue;
        }
        std::sort(files.begin(), files.end(), [&](const MediaFile &a, const MediaFile &b) {
            if (tagged) {
                auto position = [](const MediaFile &f) { auto t = tags(f); return std::pair(t["disc"].toString().section('/', 0, 0).toInt(), t["track"].toString().section('/', 0, 0).toInt()); };
                if (position(a) != position(b)) return position(a) < position(b);
            }
            const auto order = natural.compare(a.path, b.path);
            return order == 0 ? a.path < b.path : order < 0;
        });
        // Only the first ordered file supplies book artwork; folder art takes precedence.
        auto &f = files.first();
        for (const auto &s : f.probe["streams"].toArray()) {
            if (s.toObject()["disposition"].toObject()["attached_pic"].toInt() == 1) {
                if (QFileInfo::exists(importedBook(files)["cover"].toString())) break;
                const auto key = QCryptographicHash::hash((f.path + QString::number(f.mtime) + ":" + QString::number(f.size)).toUtf8(), QCryptographicHash::Sha256).toHex();
                const auto cover = cacheDir + "/" + key + ".jpg";
                if (!QFileInfo::exists(cover)) {
                    try { process("ffmpeg", {"-v", "error", "-nostdin", "-protocol_whitelist", "file,crypto,data", "-i", f.path, "-map", "0:v:0", "-frames:v", "1", "-vf", "scale=600:600:force_original_aspect_ratio=decrease", "-y", cover}, cancel); }
                    catch (const std::exception &e) { result.errors << f.path + ": artwork: " + e.what(); QFile::remove(cover); }
                }
                if (QFileInfo::exists(cover) && f.probe["oma_cover"].toString() != cover) { f.probe["oma_cover"] = cover; f.probeChanged = true; }
                break;
            }
        }
        result.books.insert(it.key(), importedBook(files));
        result.files << files;
    }
    result.canceled = *cancel;
    return result;
}
