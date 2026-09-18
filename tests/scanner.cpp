#include "scanner.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>

static void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("-show_format")) {
        const auto output = QByteArray("{\"streams\":[{\"codec_type\":\"audio\"}],\"padding\":\"") + QByteArray(17 * 1024 * 1024, 'x') + "\"}";
        return fwrite(output.constData(), 1, output.size(), stdout) == size_t(output.size()) ? 0 : 1;
    }
    try {
        QTemporaryDir temporary("/tmp/oma-audio-books-synthetic-scanner-XXXXXX");
        require(temporary.isValid(), "temporary scanner state");
        const auto base = temporary.path(), root = base + "/Book", cache = base + "/covers";
        require(QDir().mkpath(root), "temporary book folder");
        const auto picture = base + "/embedded.jpg", audio = base + "/source.mp3";
        require(QProcess::execute("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "color=c=blue:s=32x32", "-frames:v", "1", picture}) == 0, "synthetic cover");
        require(QProcess::execute("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "sine=frequency=440:duration=0.1", "-i", picture,
            "-map", "0:a", "-map", "1:v", "-c:v", "copy", "-disposition:v", "attached_pic", audio}) == 0, "synthetic audio");
        QMap<QString, MediaFile> cached;
        const QJsonObject probe{{"streams", QJsonArray{QJsonObject{{"codec_type", "audio"}},
            QJsonObject{{"disposition", QJsonObject{{"attached_pic", 1}}}}}}};
        for (const auto name : {"10.mp3", "2.mp3"}) {
            const auto path = root + "/" + name;
            require(QFile::copy(audio, path), "synthetic track copy");
            const QFileInfo fi(path);
            cached[path] = {path, root, fi.size(), fi.lastModified().toMSecsSinceEpoch(), 100, probe};
        }
        auto cancel = std::make_shared<std::atomic_bool>(false);
        auto scan = [&] {
            auto result = scanMedia({root}, cached, cache, cancel);
            require(result.errors.isEmpty() && result.files.size() == 2 && !result.canceled, "book scan");
            require(result.books.size() == 1 && result.books.value(root) == importedBook(result.files), "worker must supply final book metadata and selected artwork");
            for (const auto &f : result.files) cached[f.path] = f;
            return result;
        };
        auto result = scan();
        require(QFileInfo(result.files.first().path).fileName() == "2.mp3", "first naturally ordered artwork source");
        const auto selectedCover = importedBook(result.files)["cover"].toString();
        require(QFileInfo::exists(selectedCover) && QDir(cache).entryList({"*.jpg"}, QDir::Files).size() == 1, "extract one cover per book");
        require(!result.files.last().probe.contains("oma_cover"), "unused track artwork was extracted");
        scan();
        require(QDir(cache).entryList({"*.jpg"}, QDir::Files).size() == 1, "rescan reused selected artwork");
        require(QDir(cache).removeRecursively(), "clear generated artwork");
        cached[result.files.first().path].probe.remove("oma_cover");
        result = scan();
        require(importedBook(result.files)["cover"].toString() == selectedCover && QFileInfo::exists(selectedCover), "regenerate missing artwork without extraction history");
        require(QFile::copy(picture, root + "/cover.jpg") && QDir(cache).removeRecursively(), "folder artwork fixture");
        result = scan();
        require(importedBook(result.files)["cover"] == root + "/cover.jpg" && QDir(cache).entryList({"*.jpg"}, QDir::Files).isEmpty(), "folder cover avoids embedded extraction");
        require(QFile::remove(root + "/cover.jpg"), "remove folder artwork");
        result = scan();
        require(QFileInfo::exists(importedBook(result.files)["cover"].toString()), "embedded fallback after folder artwork disappears");
        *cancel = true;
        require(scanMedia({root}, cached, cache, cancel).canceled, "cancellation retained");
        *cancel = false;

        const auto nested = base + "/nested";
        cached.clear();
        for (const auto relative : {"Parent/1.mp3", "Parent/Child.m4b/1.mp3", "ParentExtra/1.mp3", "Mixed/1.mp3", "Mixed/only.m4b"}) {
            const auto path = nested + "/" + relative;
            require(QDir().mkpath(QFileInfo(path).absolutePath()), "nested book folder");
            QFile file(path); require(file.open(QIODevice::WriteOnly), "nested track"); file.close();
            const QFileInfo fi(path);
            cached[path] = {path, {}, 0, fi.lastModified().toMSecsSinceEpoch(), 100, {}};
        }
        result = scanMedia({nested}, cached, cache, cancel);
        require(result.files.size() == 4 && result.errors.size() == 1 && result.errors.first().startsWith(nested + "/Parent:"), "reject only ambiguous MP3 ancestors, retain siblings and nested M4B");
        const auto binaries = base + "/bin", oversized = base + "/oversized";
        require(QDir().mkpath(binaries) && QDir().mkpath(oversized), "oversized probe fixture");
        require(QFile::link(QCoreApplication::applicationFilePath(), binaries + "/ffprobe"), "synthetic probe executable");
        QFile file(oversized + "/huge.m4b"); require(file.open(QIODevice::WriteOnly), "oversized metadata file"); file.close();
        const auto path = qgetenv("PATH"); qputenv("PATH", binaries.toUtf8() + ':' + path);
        result = scanMedia({oversized}, {}, cache, cancel);
        qputenv("PATH", path);
        require(result.files.isEmpty() && result.errors.size() == 1 && result.errors.first().contains("excessive metadata"), "reject excessive probe output even when process exits quickly");
        puts("PASS: scanner artwork selection/reuse/regeneration/folder precedence, cancellation, nested grouping and metadata limit");
        return 0;
    } catch (const std::exception &error) {
        fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
