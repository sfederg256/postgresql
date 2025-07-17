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

CREATE OR REPLACE FUNCTION scheduler.add_scheduled_task(
    task_name TEXT,
    task_sql_command TEXT,
    task_next_run_at TIMESTAMPTZ
)
RETURNS INT AS $$
DECLARE
    new_task_id INT;
BEGIN
    INSERT INTO scheduler.scheduled_tasks (
        name,
        sql_command,
        next_run_at
    )
    VALUES (
        task_name,
        task_sql_command,
        task_next_run_at
    )
    RETURNING id INTO new_task_id;

    RETURN new_task_id;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION scheduler.delete_scheduled_task(task_id INT)
RETURNS VOID AS $$
BEGIN
    DELETE FROM scheduler.scheduled_tasks
    WHERE id = task_id;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION scheduler.test_scheduler()
RETURNS TEXT AS $$
DECLARE
    task_id INT;
BEGIN
    INSERT INTO scheduler.scheduled_tasks (
        name,
        description,
        sql_command,
        next_run_at,
        repeat_interval
    ) VALUES (
        'Test task',
        'create test_table',
        'CREATE TABLE IF NOT EXISTS test_table (id serial primary key, created_at timestamp default now());',
        NOW() + INTERVAL '10 seconds',
        INTERVAL '1 minute'
    )
    RETURNING id INTO task_id;

    RETURN 'Added a new task with ID: ' || task_id;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION scheduler.wake_scheduler()
RETURNS TRIGGER AS $$
BEGIN
    PERFORM pg_notify('scheduler_channel', 'new task');
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_wake_scheduler
AFTER INSERT ON scheduler.scheduled_tasks
FOR EACH ROW EXECUTE FUNCTION scheduler.wake_scheduler();