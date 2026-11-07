// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
//
// e906_oneway_reader.c - one-way time from an E906 event to the data in a Linux thread.
// @impl [SPEC-E906-BENCH-07] apps/e906_bench/SPECIFICATION.md
//
//   e906_oneway_reader --dev /dev/rpmsgN [--count 60000] [--size 32] [--rt] [--cpu 2]
//                      [--pings 2000] [--timeout-s S]
//   e906_oneway_reader --clock-pair
//
// Normally run by bench_e906_runtime.py --oneway, which creates the endpoint. Prints JSON.
// Exit 0 ok, 1 a check failed, 2 could not run.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define TICKS_PER_US 24
#define HIST_US 20000
#define MAX_MSG 512
#define SAMPLE_TAG 0x01
#define SAMPLE_LEN 32
#define REPORT_MAX 8192

struct hist {
    int64_t min, max, sum;
    uint32_t n, over;
    uint32_t bucket[HIST_US];
};

struct bounds {
    int64_t lo, hi, min_rtt;
    uint32_t n;
};

static struct hist g_sent, g_event;
static char g_report[REPORT_MAX];
static size_t g_report_len;

// CNTVCT_EL0 on the board; the same scale from the system clock in the off-board test.
static inline int64_t now_ticks(void)
{
#if defined(__aarch64__)
    uint64_t v;
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v));
    return (int64_t)v;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (int64_t)ts.tv_sec * 24000000 + (int64_t)ts.tv_nsec * TICKS_PER_US / 1000;
#endif
}

static int64_t ticks_to_ns(int64_t t) { return t * 1000 / TICKS_PER_US; }

static int64_t get_i64(const unsigned char *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i)
        v = (v << 8) | p[i];
    return (int64_t)v;
}

static void hist_init(struct hist *h)
{
    memset(h, 0, sizeof(*h));
    h->min = INT64_MAX;
}

static void hist_add(struct hist *h, int64_t ticks)
{
    if (ticks < 0)
        ticks = 0; // inside the clock uncertainty: the caller counted anything beyond it
    if (ticks < h->min)
        h->min = ticks;
    if (ticks > h->max)
        h->max = ticks;
    h->sum += ticks;
    ++h->n;
    const int64_t us = ticks / TICKS_PER_US;
    if (us < HIST_US)
        ++h->bucket[us];
    else
        ++h->over;
}

static uint32_t hist_pct(const struct hist *h, uint32_t per_mille)
{
    const uint64_t need = ((uint64_t)h->n * per_mille + 999) / 1000;
    uint64_t acc = 0;
    for (uint32_t b = 0; b < HIST_US; ++b) {
        acc += h->bucket[b];
        if (acc >= need)
            return b + 1;
    }
    return HIST_US;
}

static void print_hist(const char *name, const struct hist *h)
{
    printf(" \"%s\": {\"n\": %u, \"min_ns\": %" PRId64 ", \"avg_ns\": %" PRId64 ", \"max_ns\": %" PRId64
           ", \"p50_us\": %u, \"p99_us\": %u, \"p999_us\": %u, \"over_20ms\": %u},\n",
           name, h->n, h->n ? ticks_to_ns(h->min) : 0, h->n ? ticks_to_ns(h->sum / h->n) : 0, ticks_to_ns(h->max),
           hist_pct(h, 500), hist_pct(h, 990), hist_pct(h, 999), h->over);
}

static void on_alarm(int sig) { (void)sig; } // only so a blocked read() returns EINTR

// One probe per loop; the reply is matched by its length and tag, anything else is an error.
static int probe_clock(int fd, uint32_t pings, struct bounds *b)
{
    b->lo = INT64_MIN;
    b->hi = INT64_MAX;
    b->min_rtt = INT64_MAX;
    b->n = 0;
    unsigned char buf[MAX_MSG];
    for (uint32_t i = 0; i < pings; ++i) {
        const int64_t t0 = now_ticks();
        if (write(fd, "T", 1) != 1)
            return -1;
        const ssize_t n = read(fd, buf, sizeof(buf));
        const int64_t t1 = now_ticks();
        if (n != 9 || buf[0] != 'T')
            return -1;
        const int64_t e = get_i64(buf + 1);
        if (e - t1 > b->lo)
            b->lo = e - t1;
        if (e - t0 < b->hi)
            b->hi = e - t0;
        if (t1 - t0 < b->min_rtt)
            b->min_rtt = t1 - t0;
        ++b->n;
    }
    return 0;
}

static int64_t abs64(int64_t v) { return v < 0 ? -v : v; }

