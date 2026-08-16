-- Qbrain schema v1
-- n38 (SQL census): this file is the SQLITE canonical schema; the PG
-- equivalent lives in N38-A's kPgSchemaSql (identity columns, timestamptz,
-- bytea, tsvector+GIN, COLLATE "C"). Per-expression COLLATE BINARY was
-- removed from shared query SQL and is pinned here at column level (BINARY
-- is SQLite's default collation for TEXT, so this is semantics-preserving
-- documentation of the ordering contract). AUTOINCREMENT and
-- DEFAULT (datetime('now')) are kept: check_schema_integrity and the N17
-- tests assert the literal DDL text; PG never executes this file.
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS schema_version (
  version INTEGER NOT NULL PRIMARY KEY,
  applied_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS sources (
  id TEXT PRIMARY KEY,
  name TEXT,
  local_path TEXT,
  config_json TEXT NOT NULL DEFAULT '{}',
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  last_sync_at TEXT
);

CREATE TABLE IF NOT EXISTS pages (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  source_id TEXT NOT NULL DEFAULT 'default',
  slug TEXT NOT NULL COLLATE BINARY,
  type TEXT NOT NULL DEFAULT 'note' COLLATE BINARY,
  title TEXT NOT NULL DEFAULT '',
  body TEXT NOT NULL DEFAULT '',
  frontmatter_json TEXT NOT NULL DEFAULT '{}',
  content_hash TEXT,
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  updated_at TEXT NOT NULL DEFAULT (datetime('now')),
  deleted_at TEXT,
  UNIQUE(source_id, slug),
  FOREIGN KEY(source_id) REFERENCES sources(id)
);

CREATE INDEX IF NOT EXISTS idx_pages_type ON pages(type);
CREATE INDEX IF NOT EXISTS idx_pages_updated ON pages(updated_at DESC);
CREATE INDEX IF NOT EXISTS idx_pages_source ON pages(source_id);

CREATE TABLE IF NOT EXISTS content_chunks (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  page_id INTEGER NOT NULL,
  chunk_index INTEGER NOT NULL,
  text TEXT NOT NULL,
  embedding BLOB,
  dim INTEGER,
  model TEXT,
  UNIQUE(page_id, chunk_index),
  FOREIGN KEY(page_id) REFERENCES pages(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_chunks_page ON content_chunks(page_id);

CREATE TABLE IF NOT EXISTS links (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  source_id TEXT NOT NULL DEFAULT 'default',
  from_slug TEXT NOT NULL COLLATE BINARY,
  to_slug TEXT NOT NULL COLLATE BINARY,
  link_type TEXT NOT NULL DEFAULT 'related',
  context TEXT NOT NULL DEFAULT '',
  link_source TEXT NOT NULL DEFAULT 'markdown' COLLATE BINARY,
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  UNIQUE(source_id, from_slug, to_slug, link_type, link_source)
);

CREATE INDEX IF NOT EXISTS idx_links_from ON links(source_id, from_slug);
CREATE INDEX IF NOT EXISTS idx_links_to ON links(source_id, to_slug);

CREATE TABLE IF NOT EXISTS tags (
  page_id INTEGER NOT NULL,
  tag TEXT NOT NULL,
  PRIMARY KEY(page_id, tag),
  FOREIGN KEY(page_id) REFERENCES pages(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS config (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS jobs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  queue TEXT NOT NULL DEFAULT 'default',
  type TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'waiting',
  payload_json TEXT NOT NULL DEFAULT '{}',
  result_json TEXT,
  priority INTEGER NOT NULL DEFAULT 100,
  attempts INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  updated_at TEXT NOT NULL DEFAULT (datetime('now')),
  lock_until TEXT,
  lock_token TEXT,
  error_text TEXT
);

CREATE INDEX IF NOT EXISTS idx_jobs_claim ON jobs(queue, status, priority, created_at);

CREATE VIRTUAL TABLE IF NOT EXISTS pages_fts USING fts5(
  slug,
  title,
  body,
  content='pages',
  content_rowid='id',
  tokenize='unicode61'
);

CREATE TRIGGER IF NOT EXISTS pages_ai AFTER INSERT ON pages BEGIN
  INSERT INTO pages_fts(rowid, slug, title, body)
  VALUES (new.id, new.slug, new.title, new.body);
END;

CREATE TRIGGER IF NOT EXISTS pages_ad AFTER DELETE ON pages BEGIN
  INSERT INTO pages_fts(pages_fts, rowid, slug, title, body)
  VALUES ('delete', old.id, old.slug, old.title, old.body);
END;

CREATE TRIGGER IF NOT EXISTS pages_au AFTER UPDATE ON pages BEGIN
  INSERT INTO pages_fts(pages_fts, rowid, slug, title, body)
  VALUES ('delete', old.id, old.slug, old.title, old.body);
  INSERT INTO pages_fts(rowid, slug, title, body)
  VALUES (new.id, new.slug, new.title, new.body);
END;

-- n38: INSERT OR IGNORE -> ON CONFLICT DO NOTHING (SQLite 3.46+ / PG both parse it)
INSERT INTO sources(id, name) VALUES ('default', 'Default Source') ON CONFLICT DO NOTHING;
INSERT INTO schema_version(version) VALUES (1) ON CONFLICT DO NOTHING;
