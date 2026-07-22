#pragma once

#if defined(ANIME_LAND_HAS_SECRET_SERVICE)

#include <QDBusVariant>
#include <QEventLoop>
#include <QObject>

namespace anime_land::detail {

/**
 * @brief 接收 Secret Service Prompt.Completed 信号的局部等待器。
 * @invariant mLoop 仅在同步等待期间非空，且与本对象位于同一线程。
 */
class SecretServicePromptWaiter final : public QObject {
  Q_OBJECT

public:
  /**
   * @brief 绑定要结束的局部事件循环。
   * @param loop 当前线程中尚未销毁的事件循环。
   * @pre loop 非空，且本对象尚未收到 Completed。
   * @post 收到 Completed 后会调用 loop->quit()。
   */
  void setEventLoop(QEventLoop *loop) noexcept { mLoop = loop; }

  /** @return 是否已经收到 Secret Service 的 Completed 信号。 */
  [[nodiscard]] bool isCompleted() const noexcept { return mCompleted; }

  /** @return 用户或服务是否取消了系统凭据提示。 */
  [[nodiscard]] bool isDismissed() const noexcept { return mDismissed; }

  /** @return Completed 信号携带的协议结果 variant。 */
  [[nodiscard]] const QDBusVariant &result() const noexcept { return mResult; }

public slots:
  /**
   * @brief 处理 org.freedesktop.Secret.Prompt.Completed。
   * @param dismissed true 表示用户或服务取消操作。
   * @param result 提示所完成操作的协议结果。
   * @pre 该槽由绑定的 session bus 在本对象所属线程调用。
   * @post 保存结果、标记完成，并结束 setEventLoop() 指定的事件循环。
   */
  void completed(bool dismissed, const QDBusVariant &result) {
    mCompleted = true;
    mDismissed = dismissed;
    mResult = result;
    if (mLoop != nullptr) {
      mLoop->quit();
    }
  }

private:
  QEventLoop *mLoop = nullptr;
  bool mCompleted = false;
  bool mDismissed = false;
  QDBusVariant mResult;
};

} // namespace anime_land::detail

#endif
