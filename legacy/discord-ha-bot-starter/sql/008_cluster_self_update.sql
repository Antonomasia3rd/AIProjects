-- v0.5.1: cluster release transport + per-node version reporting.

ALTER TABLE computer_nodes
  ADD COLUMN IF NOT EXISTS current_version text;

CREATE TABLE IF NOT EXISTS cluster_update_packages (
  id bigserial PRIMARY KEY,
  filename text NOT NULL,
  sha256 text NOT NULL UNIQUE,
  size_bytes bigint NOT NULL,
  archive bytea NOT NULL,
  uploaded_by text NOT NULL,
  reason text NOT NULL,
  created_at timestamptz NOT NULL DEFAULT clock_timestamp()
);

CREATE INDEX IF NOT EXISTS cluster_update_packages_created_idx
  ON cluster_update_packages (created_at DESC);
