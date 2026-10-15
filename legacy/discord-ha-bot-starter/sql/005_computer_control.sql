CREATE TABLE IF NOT EXISTS computer_nodes (
  node_id text PRIMARY KEY,
  hostname text NOT NULL,
  platform text NOT NULL,
  os_release text NOT NULL,
  arch text NOT NULL,
  process_id integer NOT NULL,
  started_at timestamptz NOT NULL DEFAULT clock_timestamp(),
  last_seen timestamptz NOT NULL DEFAULT clock_timestamp(),
  metadata jsonb NOT NULL DEFAULT '{}'::jsonb
);

CREATE INDEX IF NOT EXISTS computer_nodes_last_seen_idx
  ON computer_nodes (last_seen DESC);

CREATE TABLE IF NOT EXISTS computer_commands (
  id bigserial PRIMARY KEY,
  guild_id text NOT NULL,
  requested_by text NOT NULL,
  target_node_id text NOT NULL,
  action text NOT NULL,
  payload jsonb NOT NULL DEFAULT '{}'::jsonb,
  reason text NOT NULL,
  status text NOT NULL DEFAULT 'pending'
    CHECK (status IN ('pending', 'claimed', 'completed', 'failed', 'expired')),
  requested_at timestamptz NOT NULL DEFAULT clock_timestamp(),
  claimed_by text,
  claimed_at timestamptz,
  completed_at timestamptz,
  expires_at timestamptz NOT NULL DEFAULT (clock_timestamp() + interval '2 minutes'),
  result jsonb
);

CREATE INDEX IF NOT EXISTS computer_commands_target_pending_idx
  ON computer_commands (target_node_id, status, requested_at);
