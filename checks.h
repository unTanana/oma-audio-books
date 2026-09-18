#pragma once
#include <QAudioBuffer>
#include <QAudioBufferOutput>
#include <QAudioOutput>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMediaPlayer>
#include <QThread>
#include <QUrl>
#include <functional>
#include <stdexcept>

inline void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
inline bool waitFor(const std::function<bool()> &ready, int timeout = 8000) {
    QElapsedTimer time;
    time.start();
    while (!ready() && time.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
    return ready();
}
inline int mediaProbe(const QStringList &paths) {
    try {
        require(paths.size() == 2, "probe needs synthetic MP3 and M4B paths");
        QMediaPlayer player;
        std::unique_ptr<QAudioOutput> audio;
        QAudioBufferOutput decoded;
        if (!qEnvironmentVariableIsSet("OMA_HEADLESS")) {
            audio = std::make_unique<QAudioOutput>();
            audio->setVolume(0.05);
            player.setAudioOutput(audio.get());
        }
        player.setAudioBufferOutput(&decoded);
        int buffers = 0;
        QObject::connect(&decoded, &QAudioBufferOutput::audioBufferReceived, &player,
                         [&](const QAudioBuffer &b) { if (b.isValid()) ++buffers; });
        require(player.pitchCompensationAvailability() != QMediaPlayer::PitchCompensationAvailability::Unavailable,
                "backend lacks pitch compensation");
        require(player.pitchCompensation(), "pitch compensation disabled");
        for (const auto &path : paths) {
            player.setSource(QUrl::fromLocalFile(path));
            require(waitFor([&] { return player.isSeekable() && player.duration() > 0; }), "load/seekable timeout");
            player.setPosition(1000);
            player.setPlaybackRate(1.5);
            QElapsedTimer rateClock;
            rateClock.start();
            player.play();
            require(waitFor([&] { return player.position() > 2400 && buffers > 0; }), "decoded playback did not advance");
            const double observedRate = double(player.position() - 1000) / qMax<qint64>(1, rateClock.elapsed());
            require(observedRate > 1.15 && observedRate < 1.9, "1.5x media clock did not match wall clock");
            player.pause();
            const auto saved = player.position();
            player.setSource({});
            player.setSource(QUrl::fromLocalFile(path));
            require(waitFor([&] { return player.isSeekable() && player.duration() > 0; }), "resume load timeout");
            player.setPosition(saved);
            require(waitFor([&] { return qAbs(player.position() - saved) < 100; }), "resume seek inaccurate");
            require(player.playbackState() != QMediaPlayer::PlayingState, "resume unexpectedly autoplayed");
            player.setPosition(player.duration() - 450);
            player.play();
            require(waitFor([&] { return player.mediaStatus() == QMediaPlayer::EndOfMedia; }), "file did not finish");
            require(player.error() == QMediaPlayer::NoError, "backend playback error");
        }
        qInfo("PASS: real Qt FFmpeg decoded MP3/M4B, seek, paused resume, 1.5x, pitch-compensation enabled, sequential sources");
        return 0;
    } catch (const std::exception &e) {
        qCritical("FAIL: %s", e.what());
        return 1;
    }
}
