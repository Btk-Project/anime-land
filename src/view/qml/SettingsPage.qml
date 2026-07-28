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

    function saveLogging() {
        if (!root.liveSettings)
            return
        Qt.inputMethod.commit()
        settingsViewModel.saveLogSettings(
            logLevelCombo.currentText, logDirectoryField.text,
            Number(logSizeField.text), Number(logCountField.text))
    }

    function saveBangumiCache() {
        if (!root.liveSettings)
            return
        Qt.inputMethod.commit()
        settingsViewModel.saveBangumiCacheSettings(
            cacheEnabledSwitch.checked, cacheDirectoryField.text,
            Number(cacheSizeField.text), Number(cacheTtlField.text))
    }

    Timer {
        interval: 100
        running: uiSettingsSmokeTest
        repeat: false
        onTriggered: settingsFlickable.contentY = Math.max(
            0, cacheSettingsHeader.y - Theme.pageMargin)
    }

    Flickable {
        id: settingsFlickable
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

            SectionHeader {
                id: cacheSettingsHeader
                width: parent.width
                title: "Bangumi 缓存"
                detail: "仅缓存公开接口数据与图片"
            }

            Rectangle {
                width: parent.width
                height: cacheSettingsForm.implicitHeight + 32
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    id: cacheSettingsForm
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5

                            AppText {
                                text: "启用磁盘缓存"
                                color: Theme.text
                                font.pixelSize: Theme.bodySize
                                font.weight: Font.DemiBold
                            }

                            AppText {
                                Layout.fillWidth: true
                                text: "不加密、不保存登录凭据或用户私有接口数据；关闭或设为 0 MiB 时不会创建缓存目录。"
                                color: Theme.textMuted
                                font.pixelSize: Theme.captionSize
                                wrapMode: Text.Wrap
                            }
                        }

                        Switch {
                            id: cacheEnabledSwitch
                            checked: root.liveSettings
                                     ? settingsViewModel.bangumiCacheEnabled
                                     : true
                            enabled: root.liveSettings
                                     && !settingsViewModel.saving
                        }
                    }

                    AppText {
                        text: "缓存目录"
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize
                    }

                    AppTextField {
                        id: cacheDirectoryField
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        text: root.liveSettings
                              ? settingsViewModel.bangumiCacheDirectory : ""
                        placeholderText: "Bangumi 缓存输出目录"
                        enabled: root.liveSettings
                                 && !settingsViewModel.saving
                                 && cacheEnabledSwitch.checked
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            AppText {
                                text: "总大小上限（MiB，0 表示关闭）"
                                color: Theme.textMuted
                                font.pixelSize: Theme.captionSize
                            }

                            AppTextField {
                                id: cacheSizeField
                                Layout.fillWidth: true
                                Layout.preferredHeight: 40
                                text: root.liveSettings
                                      ? String(settingsViewModel
                                               .bangumiCacheMaxSizeMiB)
                                      : "512"
                                inputMethodHints: Qt.ImhDigitsOnly
                                validator: IntValidator {
                                    bottom: 0
                                    top: 102400
                                }
                                enabled: root.liveSettings
                                         && !settingsViewModel.saving
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            AppText {
                                text: "有效期（天）"
                                color: Theme.textMuted
                                font.pixelSize: Theme.captionSize
                            }

                            AppTextField {
                                id: cacheTtlField
                                Layout.fillWidth: true
                                Layout.preferredHeight: 40
                                text: root.liveSettings
                                      ? String(settingsViewModel
                                               .bangumiCacheTtlDays)
                                      : "7"
                                inputMethodHints: Qt.ImhDigitsOnly
                                validator: IntValidator {
                                    bottom: 1
                                    top: 3650
                                }
                                enabled: root.liveSettings
                                         && !settingsViewModel.saving
                                         && cacheEnabledSwitch.checked
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        AppText {
                            Layout.fillWidth: true
                            text: cacheEnabledSwitch.checked
                                  && Number(cacheSizeField.text) > 0
                                  ? "API 与图片共用此上限；修改后下次启动生效"
                                  : "缓存已关闭；不会新建本地临时缓存"
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                        }

                        AppButton {
                            text: root.liveSettings
                                  && settingsViewModel.saving
                                  ? "正在保存…" : "保存缓存设置"
                            primary: true
                            enabled: root.liveSettings
                                     && !settingsViewModel.saving
                                     && cacheSizeField.acceptableInput
                                     && cacheTtlField.acceptableInput
                            onClicked: root.saveBangumiCache()
                        }
                    }
                }
            }

            SectionHeader {
                width: parent.width
                title: "日志"
                detail: "修改后立即生效"
            }

            Rectangle {
                width: parent.width
                height: logSettingsForm.implicitHeight + 32
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    id: logSettingsForm
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 10

                    AppText {
                        text: "日志级别"
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize
                    }

                    ComboBox {
                        id: logLevelCombo
                        Layout.preferredWidth: 220
                        height: 40
                        model: ["trace", "debug", "info", "warn",
                                "error", "critical"]
                        currentIndex: root.liveSettings
                                      ? Math.max(0, model.indexOf(
                                          settingsViewModel.logLevel)) : 2
                        enabled: root.liveSettings
                                 && !settingsViewModel.saving
                        leftPadding: 13
                        rightPadding: 34

                        contentItem: AppText {
                            text: logLevelCombo.displayText
                            color: Theme.text
                            font.pixelSize: Theme.bodySize
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: Theme.surface
                            border.width: 1
                            border.color: logLevelCombo.activeFocus
                                          ? Theme.accent : Theme.border
                        }

                        popup: Popup {
                            y: logLevelCombo.height + 4
                            width: logLevelCombo.width
                            implicitHeight: contentItem.implicitHeight + 8
                            padding: 4

                            background: Rectangle {
                                radius: Theme.radiusSmall
                                color: Theme.surfaceRaised
                                border.width: 1
                                border.color: Theme.border
                            }

                            contentItem: ListView {
                                implicitHeight: contentHeight
                                clip: true
                                model: logLevelCombo.popup.visible
                                       ? logLevelCombo.delegateModel : null
                                currentIndex: logLevelCombo.highlightedIndex
                            }
                        }

                        delegate: ItemDelegate {
                            width: logLevelCombo.width - 8
                            height: 34
                            highlighted: logLevelCombo.highlightedIndex
                                         === index
                            contentItem: AppText {
                                text: modelData
                                color: Theme.text
                                font.pixelSize: Theme.bodySize
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: Theme.radiusSmall
                                color: parent.highlighted
                                       ? Theme.surfaceHover
                                       : "transparent"
                            }
                        }
                    }

                    AppText {
                        text: "日志目录"
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize
                    }

                    AppTextField {
                        id: logDirectoryField
                        Layout.fillWidth: true
                        height: 40
                        text: root.liveSettings
                              ? settingsViewModel.logDirectory : ""
                        placeholderText: "日志文件输出目录"
                        enabled: root.liveSettings
                                 && !settingsViewModel.saving
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            AppText {
                                text: "单个文件上限（MiB）"
                                color: Theme.textMuted
                                font.pixelSize: Theme.captionSize
                            }

                            AppTextField {
                                id: logSizeField
                                Layout.fillWidth: true
                                height: 40
                                text: root.liveSettings
                                      ? String(settingsViewModel.logMaxFileSizeMiB)
                                      : "10"
                                inputMethodHints: Qt.ImhDigitsOnly
                                validator: IntValidator { bottom: 1; top: 1024 }
                                enabled: root.liveSettings
                                         && !settingsViewModel.saving
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            AppText {
                                text: "保留文件数"
                                color: Theme.textMuted
                                font.pixelSize: Theme.captionSize
                            }

                            AppTextField {
                                id: logCountField
                                Layout.fillWidth: true
                                height: 40
                                text: root.liveSettings
                                      ? String(settingsViewModel.logMaxFileCount)
                                      : "5"
                                inputMethodHints: Qt.ImhDigitsOnly
                                validator: IntValidator { bottom: 1; top: 100 }
                                enabled: root.liveSettings
                                         && !settingsViewModel.saving
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        AppText {
                            Layout.fillWidth: true
                            text: root.liveSettings
                                  ? "当前文件："
                                    + settingsViewModel.activeLogFile : ""
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                            elide: Text.ElideMiddle
                        }

                        AppButton {
                            text: root.liveSettings
                                  && settingsViewModel.saving
                                  ? "正在保存…" : "保存日志设置"
                            primary: true
                            enabled: root.liveSettings
                                     && !settingsViewModel.saving
                                     && logSizeField.acceptableInput
                                     && logCountField.acceptableInput
                            onClicked: root.saveLogging()
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
