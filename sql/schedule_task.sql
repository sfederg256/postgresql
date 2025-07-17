CREATE OR REPLACE FUNCTION test_notice() RETURNS void AS $$
BEGIN
    RAISE NOTICE 'Hello from task!';
END;
$$ LANGUAGE plpgsql;

INSERT INTO scheduler.scheduled_tasks (name, sql_command, next_run_at, repeat_interval)
VALUES ('Test Task', 'SELECT test_notice();', now(), '1 minute');

SELECT pg_sleep(5);

SELECT status, last_run_at FROM scheduler.scheduled_tasks WHERE name = 'Test Task';