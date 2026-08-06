declare module 'virtual:reference-set-archive' {
  export interface ReferenceSetArchiveFile {
    path: string;
    base64: string;
    sha256: string;
  }

  export interface ReferenceSetArchiveGroup {
    name: string;
    count: number;
    atlasRow: number;
    atlasRows: number;
  }

  export interface ReferenceSetArchivePayload {
    files: readonly ReferenceSetArchiveFile[];
    groups: readonly ReferenceSetArchiveGroup[];
    atlas: {
      width: number;
      height: number;
      columns: number;
      base64: string;
    };
  }

  export const REFERENCE_SET_ARCHIVE: ReferenceSetArchivePayload;
}

declare module 'virtual:comparison-fixtures' {
  export const COMPARISON_FIXTURES: Readonly<Record<string, readonly { key: string; base64: string }[]>>;
}

declare module '*.css';
