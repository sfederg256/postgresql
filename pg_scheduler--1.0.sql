\echo Use "CREATE EXTENSION pg_scheduler" to load this file.\quit

CREATE TABLE scheduler.scheduled_tasks (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    description TEXT,
    sql_command TEXT NOT NULL,
    next_run_at TIMESTAMPTZ NOT NULL,
    repeat_interval INTERVAL,
    last_run_at TIMESTAMPTZ,
    status TEXT CHECK (status IN ('pending', 'running', 'success', 'failed')) DEFAULT 'pending',
    is_active BOOLEAN DEFAULT TRUE
);