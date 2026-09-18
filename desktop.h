#pragma once
#include "app.h"
#include <QColor>
#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QFileSystemWatcher>
#include <QWindow>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor background MEMBER background NOTIFY changed)
    Q_PROPERTY(QColor foreground MEMBER foreground NOTIFY changed)
    Q_PROPERTY(QColor accent MEMBER accent NOTIFY changed)
    Q_PROPERTY(QColor surface MEMBER surface NOTIFY changed)
    Q_PROPERTY(QString family MEMBER family NOTIFY changed)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
public:
    explicit Theme(QObject *parent = nullptr);
    QString mode() const { return selected; }
    void setMode(const QString &mode);
    void reload();
    QColor background, foreground, accent, surface;
    QString family;
signals:
    void changed();
private:
    QFileSystemWatcher watcher;
    QTimer poll;
    QString selected;
};
class MprisRoot : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ yes CONSTANT)
    Q_PROPERTY(bool CanRaise READ yes CONSTANT)
    Q_PROPERTY(bool HasTrackList READ no CONSTANT)
    Q_PROPERTY(QString Identity READ identity CONSTANT)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry CONSTANT)
    Q_PROPERTY(QStringList SupportedUriSchemes READ empty CONSTANT)
    Q_PROPERTY(QStringList SupportedMimeTypes READ empty CONSTANT)
public:
    MprisRoot(QObject *object, QWindow *window) : QDBusAbstractAdaptor(object), window(window) {}
    bool yes() const { return true; }
    bool no() const { return false; }
    QString identity() const { return "oma-audio-books"; }
    QString desktopEntry() const { return "oma-audio-books"; }
    QStringList empty() const { return {}; }
public slots:
    void Raise() { if (window) { window->showNormal(); window->raise(); window->requestActivate(); } }
    void Quit();
private:
    QWindow *window;
};
class MprisPlayer : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ status)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ minimum CONSTANT)
    Q_PROPERTY(double MaximumRate READ maximum CONSTANT)
    Q_PROPERTY(bool CanGoNext READ canNext)
    Q_PROPERTY(bool CanGoPrevious READ selected)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ selected)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ yes CONSTANT)
public:
    MprisPlayer(QObject *object, Player &player, Library &library);
    QString status() const { return p.playing() ? "Playing" : selected() && p.position() > 0 && p.media.mediaStatus() != QMediaPlayer::EndOfMedia ? "Paused" : "Stopped"; }
    double rate() const { return p.speed(); }
    void setRate(double value) { if (value == 0) p.pause(); else p.setSpeed(value); }
    double volume() const { return p.volume(); }
    void setVolume(double value) { p.setVolume(value); }
    qlonglong position() const { return p.position() * 1000; }
    double minimum() const { return 0.5; }
    double maximum() const { return 3; }
    bool selected() const { return p.trackId() > 0; }
    bool canPlay() const { return lib.book(p.bookId())["available"].toBool(); }
    bool canSeek() const { return p.media.isSeekable(); }
    bool canNext() const;
    bool yes() const { return true; }
    QDBusObjectPath trackPath() const { return QDBusObjectPath(selected() ? QString("/org/oma/track/%1").arg(p.trackId()) : "/org/mpris/MediaPlayer2/TrackList/NoTrack"); }
    QVariantMap metadata() const;
public slots:
    void Next() { if (canNext()) p.next(); }
    void Previous() { p.previous(); }
    void Pause() { p.pause(); }
    void PlayPause() { p.toggle(); }
    void Stop() { p.pause(); p.seek(0); p.media.stop(); p.persist(); }
    void Play() { p.play(); }
    void Seek(qlonglong offset) { if (canSeek()) { const auto target = p.position() + offset / 1000; if (target > p.duration()) Next(); else p.seek(target); } }
    void SetPosition(const QDBusObjectPath &track, qlonglong value) { if (canSeek() && track.path() == trackPath().path() && value >= 0 && value / 1000 <= p.duration()) p.seek(value / 1000); }
signals:
    void Seeked(qlonglong position);
private:
    void notify();
    Player &p;
    Library &lib;
};