// Largest distance from the offset in use to a bound of this probe.
static int64_t reach(const struct bounds *b, int64_t d)
{
    const int64_t a = abs64(b->lo - d), c = abs64(b->hi - d);
    return a > c ? a : c;
}

static int report_done(void)
{
    return strncmp(g_report, "end=", 4) == 0 || strstr(g_report, "\nend=") != NULL;
}

// The firmware's key=value lines as JSON members; returns the value of `want`, or -1.
static int64_t report_value(const char *want)
{
    const size_t klen = strlen(want);
    for (const char *p = g_report; p && *p;) {
        if (strncmp(p, want, klen) == 0 && p[klen] == '=')
            return strtoll(p + klen + 1, NULL, 10);
        p = strchr(p, '\n');
        p = p ? p + 1 : NULL;
    }
    return -1;
}

static void print_report(void)
{
    printf(" \"report\": {");
    const char *sep = "";
    for (const char *p = g_report; p && *p;) {
        const char *eq = strchr(p, '=');
        const char *nl = strchr(p, '\n');
        if (!eq || !nl || eq > nl)
            break;
        const char *v = eq + 1;
        int digits = v < nl;
        for (const char *q = v; q < nl; ++q)
            digits = digits && *q >= '0' && *q <= '9';
        printf("%s\"%.*s\": %s%.*s%s", sep, (int)(eq - p), p, digits ? "" : "\"", (int)(nl - v), v, digits ? "" : "\"");
        sep = ", ";
        p = nl + 1;
    }
    printf("},\n");
}

static int print_clock_pair(void)
{
    int64_t best = INT64_MAX, offset = 0;
    for (int i = 0; i < 200; ++i) {
        struct timespec ts;
        const int64_t t0 = now_ticks();
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        const int64_t t1 = now_ticks();
        const int64_t mono = (int64_t)ts.tv_sec * 24000000 + (int64_t)ts.tv_nsec * TICKS_PER_US / 1000;
        if (t1 - t0 < best) {
            best = t1 - t0;
            offset = (t0 + t1) / 2 - mono;
        }
    }
    printf("{\"cntvct_minus_monoraw_ticks\": %" PRId64 ", \"width_ticks\": %" PRId64 "}\n", offset, best);
    return 0;
}

