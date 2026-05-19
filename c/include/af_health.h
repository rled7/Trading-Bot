#ifndef AF_HEALTH_H
#define AF_HEALTH_H

#include <stdint.h>
#include "../include/af_broker.h"

typedef struct {
    int64_t  uptime_sec;       /* seconds since start */
    int      broker_connected; /* 1=yes 0=no */
    double   equity;           /* current equity */
    double   daily_pnl_pct;   /* from drawdown guard */
    int      guard_state;      /* AF_GUARD_GREEN/YELLOW/RED (0/1/2) */
    int      open_positions;   /* count */
    int      signals_processed;
    int      signals_rejected;
    char     status[16];       /* "OK", "WARN", "CRITICAL" */
} af_health_snapshot_t;

typedef struct {
    int64_t start_time; /* unix time of bot start */
} af_health_monitor_t;

void af_health_init(af_health_monitor_t *hm);
af_health_snapshot_t af_health_check(const af_health_monitor_t *hm,
                                      const af_broker_t *broker,
                                      double equity,
                                      double daily_pnl_pct,
                                      int guard_state,
                                      int signals_processed,
                                      int signals_rejected);
void af_health_print_json(const af_health_snapshot_t *snap);

#endif /* AF_HEALTH_H */
