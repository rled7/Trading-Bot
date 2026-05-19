#include "../include/af_health.h"
#include "../include/af_broker.h"
#include "../include/af_types.h"

#include <time.h>
#include <stdio.h>
#include <string.h>

void af_health_init(af_health_monitor_t *hm)
{
    hm->start_time = (int64_t)time(NULL);
}

af_health_snapshot_t af_health_check(const af_health_monitor_t *hm,
                                      const af_broker_t *broker,
                                      double equity,
                                      double daily_pnl_pct,
                                      int guard_state,
                                      int signals_processed,
                                      int signals_rejected)
{
    af_health_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));

    int64_t now = (int64_t)time(NULL);
    snap.uptime_sec       = now - hm->start_time;
    snap.broker_connected = broker ? broker->is_connected(broker) : 0;
    snap.equity           = equity;
    snap.daily_pnl_pct    = daily_pnl_pct;
    snap.guard_state      = guard_state;
    snap.signals_processed = signals_processed;
    snap.signals_rejected  = signals_rejected;

    /* Count open positions */
    if (broker && snap.broker_connected) {
        snap.open_positions = broker->get_positions(broker, NULL, 0);
    } else {
        snap.open_positions = 0;
    }

    /* Determine status string
     * guard_state: 0=GREEN, 1=YELLOW, 2=RED */
    if (guard_state == 2) {
        strncpy(snap.status, "CRITICAL", sizeof(snap.status) - 1);
    } else if (guard_state == 1 || daily_pnl_pct <= -5.0) {
        strncpy(snap.status, "WARN", sizeof(snap.status) - 1);
    } else {
        strncpy(snap.status, "OK", sizeof(snap.status) - 1);
    }
    snap.status[sizeof(snap.status) - 1] = '\0';

    return snap;
}

void af_health_print_json(const af_health_snapshot_t *snap)
{
    printf("{"
           "\"uptime_sec\":%lld,"
           "\"broker_connected\":%d,"
           "\"equity\":%.2f,"
           "\"daily_pnl_pct\":%.4f,"
           "\"guard_state\":%d,"
           "\"open_positions\":%d,"
           "\"signals_processed\":%d,"
           "\"signals_rejected\":%d,"
           "\"status\":\"%s\""
           "}\n",
           (long long)snap->uptime_sec,
           snap->broker_connected,
           snap->equity,
           snap->daily_pnl_pct,
           snap->guard_state,
           snap->open_positions,
           snap->signals_processed,
           snap->signals_rejected,
           snap->status);
    fflush(stdout);
}
