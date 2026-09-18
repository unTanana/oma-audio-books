#pragma once
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <atomic>
#include <memory>

struct MediaFile {
    QString path, identity;
    qint64 size = 0, mtime = 0, duration = 0;
    QJsonObject probe;
    bool probeChanged = true;
};
struct ScanResult {
    QList<MediaFile> files;
    QMap<QString, QJsonObject> books;
    QStringList reconciled, errors;
    bool canceled = false;
};
ScanResult scanMedia(const QStringList &roots, const QMap<QString, MediaFile> &cached,
                     const QString &cacheDir, const std::shared_ptr<std::atomic_bool> &cancel);
bool within(const QString &path, const QString &root);
QJsonObject importedBook(const QList<MediaFile> &files);
