/** anime-land Episode Provider runtime ABI v1. */

type EpisodeProviderOperation = "ping" | "search" | "resolve";
type EpisodeAssetKind = "video" | "subtitle" | "audio" | "danmaku";
type MediaStreamType = "unknown" | "progressive" | "hls" | "dash";

interface EpisodeQuery {
  subjectId: number;
  episodeId: number;
  subjectName: string;
  subjectAliases: string[];
  episodeName: string;
  episodeType: number;
  episodeNumber?: number;
}

interface ProviderSubjectMatch {
  key: string;
  title: string;
  cover?: string;
  detail?: string;
  episodeTitle?: string;
  sourceLine?: string;
  confidence?: number;
}

interface RemoteAsset {
  kind: EpisodeAssetKind;
  streamType: MediaStreamType;
  name: string;
  language?: string;
  mimeType?: string;
  data: Record<string, unknown>;
}

interface OnlinePlayable {
  key: string;
  name: string;
  match: ProviderSubjectMatch;
  assets: RemoteAsset[];
  expiresAt?: string;
}

interface ProviderContext {
  plugin: {id: string; version: string};
  provider: {id: string};
  mirror: {id: string; baseUrl: string};
  /** Immutable JSON snapshot for this operation. */
  config: Readonly<Record<string, unknown>>;
}

interface RequestDescriptor {
  /** Relative URLs resolve against context.mirror.baseUrl. */
  url: string;
  method?: "GET" | "POST" | "HEAD";
  headers?: Record<string, string>;
  body?: string;
}

interface HostResponse {
  url: string;
  status: number;
  headers: Record<string, string>;
  text: string;
}

interface RequestStep {
  type: "request";
  request: RequestDescriptor;
  /** Must be plain, bounded, JSON-round-trippable data. */
  state: Record<string, unknown>;
}

interface CompleteStep {
  type: "complete";
  value: unknown;
}

interface FailStep {
  type: "fail";
  error: {message: string; retryable?: boolean};
}

type ProviderStep = RequestStep | CompleteStep | FailStep;

interface EpisodeProviderV1 {
  id: string;
  name: string;
  icon?: string;
  begin(operation: EpisodeProviderOperation, input: unknown,
        context: ProviderContext): ProviderStep;
  resume(state: Record<string, unknown>, response: HostResponse,
         context: ProviderContext): ProviderStep;
}

interface HtmlQueryBridge {
  /**
   * Parses recoverable HTML with libxml2, selects rows using XPath 1.0, and
   * evaluates every field XPath relative to the selected row.
   */
  queryAll(html: string, rowXPath: string,
           fields: Record<string, string>): Array<Record<string, string>>;
}

interface AnimeLandEpisodeProviderHostV1 {
  readonly html: HtmlQueryBridge;
  registerEpisodeProvider(provider: EpisodeProviderV1): void;
  log(level: "info" | "warn" | "error", message: string): void;
}

declare const AnimeLand: AnimeLandEpisodeProviderHostV1;