int main(int argc, char **argv)
{
    const char *dev = NULL;
    int fd = -1, rt = 0, cpu = 2;
    uint32_t count = 60000, size = 32, pings = 2000, timeout_s = 0;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--clock-pair"))
            return print_clock_pair();
        if (!strcmp(a, "--rt"))
            rt = 1;
        else if (!strcmp(a, "--dev") && v)
            dev = argv[++i];
        else if (!strcmp(a, "--fd") && v)
            fd = atoi(argv[++i]);
        else if (!strcmp(a, "--count") && v)
            count = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--size") && v)
            size = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--cpu") && v)
            cpu = atoi(argv[++i]);
        else if (!strcmp(a, "--pings") && v)
            pings = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--timeout-s") && v)
            timeout_s = (uint32_t)strtoul(argv[++i], NULL, 0);
        else {
            fprintf(stderr, "usage: see the top of e906_oneway_reader.c\n");
            return 2;
        }
    }
    if (dev)
        fd = open(dev, O_RDWR); // blocking on purpose: the read() wake-up is what is measured
    if (fd < 0 || pings == 0) {
        fprintf(stderr, "could not run: no device (%s)\n", strerror(errno));
        return 2;
    }
    if (timeout_s == 0)
        timeout_s = count / 1000 + 15;

    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        fprintf(stderr, "could not run: pin to CPU %d: %s\n", cpu, strerror(errno));
        return 2;
    }
    if (rt) {
        const struct sched_param sp = {.sched_priority = 95};
        if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0 || mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
            fprintf(stderr, "could not run: SCHED_FIFO 95 / mlockall: %s\n", strerror(errno));
            return 2;
        }
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_alarm; // no SA_RESTART
    sigaction(SIGALRM, &sa, NULL);

    const char *error = NULL;
    struct bounds before = {0}, after = {0};
    const char *kind = "unknown";
    int64_t d = 0, uncertainty = 0;
    uint32_t received = 0, out_of_order = 0, negative = 0, not_a_sample = 0;
    int64_t last_seq = -1;
    hist_init(&g_sent);
    hist_init(&g_event);

    alarm(timeout_s);
    if (probe_clock(fd, pings, &before) != 0) {
        error = "clock probe failed or timed out";
    } else if (before.lo > before.hi) {
        kind = "inconsistent";
        error = "clock bounds cross: not a fixed offset";
    } else if (before.lo <= 0 && before.hi >= 0) {
        kind = "shared";
    } else {
        kind = "calibrated";
        d = before.lo + (before.hi - before.lo) / 2;
    }
    if (!error)
        uncertainty = reach(&before, d);

    if (!error) {
        unsigned char req[8] = {'O'};
        for (int i = 0; i < 4; ++i)
            req[1 + i] = (unsigned char)(count >> (8 * i));
        req[6] = (unsigned char)size;
        req[7] = (unsigned char)(size >> 8);
        alarm(timeout_s);
        if (write(fd, req, sizeof(req)) != (ssize_t)sizeof(req))
            error = "request failed";
        unsigned char buf[MAX_MSG];
        while (!error && !report_done()) {
            const ssize_t n = read(fd, buf, sizeof(buf));
            const int64_t t = now_ticks(); // the data is in this thread
            if (n <= 0) {
                error = n < 0 && errno == EINTR ? "timed out before the report" : "read failed";
            } else if (buf[0] == SAMPLE_TAG && n >= SAMPLE_LEN) {
                const int64_t seq = (int64_t)(buf[4] | (buf[5] << 8) | (buf[6] << 16) | ((uint32_t)buf[7] << 24));
                const int64_t sent = t - (get_i64(buf + 24) - d);
                const int64_t event = t - (get_i64(buf + 8) - d);
                if (seq <= last_seq)
                    ++out_of_order;
                last_seq = seq;
                if (sent < -uncertainty || event < -uncertainty)
                    ++negative;
                hist_add(&g_sent, sent);
                hist_add(&g_event, event);
                ++received;
            } else if (buf[0] == SAMPLE_TAG || g_report_len + (size_t)n >= REPORT_MAX) {
                ++not_a_sample;
            } else {
                memcpy(g_report + g_report_len, buf, (size_t)n);
                g_report_len += (size_t)n;
                g_report[g_report_len] = '\0';
            }
        }
    }

    if (!error) {
        alarm(timeout_s);
        if (probe_clock(fd, pings, &after) != 0) {
            error = "second clock probe failed or timed out";
        } else if (after.lo > after.hi) {
            kind = "inconsistent";
            error = "second clock probe: bounds cross";
        } else {
            const int64_t r = reach(&after, d);
            if (r > uncertainty)
                uncertainty = r;
            if (!strcmp(kind, "shared") && (after.lo > 0 || after.hi < 0))
                error = "first clock probe said shared, second contradicts it";
        }
    }
    alarm(0);

    const int64_t sent_fw = report_value("ow_sent");
    const int64_t tx_fail = report_value("ow_tx_fail");
    const int64_t missed = report_value("ow_missed");
    const int64_t dropped = report_value("dropped_resumes");
    const int64_t lost = sent_fw >= 0 ? sent_fw - (int64_t)received : 0;
    if (!error && (sent_fw < 0 || tx_fail < 0 || missed < 0))
        error = "report incomplete";
    if (!error && sent_fw + tx_fail + missed != (int64_t)count)
        error = "firmware counts do not add up";
    if (!error && lost != 0)
        error = "samples lost";
    if (!error && out_of_order)
        error = "samples out of order";
    if (!error && negative)
        error = "negative time: clock wrong";
    if (!error && not_a_sample)
        error = "unexpected message";
    if (!error && dropped != 0)
        error = "dropped resumes";

    printf("{\n \"reader\": \"c\",\n \"sched\": \"%s\",\n \"cpu\": %d,\n \"count\": %u,\n \"size\": %u,\n",
           rt ? "fifo95" : "other", cpu, count, size);
    printf(" \"clock\": {\"kind\": \"%s\", \"offset_ticks\": %" PRId64 ", \"uncertainty_ns\": %" PRId64
           ", \"before\": {\"lo\": %" PRId64 ", \"hi\": %" PRId64 ", \"min_rtt\": %" PRId64
           ", \"n\": %u}, \"after\": {\"lo\": %" PRId64 ", \"hi\": %" PRId64 ", \"min_rtt\": %" PRId64 ", \"n\": %u}},\n",
           kind, d, ticks_to_ns(uncertainty), before.lo, before.hi, before.min_rtt, before.n, after.lo, after.hi,
           after.min_rtt, after.n);
    print_hist("sent_to_read", &g_sent);
    print_hist("event_to_read", &g_event);
    print_report();
    printf(" \"received\": %u,\n \"lost\": %" PRId64 ",\n \"out_of_order\": %u,\n \"negative\": %u", received, lost,
           out_of_order, negative);
    if (error)
        printf(",\n \"error\": \"%s\"", error);
    printf("\n}\n");
    return error ? 1 : 0;
}
