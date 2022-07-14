#ifndef __ACTOR_SCHEDULER_H__
#define __ACTOR_SCHEDULER_H__

#include <string>
#include <memory>
#include <functional>
#include <queue>

#include "coroutine.h"
#include "spinlock.h"

// 系统消息类型，用户不能占用
enum ActorMessageReservedType : unsigned {
    BEGIN = 0xffff0000,
    ACTOR_START,
    ACTOR_STOP,
    ACTOR_TIMER,
    END,
};

struct ActorMessage {
    unsigned from_id; // 消息是从哪个actor发来的
    unsigned long seq_id; // 消息序列号
    unsigned long long reserved;
    unsigned type; // 消息类型
    std::shared_ptr<void> payload; // 消息内容
};

constexpr unsigned ACTOR_INVALID_ID = 0;

enum class ActorState {
    NEW = 0,
    STARTING,
    STARTED,
    STOPPING,
    STOPPED,
};

// 初始化：创建各种线程
void actor_scheduler_init(int worker_nb);

// 结束时：结束所有线程
void actor_scheduler_destroy();

// 主线程挂起：等待worker线程退出
void actor_scheduler_join();

// 发起结束的命令
void actor_scheduler_post_exit();

// 创建新的actor
unsigned actor_scheduler_start(const std::string &module_name, const std::string &start_params);

// 结束某个actor
void actor_scheduler_stop(unsigned id);

// 通知框架actor已启动完成
void actor_scheduler_start_finished(int code);

// 通知框架actor已结束完成
void actor_scheduler_stop_finished(int code);

// 为actor取名
void actor_scheduler_name(const std::string &actor_name, unsigned id);
void actor_scheduler_name(const std::string &actor_name);

// 获得某个actor的id。立即返回结果，不会等，可能返回 ACTOR_INVALID_ID
unsigned actor_scheduler_query(const std::string &actor_name);

// 获得某个actor的id。隔1秒查一次，直到查到结果，不会返回ACTOR_INVALID_ID
// 只能在协程里调用，查不到结果时，会挂起当前协程
unsigned actor_scheduler_query_blocked(const std::string &actor_name);

// 获得当前actor的id
unsigned actor_scheduler_current();

// 获得当前actor的名字
std::string actor_scheduler_currentname();

// 获得当前actor的状态
ActorState actor_scheduler_currentstate();

// 获得actor的状态
ActorState actor_scheduler_state(unsigned id);

// 注册当前actor的消息处理函数
using ACTOR_MESSAGE_RAW_HANDLER = typename std::function<void(ActorMessage &message)>;
void actor_scheduler_handler_raw(unsigned type, ACTOR_MESSAGE_RAW_HANDLER handler);

using ACTOR_MESSAGE_HANDLER = typename std::function<void(unsigned from_id, unsigned type, std::shared_ptr<void> payload)>;
void actor_scheduler_handler(unsigned type, ACTOR_MESSAGE_HANDLER handler);

// 发送消息给某个actor
void actor_scheduler_send_raw(unsigned id, const ActorMessage &message, unsigned priority);

void actor_scheduler_send(unsigned id, unsigned type, std::shared_ptr<void> payload, unsigned priority);

// 创建任务（协程）
void actor_scheduler_coroutine_task(std::function<void(void)> callback, unsigned priority);

// 获得当前协程id
COROUTINE_ID actor_scheduler_coroutine_current();

// 唤醒协程
void actor_scheduler_coroutine_wakeup(COROUTINE_ID id);

// 让出当前协程
void actor_scheduler_coroutine_suspend();

// 挂起当前协程
void actor_scheduler_coroutine_sleep(unsigned long timeout_ms);

// 加载module
bool actor_scheduler_load_module(const std::string &module_name);

#endif
