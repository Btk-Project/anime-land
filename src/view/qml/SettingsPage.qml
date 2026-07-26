import QtQuick
import QtQuick.Controls

Item {
    id: root

    property string notice: ""

    Flickable {
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: content.implicitHeight + Theme.pageMargin
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: content
            x: Theme.pageMargin
            width: parent.width - Theme.pageMargin * 2
            spacing: Theme.spacing

            PageHeader {
                width: parent.width
                title: "设置"
                subtitle: "管理账户、媒体资源、播放偏好和应用行为。"
            }

            SectionHeader { width: parent.width; title: "账户" }

            SettingCard {
                width: parent.width
                title: "Bangumi 账户"
                description: "Fixture 用户 · 登录状态和 OAuth 参数由 Presentation 管理"
                actionText: "账户管理"
                onActionRequested: root.notice = "Fixture：账户管理对话框尚未接入"
            }

            SectionHeader { width: parent.width; title: "媒体库" }

            SettingCard {
                width: parent.width
                title: "媒体资源与扫描目录"
                description: "2 个本地资源根 · 上次扫描于今天 18:42"
                actionText: "资源管理"
                onActionRequested: root.notice = "Fixture：LibraryStore 尚未接入"
            }

            SectionHeader { width: parent.width; title: "播放" }

            SettingCard {
                width: parent.width
                title: "播放与字幕"
                description: "自动继续播放 · 默认字幕轨 · 音量记忆"
                actionText: "播放偏好"
                onActionRequested: root.notice = "Fixture：PlaybackSession 尚未接入"
            }

            SectionHeader { width: parent.width; title: "应用" }

            SettingCard {
                width: parent.width
                title: "外观、缓存与诊断"
                description: "朴素深色主题 · 缓存目录 · 日志与版本信息"
                actionText: "应用设置"
                onActionRequested: root.notice = "Fixture：设置保存尚未接入"
            }

            Rectangle {
                width: parent.width
                height: root.notice.length > 0 ? 52 : 0
                radius: Theme.radiusSmall
                color: Theme.surfaceRaised
                border.width: root.notice.length > 0 ? 1 : 0
                border.color: Theme.border
                visible: root.notice.length > 0

                AppText {
                    anchors.centerIn: parent
                    text: root.notice
                    color: Theme.textMuted
                    font.pixelSize: Theme.captionSize
                }
            }

            Item { width: 1; height: 16 }
        }
    }
}
