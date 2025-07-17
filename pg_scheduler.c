#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/proc.h"
#include "storage/shmem.h"
#include "storage/spin.h"
#include "executor/spi.h"
#include "commands/dbcommands.h"
#include "utils/guc.h"
#include "utils/elog.h"
#include "utils/memutils.h"
#include "utils/builtins.h"
#include "utils/snapmgr.h"
#include "access/xact.h"
#include "postmaster/bgworker.h"
#include "commands/async.h"

#define SCHEDULER_CHANNEL "scheduler_channel"

PG_MODULE_MAGIC;

static volatile sig_atomic_t got_sigterm = false;

void _PG_init(void);
PGDLLEXPORT void pg_scheduler_bgworker_main(Datum main_arg);

static void pg_scheduler_sigterm_handler(SIGNAL_ARGS);
static void handle_scheduled_tasks(bool *need_wait);
static void process_task(int task_id, char *sql, Datum repeat_interval_datum, bool has_repeat);

// Signal handler to catch SIGTERM
static void
pg_scheduler_sigterm_handler(SIGNAL_ARGS)
{
    int save_errno = errno;
    got_sigterm = true;
    SetLatch(MyLatch);
    errno = save_errno;
}

// Main background worker function
PGDLLEXPORT void
pg_scheduler_bgworker_main(Datum main_arg)
{
    pqsignal(SIGTERM, pg_scheduler_sigterm_handler);
    BackgroundWorkerUnblockSignals();
    BackgroundWorkerInitializeConnection("postgres", NULL, 0);

    elog(LOG, "pg_scheduler started");

    while (!got_sigterm)
    {
        bool need_wait = true;

        StartTransactionCommand();
        PushActiveSnapshot(GetTransactionSnapshot());

        if (SPI_connect() == SPI_OK_CONNECT)
        {
            elog(LOG, "pg_scheduler spi connection");

            handle_scheduled_tasks(&need_wait);

            SPI_finish();
        }
        else
        {
            elog(WARNING, "pg_scheduler: SPI_connect failed");
        }

        PopActiveSnapshot();
        CommitTransactionCommand();

        if (need_wait)
        {
            uint32 wait_result;

            /* Подписываемся на канал и ждём */
            WaitLatch(MyLatch,
                    WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH | WL_EXIT_ON_PM_DEATH,
                    5000L,   // 5 секунд
                    0);   

            if (wait_result & WL_LATCH_SET)
            {
                ResetLatch(MyLatch);
            }

            AcceptInvalidationMessages();
        }

        if (ProcDiePending)
            proc_exit(1);
        
    }

    proc_exit(0);
}

// Query and process all scheduled tasks
static void
handle_scheduled_tasks(bool *need_wait)
{
    int ret = SPI_execute(
        "SELECT id, sql_command, repeat_interval FROM scheduler.scheduled_tasks "
        "WHERE is_active AND status = 'pending' AND next_run_at <= now()",
        true, 0);

    if (ret == SPI_OK_SELECT && SPI_processed > 0)
    {
        for (uint64 i = 0; i < SPI_processed; i++)
        {
            bool isnull;
            int task_id = DatumGetInt32(SPI_getbinval(SPI_tuptable->vals[i],
                                                      SPI_tuptable->tupdesc,
                                                      1, &isnull));

            char *sql = TextDatumGetCString(SPI_getbinval(SPI_tuptable->vals[i],
                                                          SPI_tuptable->tupdesc,
                                                          2, &isnull));

            Datum repeat_interval_datum = SPI_getbinval(SPI_tuptable->vals[i],
                                                        SPI_tuptable->tupdesc,
                                                        3, &isnull);

            bool has_repeat = !isnull;

            process_task(task_id, sql, repeat_interval_datum, has_repeat);
            *need_wait = false;
        }
    }
}

// Process a single scheduled task
static void
process_task(int task_id, char *sql, Datum repeat_interval_datum, bool has_repeat)
{
    char update_running[128];
    snprintf(update_running, sizeof(update_running),
             "UPDATE scheduler.scheduled_tasks SET status = 'running' WHERE id = %d", task_id);
    SPI_execute(update_running, false, 0);

    PG_TRY();
    {
        int exec_result = SPI_execute(sql, false, 0);
        if (exec_result < 0)
            elog(WARNING, "pg_scheduler: task %d failed to execute SQL", task_id);

        char update_success[512];
        if (has_repeat)
        {
            snprintf(update_success, sizeof(update_success),
                     "UPDATE scheduler.scheduled_tasks "
                     "SET status = 'pending', last_run_at = now(), "
                     "next_run_at = next_run_at + repeat_interval "
                     "WHERE id = %d", task_id);
        }
        else
        {
            snprintf(update_success, sizeof(update_success),
                     "UPDATE scheduler.scheduled_tasks "
                     "SET status = 'success', last_run_at = now() "
                     "WHERE id = %d", task_id);
        }

        SPI_execute(update_success, false, 0);
    }
    PG_CATCH();
    {
        elog(WARNING, "pg_scheduler: exception during task %d execution", task_id);
        FlushErrorState();
    }
    PG_END_TRY();
}

// Register the background worker
void
_PG_init(void)
{
    BackgroundWorker worker;

    memset(&worker, 0, sizeof(worker));
    worker.bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
    worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
    worker.bgw_restart_time = 10;

    snprintf(worker.bgw_name, BGW_MAXLEN, "pg_scheduler");
    snprintf(worker.bgw_library_name, BGW_MAXLEN, "pg_scheduler");
    snprintf(worker.bgw_function_name, BGW_MAXLEN, "pg_scheduler_bgworker_main");

    worker.bgw_main_arg = (Datum) 0;
    worker.bgw_notify_pid = 0;

    RegisterBackgroundWorker(&worker);
}