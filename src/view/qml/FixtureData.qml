pragma Singleton

import QtQuick

QtObject {
    readonly property var subjects: [
        {
            "id": "frieren",
            "title": "葬送的芙莉莲",
            "subtitle": "Sousou no Frieren",
            "meta": "2023 · TV · 28 集",
            "episode": "第 17 集 · 好好照顾",
            "progress": 0.64,
            "score": "8.8",
            "status": "继续观看",
            "color": "#59636b",
            "summary": "魔王被打倒之后，精灵魔法使芙莉莲重新踏上理解时间与人的旅途。"
        },
        {
            "id": "dungeon-meshi",
            "title": "迷宫饭",
            "subtitle": "Dungeon Meshi",
            "meta": "2024 · TV · 24 集",
            "episode": "第 9 集 · 触手",
            "progress": 0.36,
            "score": "8.1",
            "status": "在看",
            "color": "#5f6859",
            "summary": "为了救回同伴，莱欧斯一行在迷宫中就地取材，烹饪魔物并继续前进。"
        },
        {
            "id": "apothecary",
            "title": "药屋少女的呢喃",
            "subtitle": "Kusuriya no Hitorigoto",
            "meta": "2023 · TV · 24 集",
            "episode": "第 12 集 · 宦官与妓女",
            "progress": 0.48,
            "score": "8.2",
            "status": "最近加入",
            "color": "#685d58",
            "summary": "药师猫猫凭借毒物与药学知识，解开后宫中接连出现的奇异事件。"
        },
        {
            "id": "bocchi",
            "title": "孤独摇滚！",
            "subtitle": "Bocchi the Rock!",
            "meta": "2022 · TV · 12 集",
            "episode": "第 8 集 · 波奇摇滚",
            "progress": 1.0,
            "score": "8.7",
            "status": "已看完",
            "color": "#6b5c65",
            "summary": "不擅长与人交流的后藤一里，因为吉他与伙伴们相遇并加入结束乐队。"
        },
        {
            "id": "euphonium",
            "title": "吹响吧！上低音号",
            "subtitle": "Hibike! Euphonium",
            "meta": "2015 · TV · 13 集",
            "episode": "尚未开始",
            "progress": 0.0,
            "score": "8.4",
            "status": "未观看",
            "color": "#5c626d",
            "summary": "北宇治高中吹奏乐部向全国大赛迈进，少女们也在音乐中重新面对自己。"
        },
        {
            "id": "pluto",
            "title": "冥王",
            "subtitle": "PLUTO",
            "meta": "2023 · ONA · 8 集",
            "episode": "尚未关联媒体",
            "progress": 0.0,
            "score": "8.5",
            "status": "待关联",
            "color": "#545d63",
            "summary": "世界上最先进的机器人接连被破坏，刑警盖吉特追查案件背后的共同线索。"
        }
    ]

    readonly property var bangumiSubjects: [
        subjects[0], subjects[1], subjects[2], subjects[3], subjects[4],
        {
            "id": "girls-band-cry",
            "title": "GIRLS BAND CRY",
            "subtitle": "ガールズバンドクライ",
            "meta": "2024 · TV · 13 集",
            "episode": "远端条目",
            "progress": 0.0,
            "score": "8.0",
            "status": "未加入媒体库",
            "color": "#655b60",
            "summary": "五名少女组成乐队，在碰撞与演奏中寻找属于自己的声音。"
        }
    ]

    readonly property var episodes: [
        {"number": "01", "title": "冒险的终点", "duration": "24:10", "progress": 1.0, "source": "已关联 · 1080p", "linked": true},
        {"number": "02", "title": "魔法不是特别之物", "duration": "24:10", "progress": 1.0, "source": "已关联 · 1080p", "linked": true},
        {"number": "03", "title": "杀人魔法", "duration": "24:10", "progress": 1.0, "source": "已关联 · 2 个版本", "linked": true},
        {"number": "04", "title": "灵魂安眠之地", "duration": "24:10", "progress": 0.64, "source": "已关联 · 1080p", "linked": true},
        {"number": "05", "title": "死者的幻影", "duration": "24:10", "progress": 0.0, "source": "未关联", "linked": false},
        {"number": "06", "title": "村子的英雄", "duration": "24:10", "progress": 0.0, "source": "自动匹配待确认", "linked": false}
    ]
}
