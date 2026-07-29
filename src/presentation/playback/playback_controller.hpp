#pragma once

#include "model/playback/playback_session.hpp"

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <ilias/task.hpp>
#include <ilias/task/scope.hpp>

#include <cstdint>

namespace anime_land {

/** QML adapter for PlaybackSession; it never exposes nekoav objects. */
class PlaybackController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString stateName READ stateName NOTIFY snapshotChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY snapshotChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY snapshotChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY snapshotChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY snapshotChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY snapshotChanged)
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY mediaChanged)

public:
    explicit PlaybackController(PlaybackSession &session,
                                QObject *parent = nullptr);
    ~PlaybackController() override;

    auto stateName() const -> QString;
    auto playing() const -> bool;
    auto busy() const -> bool;
    auto positionMs() const -> qint64;
    auto durationMs() const -> qint64;
    auto errorMessage() const -> QString;
    auto mediaTitle() const -> QString;

    auto openMedia(const QUrl &source) -> bool;
    auto openMedia(const QUrl &source, QString displayTitle) -> bool;
    auto shutdown() -> ilias::Task<void>;

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void stop();

signals:
    void snapshotChanged();
    void mediaChanged();
    void openRequested(const QString &title);

private:
    auto openAndPlay(QUrl source) -> ilias::Task<void>;
    auto send(PlaybackCommand command) -> ilias::Task<void>;
    void refreshSnapshot();

    PlaybackSession &mSession;
    PlaybackSnapshot mSnapshot;
    ilias::TaskScope mTasks;
    ilias::WaitHandle<void> mRunner;
    QTimer mRefreshTimer;
    QString mMediaTitle;
    bool mClosed = false;
};

} // namespace anime_land
