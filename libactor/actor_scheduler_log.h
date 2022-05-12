#ifndef __ACTOR_SCHEDULER_LOG_H__
#define __ACTOR_SCHEDULER_LOG_H__

#include "actor_scheduler.h"

#include <time.h>
#include <sys/time.h>

#define actor_log(level, fmt, args...) \
    do {\
        struct timeval _now_tv_;\
        char _timerepr_[32];\
        struct tm _now_tm_;\
        gettimeofday(&_now_tv_, NULL);\
        time_t _now_time_ = (time_t)_now_tv_.tv_sec;\
        int _milisecond_ = (int)_now_tv_.tv_usec / 1000;\
        strftime(_timerepr_, sizeof(_timerepr_), "%Y-%m-%d %H:%M:%S", localtime_r(&_now_time_, &_now_tm_));\
        fprintf(stdout, "[%s.%03d] [%s:%d:%s] [%s] [%u:%s] " fmt "\n",\
                _timerepr_, _milisecond_, __FILE__, __LINE__, __FUNCTION__, #level, actor_scheduler_current(),\
                actor_scheduler_currentname().c_str(), ##args);\
    } while(0)

#define actor_debug_log(fmt, args...) actor_log(DEBUG, fmt, ##args)
#define actor_info_log(fmt, args...) actor_log(INFO, fmt, ##args)

#define actor_error_log(fmt, args...) actor_log(ERROR, fmt, ##args)


#endif
