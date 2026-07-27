import QtQuick
import QtQuick.Controls

Item {
    id: root

    signal openSubject(var subject)

    property int currentTab: 0
    property int fixtureSelectedWeekday: FixtureData.currentWeekdayId()
    property bool fixtureCollectionOnly: false

    function submitSearch() {
        if (uiFixtureMode)
            return
        Qt.inputMethod.commit()
        Qt.callLater(function() {
            if (bangumiBrowserViewModel)
                bangumiBrowserViewModel.search(searchField.text)
        })
    }

    function selectTab(tab) {
        currentTab = tab
        if (!uiFixtureMode && tab === 2 && bangumiBrowserViewModel
                && bangumiBrowserViewModel.loggedIn
                && bangumiBrowserViewModel.collectionResults.length === 0)
            bangumiBrowserViewModel.refreshCollections()
    }

    readonly property var calendarDays: uiFixtureMode
        ? FixtureData.calendarDays : calendarViewModel.days
    readonly property int selectedWeekday: uiFixtureMode
        ? fixtureSelectedWeekday : calendarViewModel.selectedWeekday
    readonly property var selectedCalendarDay: {
        if (uiFixtureMode)
            return FixtureData.calendarDay(fixtureSelectedWeekday)
        return {
            "id": calendarViewModel.selectedWeekday,
            "label": calendarViewModel.selectedWeekdayLabel,
            "items": calendarViewModel.selectedItems
        }
    }
    readonly property bool calendarLoading:
        !uiFixtureMode && calendarViewModel.loading
    readonly property string calendarError: uiFixtureMode
        ? "" : calendarViewModel.errorMessage
    property var visibleSubjects: {
        if (currentTab === 0) {
            const items = selectedCalendarDay.items
            if (!uiFixtureMode || !fixtureCollectionOnly)
                return items
            return items.filter(function(subject) {
                return subject.progress > 0 && subject.progress < 1
            })
        }

        if (!uiFixtureMode) {
            if (!bangumiBrowserViewModel)
                return []
            return currentTab === 1
                    ? bangumiBrowserViewModel.searchResults
                    : bangumiBrowserViewModel.collectionResults
        }
        const base = currentTab === 1 ? FixtureData.bangumiSubjects
                                      : FixtureData.subjects.slice(0, 4)
        const query = searchField.text.trim().toLowerCase()
        if (query.length === 0)
            return base
        return base.filter(function(subject) {
            return subject.title.toLowerCase().indexOf(query) >= 0
                    || subject.subtitle.toLowerCase().indexOf(query) >= 0
        })
    }

    function selectWeekday(weekday) {
        if (uiFixtureMode)
            fixtureSelectedWeekday = weekday
        else
            calendarViewModel.selectedWeekday = weekday
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
                title: "Bangumi"
                subtitle: uiFixtureMode
                    ? "使用 fixture 调试每日放送、搜索与收藏界面。"
                    : "每日放送、公开搜索、账号与收藏均使用真实 Bangumi 数据。"
            }

            Row {
                spacing: 10

                AppButton {
                    text: "每日放送"
                    primary: root.currentTab === 0
                    onClicked: root.selectTab(0)
                }

                AppButton {
                    text: "搜索"
                    primary: root.currentTab === 1
                    onClicked: root.selectTab(1)
                }

                AppButton {
                    text: "我的收藏"
                    primary: root.currentTab === 2
                    onClicked: root.selectTab(2)
                }

                Rectangle {
                    width: 1
                    height: 28
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.border
                }

                AppText {
                    anchors.verticalCenter: parent.verticalCenter
                    text: {
                        if (root.currentTab === 0)
                            return uiFixtureMode ? "Fixture 时间表" : "真实时间表"
                        if (root.currentTab === 1)
                            return uiFixtureMode ? "Fixture 搜索" : "公开搜索"
                        if (uiFixtureMode)
                            return "Fixture 账户"
                        return bangumiBrowserViewModel
                               ? bangumiBrowserViewModel.accountStatus
                               : "账户服务未初始化"
                    }
                    color: Theme.textMuted
                    font.pixelSize: Theme.captionSize
                }
            }

            Row {
                visible: root.currentTab === 0
                spacing: 8

                Repeater {
                    model: root.calendarDays

                    AppButton {
                        text: modelData.shortLabel
                        primary: root.selectedWeekday === modelData.id
                        implicitWidth: 46
                        onClicked: root.selectWeekday(modelData.id)
                    }
                }

                Item { width: 4; height: 1 }

                AppButton {
                    visible: uiFixtureMode
                    text: "只看在看"
                    primary: root.fixtureCollectionOnly
                    onClicked: root.fixtureCollectionOnly =
                               !root.fixtureCollectionOnly
                }

                AppButton {
                    visible: !uiFixtureMode
                    text: root.calendarLoading ? "加载中" : "刷新"
                    enabled: !root.calendarLoading
                    onClicked: calendarViewModel.refresh()
                }
            }

            Row {
                visible: root.currentTab === 1
                height: visible ? 40 : 0
                spacing: 10

                AppTextField {
                    id: searchField
                    width: Math.min(520, content.width - 110)
                    height: 40
                    placeholderText: "搜索 Bangumi 动画条目"
                    onAccepted: {
                        if (!inputMethodComposing)
                            root.submitSearch()
                    }
                }

                AppButton {
                    visible: !uiFixtureMode
                    text: bangumiBrowserViewModel
                          && bangumiBrowserViewModel.searchLoading
                          ? "搜索中…" : "搜索"
                    primary: true
                    enabled: bangumiBrowserViewModel
                             && !bangumiBrowserViewModel.searchLoading
                    onPressed: Qt.inputMethod.commit()
                    onClicked: root.submitSearch()
                }
            }

            Row {
                visible: root.currentTab === 2 && !uiFixtureMode
                height: visible ? 40 : 0
                spacing: 10

                AppButton {
                    text: bangumiBrowserViewModel
                          && bangumiBrowserViewModel.loggedIn
                          ? (bangumiBrowserViewModel.collectionsLoading
                             ? "读取中…" : "刷新收藏")
                          : "登录 Bangumi"
                    primary: true
                    enabled: bangumiBrowserViewModel
                             && !bangumiBrowserViewModel.accountBusy
                             && !bangumiBrowserViewModel.collectionsLoading
                    onClicked: {
                        if (bangumiBrowserViewModel.loggedIn)
                            bangumiBrowserViewModel.refreshCollections()
                        else
                            bangumiBrowserViewModel.login()
                    }
                }

                AppButton {
                    visible: bangumiBrowserViewModel
                             && bangumiBrowserViewModel.loggedIn
                    text: "退出登录"
                    enabled: bangumiBrowserViewModel
                             && !bangumiBrowserViewModel.accountBusy
                    onClicked: bangumiBrowserViewModel.logout()
                }
            }

            AppText {
                visible: root.currentTab === 0
                         && (root.calendarLoading
                             || root.calendarError.length > 0)
                width: parent.width
                text: root.calendarLoading
                    ? "正在从 Bangumi 加载每日放送……"
                    : root.calendarError
                color: root.calendarError.length > 0
                       ? Theme.danger : Theme.textMuted
                font.pixelSize: Theme.bodySize
                wrapMode: Text.Wrap
            }

            AppText {
                visible: !uiFixtureMode && root.currentTab === 1
                         && bangumiBrowserViewModel
                         && (bangumiBrowserViewModel.searchLoading
                             || bangumiBrowserViewModel.searchError.length > 0)
                width: parent.width
                text: bangumiBrowserViewModel
                      ? (bangumiBrowserViewModel.searchLoading
                         ? "正在搜索 Bangumi……"
                         : bangumiBrowserViewModel.searchError) : ""
                color: bangumiBrowserViewModel
                       && bangumiBrowserViewModel.searchError.length > 0
                       ? Theme.danger : Theme.textMuted
                font.pixelSize: Theme.bodySize
                wrapMode: Text.Wrap
            }

            AppText {
                visible: !uiFixtureMode && root.currentTab === 2
                         && bangumiBrowserViewModel
                         && bangumiBrowserViewModel.collectionsError.length > 0
                width: parent.width
                text: bangumiBrowserViewModel
                      ? bangumiBrowserViewModel.collectionsError : ""
                color: Theme.danger
                font.pixelSize: Theme.bodySize
                wrapMode: Text.Wrap
            }

            SectionHeader {
                width: parent.width
                title: {
                    if (root.currentTab === 0) {
                        const label = root.selectedCalendarDay.label
                        return label.length > 0 ? label + "放送" : "每日放送"
                    }
                    if (root.currentTab === 1)
                        return "搜索结果"
                    return "在看与收藏"
                }
                detail: {
                    if (root.currentTab === 0 && root.calendarLoading)
                        return "正在加载"
                    if (root.currentTab === 0
                            && root.calendarError.length > 0)
                        return "加载失败"
                    if (!uiFixtureMode && root.currentTab === 1
                            && bangumiBrowserViewModel)
                        return root.visibleSubjects.length + " / "
                               + bangumiBrowserViewModel.searchTotal + " 个条目"
                    if (!uiFixtureMode && root.currentTab === 2
                            && bangumiBrowserViewModel)
                        return root.visibleSubjects.length + " / "
                               + bangumiBrowserViewModel.collectionTotal
                               + " 个收藏"
                    return root.visibleSubjects.length + " 个条目"
                }
            }

            Flow {
                width: parent.width
                height: childrenRect.height
                spacing: 16

                Repeater {
                    model: root.visibleSubjects

                    SubjectCard {
                        subject: modelData
                        statusText: modelData.status
                        onActivated: selected => root.openSubject(selected)
                    }
                }
            }

            AppButton {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !uiFixtureMode && bangumiBrowserViewModel
                         && ((root.currentTab === 1
                              && bangumiBrowserViewModel.hasMoreSearch)
                             || (root.currentTab === 2
                                 && bangumiBrowserViewModel.hasMoreCollections))
                text: bangumiBrowserViewModel
                      && ((root.currentTab === 1
                           && bangumiBrowserViewModel.searchLoading)
                          || (root.currentTab === 2
                              && bangumiBrowserViewModel.collectionsLoading))
                      ? "正在加载…" : "加载更多"
                enabled: bangumiBrowserViewModel
                         && (root.currentTab === 1
                             ? !bangumiBrowserViewModel.searchLoading
                             : !bangumiBrowserViewModel.collectionsLoading)
                onClicked: {
                    if (root.currentTab === 1)
                        bangumiBrowserViewModel.loadMoreSearch()
                    else
                        bangumiBrowserViewModel.loadMoreCollections()
                }
            }

            AppText {
                visible: !root.calendarLoading
                         && root.calendarError.length === 0
                         && root.visibleSubjects.length === 0
                         && (uiFixtureMode || !bangumiBrowserViewModel
                             || (root.currentTab !== 1
                                 || !bangumiBrowserViewModel.searchLoading)
                             && (root.currentTab !== 2
                                 || !bangumiBrowserViewModel.collectionsLoading))
                width: parent.width
                text: root.currentTab === 0 && root.fixtureCollectionOnly
                      ? "这一天没有正在观看的条目。"
                      : root.currentTab === 1
                        ? "输入动画名称后开始搜索。"
                        : (!uiFixtureMode && bangumiBrowserViewModel
                           && !bangumiBrowserViewModel.loggedIn
                           ? "登录后可读取真实收藏。" : "没有匹配的条目。")
                color: Theme.textMuted
                font.pixelSize: Theme.bodySize
            }

            Item { width: 1; height: 16 }
        }
    }
}
