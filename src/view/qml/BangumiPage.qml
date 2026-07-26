import QtQuick
import QtQuick.Controls

Item {
    id: root

    signal openSubject(var subject)

    property int currentTab: 0
    property int fixtureSelectedWeekday: FixtureData.currentWeekdayId()
    property bool fixtureCollectionOnly: false

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

        const base = currentTab === 1
                ? FixtureData.bangumiSubjects
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
                    : "每日放送已连接真实数据；搜索与收藏仍使用 fixture。"
            }

            Row {
                spacing: 10

                AppButton {
                    text: "每日放送"
                    primary: root.currentTab === 0
                    onClicked: root.currentTab = 0
                }

                AppButton {
                    text: "搜索"
                    primary: root.currentTab === 1
                    onClicked: root.currentTab = 1
                }

                AppButton {
                    text: "我的收藏"
                    primary: root.currentTab === 2
                    onClicked: root.currentTab = 2
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
                            return "Fixture 搜索"
                        return "Fixture 账户"
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

            TextField {
                id: searchField
                visible: root.currentTab !== 0
                width: Math.min(520, parent.width)
                height: visible ? 40 : 0
                placeholderText: root.currentTab === 1
                                 ? "搜索 Bangumi 动画条目"
                                 : "筛选我的收藏"
                color: Theme.text
                placeholderTextColor: Theme.textFaint
                selectionColor: Theme.accent
                selectedTextColor: Theme.accentText
                font.pixelSize: Theme.bodySize
                leftPadding: 13

                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.surface
                    border.width: 1
                    border.color: searchField.activeFocus
                                  ? Theme.accent : Theme.border
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

            AppText {
                visible: !root.calendarLoading
                         && root.calendarError.length === 0
                         && root.visibleSubjects.length === 0
                width: parent.width
                text: root.currentTab === 0 && root.fixtureCollectionOnly
                      ? "这一天没有正在观看的条目。" : "没有匹配的条目。"
                color: Theme.textMuted
                font.pixelSize: Theme.bodySize
            }

            Item { width: 1; height: 16 }
        }
    }
}
