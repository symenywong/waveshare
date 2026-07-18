#include "aiqa_datetime.h"

#include <stdio.h>

static const char *weekday_zh(int weekday)
{
    static const char *const NAMES[] = {"日", "一", "二", "三", "四", "五", "六"};
    return weekday >= 0 && weekday < 7 ? NAMES[weekday] : NULL;
}

static bool formatted_value_fits(int written, size_t capacity)
{
    return written >= 0 && (size_t)written < capacity;
}

static bool is_leap_year(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static bool local_time_is_valid(const struct tm *local_time)
{
    static const int DAYS_PER_MONTH[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (local_time == NULL || local_time->tm_year < 70 || local_time->tm_year > 8099 ||
        local_time->tm_mon < 0 || local_time->tm_mon > 11 ||
        local_time->tm_hour < 0 || local_time->tm_hour > 23 ||
        local_time->tm_min < 0 || local_time->tm_min > 59 ||
        local_time->tm_sec < 0 || local_time->tm_sec > 60 ||
        local_time->tm_wday < 0 || local_time->tm_wday > 6) {
        return false;
    }
    int max_day = DAYS_PER_MONTH[local_time->tm_mon];
    const int year = local_time->tm_year + 1900;
    if (local_time->tm_mon == 1 && is_leap_year(year)) {
        max_day = 29;
    }
    return local_time->tm_mday >= 1 && local_time->tm_mday <= max_day;
}

bool aiqa_datetime_format_local_reply(
    aiqa_local_command_type_t command_type,
    const struct tm *local_time,
    char *out_reply,
    size_t out_reply_size)
{
    if (!local_time_is_valid(local_time) || out_reply == NULL || out_reply_size == 0U) {
        return false;
    }
    out_reply[0] = '\0';
    const char *weekday = weekday_zh(local_time->tm_wday);
    if (weekday == NULL) {
        return false;
    }

    int written = -1;
    switch (command_type) {
    case AIQA_LOCAL_COMMAND_DATE_QUERY:
        written = snprintf(out_reply,
                           out_reply_size,
                           "今天是%04d年%d月%d日，星期%s。",
                           local_time->tm_year + 1900,
                           local_time->tm_mon + 1,
                           local_time->tm_mday,
                           weekday);
        break;
    case AIQA_LOCAL_COMMAND_TIME_QUERY:
        written = snprintf(out_reply,
                           out_reply_size,
                           "现在是%02d:%02d。",
                           local_time->tm_hour,
                           local_time->tm_min);
        break;
    case AIQA_LOCAL_COMMAND_WEEKDAY_QUERY:
        written = snprintf(out_reply, out_reply_size, "今天是星期%s。", weekday);
        break;
    case AIQA_LOCAL_COMMAND_DATETIME_QUERY:
        written = snprintf(out_reply,
                           out_reply_size,
                           "现在是%04d年%d月%d日，星期%s，%02d:%02d。",
                           local_time->tm_year + 1900,
                           local_time->tm_mon + 1,
                           local_time->tm_mday,
                           weekday,
                           local_time->tm_hour,
                           local_time->tm_min);
        break;
    default:
        return false;
    }
    if (!formatted_value_fits(written, out_reply_size)) {
        out_reply[0] = '\0';
        return false;
    }
    return true;
}

bool aiqa_datetime_format_trusted_context(
    const struct tm *local_time,
    char *out_context,
    size_t out_context_size)
{
    if (!local_time_is_valid(local_time) || out_context == NULL || out_context_size == 0U) {
        return false;
    }
    out_context[0] = '\0';
    const int written = snprintf(
        out_context,
        out_context_size,
        "Trusted device local datetime: %04d-%02d-%02dT%02d:%02d:%02d+08:00; "
        "timezone=Asia/Shanghai; source=SNTP. Use this as authoritative for current "
        "and relative date/time questions.",
        local_time->tm_year + 1900,
        local_time->tm_mon + 1,
        local_time->tm_mday,
        local_time->tm_hour,
        local_time->tm_min,
        local_time->tm_sec);
    if (!formatted_value_fits(written, out_context_size)) {
        out_context[0] = '\0';
        return false;
    }
    return true;
}
