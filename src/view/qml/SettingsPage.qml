import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    signal libraryRequested()

    readonly property bool liveSettings: !uiFixtureMode && settingsViewModel
    readonly property string stateMessage: {
        if (!liveSettings)
            return "Fixture 模式不会读取或写入真实配置。"
        if (settingsViewModel.errorMessage.length > 0)
            return settingsViewModel.errorMessage
        return settingsViewModel.noticeMessage
    }

    function selectTheme(mode) {
        if (root.liveSettings)
            settingsViewModel.themeMode = mode
        else
            Theme.preference = mode
    }

    function saveBangumi() {
        if (!root.liveSettings)
            return
        Qt.inputMethod.commit()
        Qt.callLater(function() {
            settingsViewModel.saveBangumiSettings(
                clientIdField.text, clientSecretField.text,
                redirectField.text, proxyField.text)
            clientSecretField.clear()
        })
    }

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
                subtitle: "外观会立即生效；Bangumi 与网络配置会安全保存到本机。"
            }

            SectionHeader { width: parent.width; title: "外观" }

            Rectangle {
                width: parent.width
                height: 98
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 18

                    Column {
                        Layout.fillWidth: true
                        spacing: 7

                        AppText {
                            text: "主题"
                            color: Theme.text
                            font.pixelSize: Theme.bodySize
                            font.weight: Font.DemiBold
                        }

                        AppText {
                            text: root.liveSettings
                                  ? (settingsViewModel.systemDark
                                     ? "系统当前为深色；选择会持久化"
                                     : "系统当前为浅色；选择会持久化")
                                  : "真实模式会读取系统配色并保存用户选择"
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                        }
                    }

                    Row {
                        spacing: 8

                        Repeater {
                            model: [
                                {"mode": "system", "label": "跟随系统"},
                                {"mode": "dark", "label": "深色"},
                                {"mode": "light", "label": "浅色"}
                            ]

                            AppButton {
                                text: modelData.label
                                primary: Theme.preference === modelData.mode
                                enabled: root.liveSettings
                                         && !settingsViewModel.saving
                                onClicked: root.selectTheme(modelData.mode)
                            }
                        }
                    }
                }
            }

            SectionHeader { width: parent.width; title: "Bangumi 账户" }

            Rectangle {
                width: parent.width
                height: 92
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 18

                    Column {
                        Layout.fillWidth: true
                        spacing: 7

                        AppText {
                            text: !uiFixtureMode && bangumiBrowserViewModel
                                  ? bangumiBrowserViewModel.accountStatus
                                  : "Fixture 账户"
                            color: Theme.text
                            font.pixelSize: Theme.bodySize
                            font.weight: Font.DemiBold
                        }

                        AppText {
                            text: root.liveSettings
                                  && settingsViewModel.credentialPersistenceAvailable
                                  ? "OAuth Token 使用系统凭据库保存"
                                  : "当前会话的 Token 不会持久化"
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                        }
                    }

                    AppButton {
                        visible: !uiFixtureMode && bangumiBrowserViewModel
                        text: bangumiBrowserViewModel
                              && bangumiBrowserViewModel.loggedIn
                              ? "退出登录" : "登录"
                        primary: bangumiBrowserViewModel
                                 && !bangumiBrowserViewModel.loggedIn
                        enabled: bangumiBrowserViewModel
                                 && !bangumiBrowserViewModel.accountBusy
                        onClicked: {
                            if (bangumiBrowserViewModel.loggedIn)
                                bangumiBrowserViewModel.logout()
                            else
                                bangumiBrowserViewModel.login()
                        }
                    }
                }
            }

            SectionHeader {
                width: parent.width
                title: "Bangumi 应用与网络"
                detail: root.liveSettings
                        && settingsViewModel.clientSecretConfigured
                        ? "App Secret 已配置" : "App Secret 未配置"
            }

            Rectangle {
                width: parent.width
                height: settingsForm.implicitHeight + 32
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    id: settingsForm
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 10

                    AppText {
                        text: "App ID"
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize
                    }

                    AppTextField {
                        id: clientIdField
                        Layout.fillWidth: true
                        height: 40
                        text: root.liveSettings
                              ? settingsViewModel.bangumiClientId : ""
                        placeholderText: "Bangumi OAuth App ID"
                        enabled: root.liveSettings && !settingsViewModel.saving
                    }

                    AppText {
                        text: "App Secret（留空表示保持原值）"
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize
                    }

                    AppTextField {
                        id: clientSecretField
                        Layout.fillWidth: true
                        height: 40
                        placeholderText: root.liveSettings
                                         && settingsViewModel.clientSecretConfigured
                                         ? "已配置；输入新值可替换" : "App Secret"
                        echoMode: TextInput.Password
                        enabled: root.liveSettings && !settingsViewModel.saving
                    }

                    AppText {
                        text: "OAuth 回调地址"
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize
                    }

                    AppTextField {
                        id: redirectField
                        Layout.fillWidth: true
                        height: 40
                        text: root.liveSettings
                              ? settingsViewModel.redirectUri : ""
                        placeholderText: "http://127.0.0.1:38457/callback"
                        enabled: root.liveSettings && !settingsViewModel.saving
                    }

                    AppText {
                        text: "HTTP / SOCKS5 代理（可留空）"
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize
                    }

                    AppTextField {
                        id: proxyField
                        Layout.fillWidth: true
                        height: 40
                        text: root.liveSettings
                              ? settingsViewModel.proxyUrl : ""
                        placeholderText: "例如 http://127.0.0.1:7890"
                        enabled: root.liveSettings && !settingsViewModel.saving
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        AppText {
                            Layout.fillWidth: true
                            text: root.liveSettings
                                  && settingsViewModel.restartRequired
                                  ? "部分网络设置需要重启应用" : ""
                            color: Theme.warning
                            font.pixelSize: Theme.captionSize
                        }

                        AppButton {
                            text: root.liveSettings
                                  && settingsViewModel.saving
                                  ? "正在保存…" : "保存设置"
                            primary: true
                            enabled: root.liveSettings
                                     && !settingsViewModel.saving
                            onClicked: root.saveBangumi()
                        }
                    }
                }
            }

            SectionHeader { width: parent.width; title: "媒体库与诊断" }

            SettingCard {
                width: parent.width
                title: "本地媒体库"
                description: !uiFixtureMode && libraryViewModel
                             ? libraryViewModel.mediaCount
                               + " 个媒体文件；数据库："
                               + (settingsViewModel
                                  ? settingsViewModel.databasePath : "")
                             : "Fixture 媒体库"
                actionText: "打开媒体库"
                onActionRequested: root.libraryRequested()
            }

            Rectangle {
                width: parent.width
                height: diagnostics.implicitHeight + 28
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                Column {
                    id: diagnostics
                    x: 14
                    y: 14
                    width: parent.width - 28
                    spacing: 7

                    AppText {
                        width: parent.width
                        text: "配置文件：" + (root.liveSettings
                              ? settingsViewModel.configPath : "Fixture")
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize
                        elide: Text.ElideMiddle
                    }

                    AppText {
                        width: parent.width
                        text: root.stateMessage
                        color: root.liveSettings
                               && settingsViewModel.errorMessage.length > 0
                               ? Theme.danger : Theme.textMuted
                        font.pixelSize: Theme.captionSize
                        wrapMode: Text.Wrap
                    }
                }
            }

            Item { width: 1; height: 16 }
        }
    }
}
