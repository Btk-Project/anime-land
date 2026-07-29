(function () {
  "use strict";

  const providerId = "yhdmmm";
  const searchItemXPath =
    "//div[contains(concat(' ', normalize-space(@class), ' '), ' index-tj ')]" +
    "//ul[contains(concat(' ', normalize-space(@class), ' '), ' main ')]" +
    "/li/a[contains(concat(' ', normalize-space(@class), ' '), ' li-hv ')]";
  const sourceTabXPath = "//li[starts-with(@id, 'tab')]";
  const episodeLinkXPath =
    "//div[starts-with(@id, 'stab')]//a[contains(@href, '/4kplay/')]";
  const playerScriptXPath = "//script[contains(., 'var player_data=')]";

  function request(url, method, state, headers, body) {
    return {
      type: "request",
      request: {
        url: url,
        method: method || "GET",
        headers: headers || {},
        body: body || ""
      },
      state: state
    };
  }

  function complete(value) {
    return {type: "complete", value: value};
  }

  function fail(message, retryable) {
    return {
      type: "fail",
      error: {message: String(message), retryable: Boolean(retryable)}
    };
  }

  function normalizeTitle(value) {
    return String(value || "")
      .toLocaleLowerCase()
      .replace(/[\s\u3000·・:：,，.。!！?？~～_\-—()（）\[\]【】]/g, "");
  }

  function numberInEpisodeName(value) {
    const match = String(value || "").match(/(\d+(?:\.\d+)?)/);
    return match ? Number(match[1]) : null;
  }

  function episodeMatches(title, query) {
    const actualNumber = numberInEpisodeName(title);
    if (typeof query.episodeNumber === "number" && actualNumber !== null) {
      return Math.abs(actualNumber - query.episodeNumber) < 0.0001;
    }
    const expected = normalizeTitle(query.episodeName);
    const actual = normalizeTitle(title);
    return expected.length > 0 &&
      (actual === expected || actual.indexOf(expected) >= 0 || expected.indexOf(actual) >= 0);
  }

  function titleConfidence(actual, query) {
    const normalizedActual = normalizeTitle(actual);
    const names = [query.subjectName].concat(query.subjectAliases || [])
      .map(normalizeTitle)
      .filter(Boolean);
    if (names.some(function (name) { return name === normalizedActual; })) {
      return 1.0;
    }
    if (names.some(function (name) {
      return name.indexOf(normalizedActual) >= 0 || normalizedActual.indexOf(name) >= 0;
    })) {
      return 0.75;
    }
    return 0.35;
  }

  function parseSearchPage(text, query, maxCandidates) {
    const rows = AnimeLand.html.queryAll(text, searchItemXPath, {
      href: "string(@href)",
      title: "normalize-space(string(@title))",
      cover: "string(.//img/@data-original)",
      note: "normalize-space(string(.//p[contains(concat(' ', normalize-space(@class), ' '), ' bz ')]))"
    });
    return rows
      .filter(function (row) {
        return /^\/4kvideo\/\d+\.html$/.test(String(row.href || ""));
      })
      .slice(0, maxCandidates)
      .map(function (row) {
        return {
          key: String(row.href),
          href: String(row.href),
          title: String(row.title || ""),
          cover: String(row.cover || ""),
          note: String(row.note || ""),
          confidence: titleConfidence(row.title, query)
        };
      });
  }

  function parseDetailPage(text, candidate, query, preferredLines, mirrorId) {
    const tabs = AnimeLand.html.queryAll(text, sourceTabXPath, {
      id: "string(@id)",
      name: "normalize-space(string(.))"
    });
    const sourceNames = {};
    tabs.forEach(function (tab) {
      if (/^tab\d+$/.test(String(tab.id || ""))) {
        sourceNames[String(tab.id).slice(3)] = String(tab.name || "");
      }
    });

    const links = AnimeLand.html.queryAll(text, episodeLinkXPath, {
      href: "string(@href)",
      title: "normalize-space(string(@title))",
      text: "normalize-space(string(.))",
      sourceContainer: "string(ancestor::div[starts-with(@id, 'stab')][1]/@id)"
    });
    const preferred = Array.isArray(preferredLines) ? preferredLines : [];
    const results = [];
    links.forEach(function (link) {
      const href = String(link.href || "");
      const episodeTitle = String(link.title || link.text || "");
      const sourceId = String(link.sourceContainer || "").replace(/^stab/, "");
      const sourceLine = sourceNames[sourceId] || ("线路 " + sourceId);
      if (!/^\/4kplay\/\d+-\d+-\d+\.html$/.test(href) ||
          !episodeMatches(episodeTitle, query) ||
          (preferred.length > 0 && preferred.indexOf(sourceLine) < 0)) {
        return;
      }
      results.push({
        key: candidate.key + "|" + sourceId + "|" + href,
        name: candidate.title + " · " + episodeTitle + " · " + sourceLine,
        match: {
          key: candidate.key,
          title: candidate.title,
          cover: candidate.cover,
          detail: candidate.note,
          episodeTitle: episodeTitle,
          sourceLine: sourceLine,
          confidence: candidate.confidence
        },
        assets: [{
          kind: "video",
          streamType: "unknown",
          name: sourceLine,
          data: {
            continuation: {
              playPath: href,
              sourceLine: sourceLine,
              mirrorId: mirrorId
            }
          }
        }]
      });
    });
    return results;
  }

  function extractJsonObjectAfter(text, marker) {
    const markerIndex = text.indexOf(marker);
    if (markerIndex < 0) {
      return null;
    }
    const start = text.indexOf("{", markerIndex + marker.length);
    if (start < 0) {
      return null;
    }
    let depth = 0;
    let quoted = false;
    let escaped = false;
    for (let index = start; index < text.length; ++index) {
      const character = text[index];
      if (quoted) {
        if (escaped) {
          escaped = false;
        }
        else if (character === "\\") {
          escaped = true;
        }
        else if (character === "\"") {
          quoted = false;
        }
        continue;
      }
      if (character === "\"") {
        quoted = true;
      }
      else if (character === "{") {
        depth += 1;
      }
      else if (character === "}") {
        depth -= 1;
        if (depth === 0) {
          return text.slice(start, index + 1);
        }
      }
    }
    return null;
  }

  function parsePlayerData(text) {
    const scripts = AnimeLand.html.queryAll(text, playerScriptXPath, {
      text: "string(.)"
    });
    for (let index = 0; index < scripts.length; ++index) {
      const encoded = extractJsonObjectAfter(String(scripts[index].text || ""),
                                             "var player_data");
      if (!encoded) {
        continue;
      }
      const data = JSON.parse(encoded);
      if (Number(data.encrypt || 0) !== 0) {
        throw new Error("暂不支持加密的 player_data.url");
      }
      const url = String(data.url || "");
      if (!/^https:\/\//i.test(url)) {
        throw new Error("player_data 没有可用的 HTTPS URL");
      }
      const source = String(data.from || "").toLocaleLowerCase();
      const path = url.split(/[?#]/, 1)[0].toLocaleLowerCase();
      return {
        url: url,
        streamType: source.indexOf("m3u8") >= 0 || /\.m3u8$/.test(path)
          ? "hls"
          : "progressive",
        source: String(data.from || "")
      };
    }
    throw new Error("播放页没有 player_data");
  }

  function nextDetail(state) {
    if (state.index >= state.candidates.length) {
      return complete(state.results);
    }
    return request(state.candidates[state.index].href, "GET", state, {
      Accept: "text/html,application/xhtml+xml"
    });
  }

  function begin(operation, input, context) {
    if (operation === "ping") {
      return request("/", "HEAD", {phase: "ping"}, {});
    }
    if (operation === "search") {
      const searchName = input.subjectName ||
        (input.subjectAliases && input.subjectAliases[0]) || "";
      const body = "wd=" + encodeURIComponent(String(searchName));
      return request("/vodsearch.html", "POST", {
        phase: "search",
        query: input
      }, {
        Accept: "text/html,application/xhtml+xml",
        "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8"
      }, body);
    }
    if (operation === "resolve") {
      const video = input && input.assets && input.assets[0];
      const continuation = video && video.data && video.data.continuation;
      const playPath = continuation && String(continuation.playPath || "");
      const mirrorId = continuation && String(continuation.mirrorId || "");
      if (!/^\/4kplay\/\d+-\d+-\d+\.html$/.test(playPath)) {
        return fail("在线结果缺少有效播放页 continuation", false);
      }
      if (!mirrorId || mirrorId !== context.mirror.id) {
        return fail("生成在线结果的镜像与 resolve 上下文不一致", false);
      }
      return request(playPath, "GET", {
        phase: "resolve",
        playable: input
      }, {
        Accept: "text/html,application/xhtml+xml"
      });
    }
    return fail("不支持的 Provider 操作", false);
  }

  function resume(state, response, context) {
    if (state.phase === "ping") {
      return complete({
        reachable: response.status >= 200 && response.status < 400,
        mirrorId: context.mirror.id,
        detail: "HTTP " + response.status
      });
    }
    if (response.status < 200 || response.status >= 400) {
      return fail("站点返回 HTTP " + response.status, response.status >= 500);
    }
    if (state.phase === "search") {
      const configured = Number(context.config.maxCandidates || 2);
      const maxCandidates = Math.max(1, Math.min(5, configured));
      const candidates = parseSearchPage(response.text, state.query, maxCandidates);
      if (candidates.length === 0) {
        return complete([]);
      }
      return nextDetail({
        phase: "detail",
        query: state.query,
        candidates: candidates,
        index: 0,
        results: []
      });
    }
    if (state.phase === "detail") {
      const candidate = state.candidates[state.index];
      const found = parseDetailPage(response.text, candidate, state.query,
                                    context.config.preferredLines,
                                    context.mirror.id);
      state.results = state.results.concat(found);
      state.index += 1;
      return nextDetail(state);
    }
    if (state.phase === "resolve") {
      try {
        const player = parsePlayerData(response.text);
        const playable = state.playable;
        const video = playable.assets[0];
        video.streamType = player.streamType;
        video.mimeType = player.streamType === "hls"
          ? "application/vnd.apple.mpegurl"
          : "video/*";
        video.data = {
          url: player.url,
          source: player.source,
          continuation: video.data.continuation
        };
        return complete(playable);
      }
      catch (error) {
        return fail(error && error.message ? error.message : String(error), false);
      }
    }
    return fail("无法识别 Provider continuation phase", false);
  }

  AnimeLand.registerEpisodeProvider({
    id: providerId,
    name: "樱花动漫",
    icon: "",
    begin: begin,
    resume: resume
  });
})();
