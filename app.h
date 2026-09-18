#pragma once
#include "scanner.h"
#include <QAbstractListModel>
#include <QAudioOutput>
#include <QFutureWatcher>
#include <QMediaPlayer>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTimer>

class Library : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QStringList roots READ roots NOTIFY rootsChanged)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    explicit Library(QObject *parent = nullptr);
    ~Library() override;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : rows.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "book"}}; }
    bool scanning() const { return scanActive; }
    QString error() const { return message; }
    int revision() const { return generation; }
    QStringList roots() const;
    Q_INVOKABLE void addRoot(const QUrl &url);
    Q_INVOKABLE void removeRoot(const QString &path);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void cancelScan() { *cancel = true; }
    Q_INVOKABLE void query(const QString &text, int filter, int sort);
    Q_INVOKABLE QVariantMap book(int id) const;
    Q_INVOKABLE QVariantList bookRows(int columns, bool groupSeries) const;
    Q_INVOKABLE int visibleIndex(int id) const { for (int i = 0; i < rows.size(); ++i) if (rows[i]["id"].toInt() == id) return i; return -1; }
    Q_INVOKABLE QVariantList tracks(int id) const;
    Q_INVOKABLE QVariantList chapters(int id) const;
    Q_INVOKABLE QVariantList bookmarks(int id) const;
    Q_INVOKABLE void setFlag(int id, const QString &flag, bool value);
    Q_INVOKABLE void edit(int id, const QVariantMap &values);
    Q_INVOKABLE void setCover(int id, const QUrl &url);
    Q_INVOKABLE void addBookmark(int id, int track, qint64 offset, const QString &label);
    Q_INVOKABLE void removeBookmark(int id);
    Q_INVOKABLE void relink(int id, const QUrl &location);
    Q_INVOKABLE void openFolder(int id);
    Q_INVOKABLE QVariant preference(const QString &key, const QVariant &fallback) { return settings.value(key, fallback); }
    Q_INVOKABLE void setPreference(const QString &key, const QVariant &value) { settings.setValue(key, value); settings.sync(); if (settings.status() != QSettings::NoError) fail("Preferences were not saved."); }
    bool save(int id, int track, qint64 offset, double speed, bool listened, bool fromBeginning = false);
    void fail(const QString &text);
    QSqlQuery sql(const QString &text, const QVariantList &args = {}) const;
    void reload(int id = 0);
    QSettings settings;
    QString dataDir, cacheDir;
signals:
    void changed();
    void bookChanged(int id); // Zero means the catalog was refreshed.
    void chaptersChanged();
    void bookmarksChanged(int id);
    void scanChanged();
    void errorChanged();
    void rootsChanged();
private:
    void apply(const ScanResult &result);
    QSqlDatabase db;
    QHash<int, QVariantMap> all;
    QList<QVariantMap> rows;
    bool matches(const QVariantMap &book) const;
    QFutureWatcher<ScanResult> worker;
    std::shared_ptr<std::atomic_bool> cancel = std::make_shared<std::atomic_bool>(false);
    QString message, search;
    int generation = 0, selectedFilter = 0, selectedSort = 0;
    bool scanActive = false;
};

class Player : public QObject {
    Q_OBJECT
    Q_PROPERTY(int bookId READ bookId NOTIFY changed)
    Q_PROPERTY(int trackId READ trackId NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(qint64 position READ position NOTIFY tick)
    Q_PROPERTY(qint64 duration READ duration NOTIFY tick)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY changed)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY changed)
    Q_PROPERTY(QString sleepLabel READ sleepLabel NOTIFY changed)
public:
    explicit Player(Library &library, QObject *parent = nullptr);
    ~Player() override;
    int bookId() const { return currentBook; }
    int trackId() const { return currentTrack; }
    QString title() const { return lib.book(currentBook).value("title").toString(); }
    bool playing() const { return media.playbackState() == QMediaPlayer::PlayingState; }
    qint64 position() const { return pending >= 0 ? pending : media.position(); }
    qint64 duration() const { return media.duration(); }
    double speed() const { return media.playbackRate(); }
    double volume() const { return output ? output->volume() : 1; }
    QString sleepLabel() const;
    Q_INVOKABLE void open(int book, bool play = true);
    Q_INVOKABLE void startOver(int book);
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void seek(qint64 offset);
    Q_INVOKABLE void skip(int seconds);
    Q_INVOKABLE void jump(int track, qint64 offset, bool play = true);
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void setSpeed(double speed);
    Q_INVOKABLE void setVolume(double volume);
    Q_INVOKABLE void sleep(int minutes); // -1 = end of current chapter
    Q_INVOKABLE void bookmark(const QString &label);
    Q_INVOKABLE bool persist();
    QMediaPlayer media;
signals:
    void changed();
    void tick();
    void seeked(qint64 position);
private:
    void load(int track, qint64 offset, bool autoplay);
    void loaded();
    void ended();
    Library &lib;
    std::unique_ptr<QAudioOutput> output;
    QTimer saveTimer, sleepTimer;
    int currentBook = 0, currentTrack = 0;
    qint64 pending = -1, sleepBoundary = -1;
    bool autoPlay = false, switching = false, stoppedAtBoundary = false, listenedSinceSave = false;
};
