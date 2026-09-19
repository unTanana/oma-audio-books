#include "desktop.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFontDatabase>
#include <QRegularExpression>
#include <QStandardPaths>

Theme::Theme(QObject *parent) : QObject(parent), selected(QSettings().value("theme", "Follow Omarchy").toString()) {
    connect(&watcher, &QFileSystemWatcher::fileChanged, this, [this] { reload(); });
    connect(&watcher, &QFileSystemWatcher::directoryChanged, this, [this] { reload(); });
    // ponytail: a two-second palette poll also catches replaced symlink ancestors without recursive watches.
    connect(&poll, &QTimer::timeout, this, &Theme::reload); poll.start(2000);
    reload();
}
void Theme::setMode(const QString &mode) {
    if (mode != "Follow Omarchy" && mode != "Dark" && mode != "Light") return;
    selected = mode; QSettings().setValue("theme", mode); reload();
}
void Theme::reload() {
    QColor bg = selected == "Light" ? QColor("#f4f2ed") : QColor("#15191e");
    QColor fg = selected == "Light" ? QColor("#20252b") : QColor("#eee9df");
    QColor ac = selected == "Light" ? QColor("#315b86") : QColor("#9abddc");
    const bool flatpak = qEnvironmentVariableIsSet("FLATPAK_ID");
    const QString state = qEnvironmentVariable(flatpak ? "HOST_XDG_STATE_HOME" : "XDG_STATE_HOME", QDir::homePath() + "/.local/state");
    const QString config = flatpak ? qEnvironmentVariable("HOST_XDG_CONFIG_HOME", QDir::homePath() + "/.config")
                                  : QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QStringList paths{state + "/omarchy/current/theme/colors.toml", config + "/omarchy/current/theme/colors.toml"};
    QStringList watched;
    for (const auto &path : paths) {
        QString parent = path;
        for (int i = 0; i < 4; ++i) { if (QFileInfo::exists(parent)) watched << parent; parent = QFileInfo(parent).absolutePath(); }
    }
    if (selected == "Follow Omarchy") for (const auto &path : paths) {
        QFile file(path); if (!file.open(QIODevice::ReadOnly)) continue;
        const auto text = QString::fromUtf8(file.read(65536));
        auto color = [&](const QString &key) {
            auto m = QRegularExpression("(?:^|\\n)\\s*" + key + "\\s*=\\s*[\"'](#[0-9a-fA-F]{6})[\"']").match(text);
            return QColor(m.captured(1));
        };
        const auto b = color("background"), f = color("foreground"), a = color("accent");
        if (b.isValid() && f.isValid() && a.isValid() && qAbs(b.lightness() - f.lightness()) > 65) { bg = b; fg = f; ac = a; break; }
    }
    if (!watcher.files().isEmpty()) watcher.removePaths(watcher.files());
    if (!watcher.directories().isEmpty()) watcher.removePaths(watcher.directories());
    watched.removeDuplicates(); if (!watched.isEmpty()) watcher.addPaths(watched);
    const auto font = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    const bool different = bg != background || fg != foreground || ac != accent || font != family;
    background = bg; foreground = fg; accent = ac;
    surface = bg.lightness() < 128 ? bg.lighter(140) : bg.darker(105);
    family = font;
    if (different) emit changed();
}
void MprisRoot::Quit() { if (window) window->close(); else QCoreApplication::quit(); }
MprisPlayer::MprisPlayer(QObject *object, Player &player, Library &library) : QDBusAbstractAdaptor(object), p(player), lib(library) {
    connect(&p, &Player::changed, this, &MprisPlayer::notify);
    connect(&p.media, &QMediaPlayer::seekableChanged, this, &MprisPlayer::notify);
    connect(&p.media, &QMediaPlayer::durationChanged, this, &MprisPlayer::notify);
    connect(&p, &Player::seeked, this, [this](qint64 offset) { emit Seeked(offset * 1000); });
}
bool MprisPlayer::canNext() const {
    bool current = false;
    for (const auto &v : lib.chapters(p.bookId())) {
        const auto c = v.toMap();
        if (c["track"].toInt() == p.trackId()) { current = true; if (c["start"].toLongLong() > p.position() + 100) return true; }
        else if (current) return true;
    }
    return false;
}
QVariantMap MprisPlayer::metadata() const {
    if (!selected()) return {};
    const auto b = lib.book(p.bookId());
    QVariantMap result{{"mpris:trackid", QVariant::fromValue(trackPath())}, {"mpris:length", p.duration() * 1000},
        {"xesam:title", b["title"]}, {"xesam:album", b["title"]}, {"xesam:artist", QStringList{b["author"].toString()}}};
    if (!b["cover"].toString().isEmpty()) result["mpris:artUrl"] = b["coverUrl"].toUrl().toString();
    return result;
}
void MprisPlayer::notify() {
    auto message = QDBusMessage::createSignal("/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "PropertiesChanged");
    message << QString("org.mpris.MediaPlayer2.Player") << QVariantMap{{"PlaybackStatus", status()}, {"Rate", rate()}, {"Volume", volume()}, {"Metadata", metadata()}, {"CanGoNext", canNext()}, {"CanGoPrevious", selected()}, {"CanPlay", canPlay()}, {"CanPause", selected()}, {"CanSeek", canSeek()}} << QStringList{};
    QDBusConnection::sessionBus().send(message);
}
