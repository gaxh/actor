#include "actor_scheduler.h"
#include "actor_message.h"
#include "spinlock.h"
#include "actor.h"
#include "coroutine.h"
#include "class_loader.h"
#include "actor_scheduler_timer.h"
#include "actor_scheduler_log.h"

#include <unistd.h>
#include <assert.h>
#include <time.h>

#include <unordered_map>
#include <vector>
#include <thread>

// timer

static struct {
    ActorSchedulerTimer timer;
    SpinLock lock;
} s_actor_timers;

static void actor_timer_init() {
    s_actor_timers.timer.Init(10000);
}

static void actor_timer_destroy() {
    s_actor_timers.timer.Destroy();
}

static ActorSchedulerTimer::TIMER_ID
actor_timer_new(unsigned long delay_ms, ActorSchedulerTimer::TIMER_CALLBACK cb) {
    return s_actor_timers.timer.New(s_actor_timers.lock, delay_ms, cb);
}

static int actor_timer_update() {
    return s_actor_timers.timer.Update(s_actor_timers.lock);
}

#ifdef ACTOR_SCHEDULER_PROFILING
// 当前线程cpu时间，微秒
static unsigned long long thread_cpu_time() {
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);

    return (unsigned long long)ts.tv_sec * (unsigned long long)1000000 +
        (unsigned long long)ts.tv_nsec / (unsigned long long)1000;
}
#endif

// end of timer

// context

class ActorContext;
static std::shared_ptr<ActorContext> get_context_by_id(unsigned id);
static unsigned long get_message_next_seq_id();

class ActorContext {
public:
    static constexpr unsigned MAX_PRIORITY = 2;
    static constexpr size_t COROUTINE_STACK_CAPACITY = 64 * 1024;
    static constexpr size_t COROUTINE_POOL_CAPACITY = 100;

#ifdef ACTOR_SCHEDULER_PROFILING
    struct Statistics {
        size_t message_processed = 0;
        size_t task_processed = 0;
        unsigned long long cpu_cost = 0;
        unsigned long long cpu_cost_start = 0;
        size_t message_queue_overload = 128;
    };
#endif

public:
    ActorContext(unsigned id, Actor *actor, const std::string &start_params) :
        m_id(id), m_actor(actor), m_start_params(start_params) {

        assert(actor);
        assert(id != ACTOR_INVALID_ID);

        m_message_queue = std::make_shared<ActorMessageQueue>(id, MAX_PRIORITY);
        m_coroutine_scheduler.Init(COROUTINE_STACK_CAPACITY, COROUTINE_POOL_CAPACITY);
    }

    ~ActorContext() {
        m_coroutine_scheduler.Destroy();
        m_actor->GetClassDescriber()->FreeObject(m_actor);
    }

    unsigned GetId() {
        return m_id;
    }

    const std::string &GetName() {
        return m_name;
    }

    void SetName(const std::string &name) {
        m_name = name;
    }

    // 返回actor是否已结束
    bool ProcessMessage(ActorMessage &msg, unsigned priority) {
        unsigned type = msg.type;

        auto iter = m_message_handlers.find(type);

        if(iter == m_message_handlers.end()) {
            actor_error_log("actor (%d:%s) can NOT get handler for message type %u",
                    m_id, m_name.c_str(), type);
            return false;
        }

        ACTOR_MESSAGE_HANDLER &handler = iter->second;

        COROUTINE_ID co_id = m_coroutine_scheduler.New(std::bind(handler, msg));

        ResumeCoroutine(co_id, priority);

#ifdef ACTOR_SCHEDULER_PROFILING
        ++m_statistics.message_processed;
#endif

        return m_state == ActorState::STOPPED;
    }

    // 返回actor是否已结束
    bool ProcessPendingTasks() {
        // 总体按优先级来
        for(unsigned i = 0; i < MAX_PRIORITY; ++i) {
            std::queue<COROUTINE_ID> &running_q = m_tasks.coroutine_running_queue[i];
            std::queue<std::function<void(void)>> &task_q = m_tasks.task_queue[i];

            // 执行所有待执行任务
            while(!task_q.empty()) {
                std::function<void(void)> task_handler = task_q.front();
                task_q.pop();
                COROUTINE_ID co_id = m_coroutine_scheduler.New(task_handler);

                ResumeCoroutine(co_id, i);
#ifdef ACTOR_SCHEDULER_PROFILING
                ++m_statistics.task_processed;
#endif

                if(m_state == ActorState::STOPPED) {
                    return true;
                }
            }
            // 执行所有待执行协程
            while(!running_q.empty()) {
                COROUTINE_ID co_id = running_q.front();
                running_q.pop();

                ResumeCoroutine(co_id, i);
#ifdef ACTOR_SCHEDULER_PROFILING
                ++m_statistics.task_processed;
#endif

                if(m_state == ActorState::STOPPED) {
                    return true;
                }
            }
        }

        return false;
    }

    void SetMessageHandler(unsigned type, ACTOR_MESSAGE_HANDLER handler) {
        if(handler) {
            m_message_handlers[type] = handler;
        } else {
            m_message_handlers.erase(type);
        }
    }

    void PushMessage(const ActorMessage &message, unsigned priority) {
        if(priority >= MAX_PRIORITY) {
            return;
        }

        m_message_queue->Push(message, priority);

#ifdef ACTOR_SCHEDULER_PROFILING
        size_t queue_size = m_message_queue->Size();
        if(queue_size >= m_statistics.message_queue_overload) {
            m_statistics.message_queue_overload += m_statistics.message_queue_overload;

            actor_error_log("message queue is overloaded, id=%u", m_id);
        }
#endif
    }

    unsigned long NextSeqId() {
        return m_next_seq_id++;
    }

    ActorState GetState() {
        return m_state;
    }

    std::string GetModuleName() {
        return m_actor ? m_actor->GetClassDescriber()->GetClassName() : "";
    }

    void RegisterReservedMessageHandlers() {
        SetMessageHandler(ActorMessageReservedType::ACTOR_START,
                std::bind(&ActorContext::OnActorStartMessage, this, std::placeholders::_1));

        SetMessageHandler(ActorMessageReservedType::ACTOR_STOP,
                std::bind(&ActorContext::OnActorStopMessage, this, std::placeholders::_1));

        SetMessageHandler(ActorMessageReservedType::ACTOR_TIMER,
                std::bind(&ActorContext::OnActorTimerMessage, this, std::placeholders::_1));

    }

    void StartFinished(int code) {
        if(m_state != ActorState::STARTING) {
            actor_error_log("start finish error, INVALID state. state=%d", (int)m_state);
            return;
        }

        m_state = ActorState::STARTED;
        actor_info_log("actor has started, code=%d", code);
    }

    void StopFinished(int code) {
        if(m_state != ActorState::STOPPING) {
            actor_error_log("stop finish error, INVALID state. state=%d", (int)m_state);
            return;
        }

        m_state = ActorState::STOPPED;
        actor_info_log("actor has stopped, code=%d", code);
    }

    COROUTINE_ID CurrentCoroutine() {
        return m_coroutine_scheduler.Current();
    }

    void CreateTask(std::function<void(void)> callback, unsigned priority) {
        if(priority >= MAX_PRIORITY) {
            return;
        }

        m_tasks.task_queue[priority].emplace(callback);
    }

    // 只能在协程外调用
    void ResumeCoroutine(COROUTINE_ID id, unsigned priority) {
#ifdef ACTOR_SCHEDULER_PROFILING
        m_statistics.cpu_cost_start = thread_cpu_time();
#endif
        m_coroutine_scheduler.Resume(id);

#ifdef ACTOR_SCHEDULER_PROFILING
        m_statistics.cpu_cost += (thread_cpu_time() - m_statistics.cpu_cost_start);
#endif

        // 该函数是否在执行中挂起
        // 挂起的话，得暂时保存这个协程
        if(CoroutineIsSuspended(id)) {
            m_tasks.coroutine_suspended_set[id] = priority;
        }
    }

    // 只能在协程里调用
    void SuspendCoroutine() {
        m_coroutine_scheduler.Yield();
    }

    void WakeupCoroutine(COROUTINE_ID id) {
        auto iter = m_tasks.coroutine_suspended_set.find(id);

        if(iter == m_tasks.coroutine_suspended_set.end()) {
            actor_error_log("can NOT wakeup coroutine, coroutine id is not found: %llu", id);
            return;
        }

        COROUTINE_ID co_id = iter->first;
        unsigned priority = iter->second;
        m_tasks.coroutine_suspended_set.erase(iter);

        m_tasks.coroutine_running_queue[priority].emplace(co_id);
    }

    // 只能在协程里调用
    void SleepCoroutine(unsigned long timeout_ms) {
        unsigned actor_id = actor_scheduler_current();

        ActorSchedulerTimer::TIMER_ID timer_id =
            actor_timer_new(timeout_ms, std::bind(OnSleepCoroutineTimeout,
                        actor_id, std::placeholders::_1));

        m_tasks.timer_sessions[timer_id] = m_coroutine_scheduler.Current();

        SuspendCoroutine();
    }

    size_t GetUnfinishedCoroutineNumber() {
        size_t ret = 0;

        for(unsigned i = 0; i < MAX_PRIORITY; ++i) {
            ret += m_tasks.coroutine_running_queue[i].size();
        }

        ret += m_tasks.coroutine_suspended_set.size();

        return ret;
    }

#ifdef ACTOR_SCHEDULER_PROFILING
    const Statistics &GetStatistics() {
        return m_statistics;
    }
#endif

private:
    // 消息处理函数都是在协程里执行，可以做协程可以做的事
    void OnActorStartMessage(ActorMessage &msg) {
        if(m_state != ActorState::NEW) {
            actor_error_log("can NOT start actor, INVALID state. state=%d", (int)m_state);
            return;
        }

        m_state = ActorState::STARTING;
        actor_info_log("call actor Start method");
        m_actor->Start(m_start_params);
    }

    void OnActorStopMessage(ActorMessage &msg) {
        if(m_state == ActorState::STOPPING || m_state == ActorState::STOPPED) {
            actor_error_log("can NOT stop actor, INVALID state. state=%d", (int)m_state);
            return;
        }

        m_state = ActorState::STOPPING;
        actor_info_log("call actor Stop method");
        m_actor->Stop();
    }

    void OnActorTimerMessage(ActorMessage &msg) {
        ActorSchedulerTimer::TIMER_ID timer_id = msg.reserved;

        auto iter = m_tasks.timer_sessions.find(timer_id);

        if(iter == m_tasks.timer_sessions.end()) {
            return;
        }

        COROUTINE_ID co_id = iter->second;
        m_tasks.timer_sessions.erase(iter);

        WakeupCoroutine(co_id);
    }

    bool CoroutineIsSuspended(COROUTINE_ID id) {
        return m_coroutine_scheduler.Status(id) == CoroutineStatus::SUSPENDED;
    }

    static void OnSleepCoroutineTimeout(unsigned actor_id, ActorSchedulerTimer::TIMER_ID timer_id) {
        // send timer message to actor <actor_id>

        ActorMessage msg;
        msg.from_id = actor_scheduler_current();
        msg.seq_id = get_message_next_seq_id();
        msg.reserved = timer_id;
        msg.type = ActorMessageReservedType::ACTOR_TIMER;

        actor_scheduler_send_raw(actor_id, msg, 0);
    }

    unsigned m_id;
    std::string m_name;
    Actor *m_actor;
    std::string m_start_params;
    std::unordered_map<unsigned, ACTOR_MESSAGE_HANDLER> m_message_handlers;
    std::shared_ptr<ActorMessageQueue> m_message_queue;
    unsigned long m_next_seq_id = 0;
    ActorState m_state = ActorState::NEW;

    struct {
        std::queue<std::function<void(void)>> task_queue[MAX_PRIORITY];
        std::queue<COROUTINE_ID> coroutine_running_queue[MAX_PRIORITY];
        std::unordered_map<COROUTINE_ID, int> coroutine_suspended_set; // COROUTINE_ID -> priority
        std::unordered_map<ActorSchedulerTimer::TIMER_ID, COROUTINE_ID> timer_sessions; // timerid -> coroutine
    } m_tasks;

    CoroutineScheduler m_coroutine_scheduler;

#ifdef ACTOR_SCHEDULER_PROFILING
    Statistics m_statistics;
#endif
};

constexpr unsigned ActorContext::MAX_PRIORITY;
constexpr size_t ActorContext::COROUTINE_STACK_CAPACITY;
constexpr size_t ActorContext::COROUTINE_POOL_CAPACITY;

static thread_local ActorContext *current_context = NULL;
static struct {
    std::unordered_map<unsigned, std::shared_ptr<ActorContext>> contexts;
    RWLock lock;
    unsigned next_id = 0;
} s_actor_contexts;

static ActorContext *get_current_context() {
    return current_context;
}

static void set_current_context(ActorContext *c) {
    current_context = c;
}

static std::shared_ptr<ActorContext> get_context_by_id(unsigned id) {
    RLockGuard g(s_actor_contexts.lock);

    auto iter = s_actor_contexts.contexts.find(id);

    return iter != s_actor_contexts.contexts.end() ? iter->second : NULL;
}

static void add_context(unsigned id, std::shared_ptr<ActorContext> context) {
    WLockGuard g(s_actor_contexts.lock);

    s_actor_contexts.contexts[id] = context;
}

static void remove_context(unsigned id) {
    WLockGuard g(s_actor_contexts.lock);

    s_actor_contexts.contexts.erase(id);
}

static unsigned get_next_actor_id() {
    WLockGuard g(s_actor_contexts.lock);

    while( s_actor_contexts.next_id == ACTOR_INVALID_ID ||
            s_actor_contexts.contexts.find(s_actor_contexts.next_id) !=
            s_actor_contexts.contexts.end()) {
        ++s_actor_contexts.next_id;
    }

    unsigned id = s_actor_contexts.next_id++;
    return id;
}

static std::vector<unsigned> get_all_context_ids() {
    std::vector<unsigned> ret;
    RLockGuard g(s_actor_contexts.lock);

    for(auto iter = s_actor_contexts.contexts.begin();
            iter != s_actor_contexts.contexts.end(); ++iter) {
        ret.emplace_back(iter->first);
    }

    return std::move(ret);
}

static bool get_current_has_no_context() {
    RLockGuard g(s_actor_contexts.lock);

    return s_actor_contexts.contexts.empty();
}

static std::atomic<unsigned> s_invalid_next_seq_id = ATOMIC_VAR_INIT(0);

static unsigned long get_message_next_seq_id() {
    ActorContext *c = get_current_context();

    return c ? c->NextSeqId() : s_invalid_next_seq_id.fetch_add(1);
}

// end of context

// name & state
static struct {
    std::unordered_map<std::string, unsigned> names;
    RWLock lock;
} s_module_names;

void actor_scheduler_name(const std::string &actor_name, unsigned id) {
    if(actor_name.empty()) {
        return;
    }

    if(id == ACTOR_INVALID_ID) {
        return;
    }

    {
        WLockGuard g(s_module_names.lock);
        if(id == ACTOR_INVALID_ID) {
            s_module_names.names.erase(actor_name);
        } else {
            s_module_names.names[actor_name] = id;
        }
    }

    {
        std::shared_ptr<ActorContext> c = get_context_by_id(id);

        if(c) {
            c->SetName(actor_name);
        }
    }
}

void actor_scheduler_name(const std::string &actor_name) {
    actor_scheduler_name(actor_name, actor_scheduler_current());
}

unsigned actor_scheduler_query(const std::string &actor_name) {
    RLockGuard g(s_module_names.lock);
    auto iter = s_module_names.names.find(actor_name);
    return iter != s_module_names.names.end() ? iter->second : ACTOR_INVALID_ID;
}

unsigned actor_scheduler_query_blocked(const std::string &actor_name) {
    for(;;) {
        unsigned id = actor_scheduler_query(actor_name);

        if(id != ACTOR_INVALID_ID) {
            return id;
        }

        actor_scheduler_coroutine_sleep(1000);
    }
}

ActorState actor_scheduler_state(unsigned id) {
    std::shared_ptr<ActorContext> context = get_context_by_id(id);

    return context ? context->GetState() : ActorState::STOPPED;
}


// end of name & state

// current

unsigned actor_scheduler_current() {
    ActorContext *c = get_current_context();

    return c ? c->GetId() : ACTOR_INVALID_ID;
}

std::string actor_scheduler_currentname() {
    ActorContext *c = get_current_context();

    return c ? c->GetName() : "SYS";
}

ActorState actor_scheduler_currentstate() {
    return get_current_context()->GetState();
}

// end of current

// init & execute

static constexpr int WORKERS_MAX_NUMBER = 1024;
static std::thread *s_workers[WORKERS_MAX_NUMBER] = {0};
static std::thread *s_timer_thread = NULL;
static volatile int s_post_exit = 0;

static void process_actor_message(std::shared_ptr<ActorContext> &context,
        std::shared_ptr<ActorMessageQueue> &q) {

    static constexpr int max_proc_message_nb = 20;

    if(!context) {
        actor_error_log("process message failed: context %u is NULL", q->Id());
        return;
    }

    ActorMessage msg;
    unsigned priority;

    set_current_context(context.get());

    bool actor_stopped = false;

    for(int i = 0; i < max_proc_message_nb; ++i) {
        if(!q->Pop(msg, priority)) {
            break;
        }

        actor_stopped = context->ProcessMessage(msg, priority) ||
            context->ProcessPendingTasks();

        if(actor_stopped) {
            break;
        }
    }

    set_current_context(NULL);

    if(actor_stopped) {
        // 清理actor

        actor_scheduler_name(context->GetName(), ACTOR_INVALID_ID);

        remove_context(context->GetId());

        actor_info_log("actor has stopped, module_name=%s, id=%u", context->GetModuleName().c_str(), context->GetId());

        if(context->GetUnfinishedCoroutineNumber() != 0) {
            actor_error_log("actor has unfinished coroutines, may cause memory leak. id=%u",
                    context->GetId());
        }

#ifdef ACTOR_SCHEDULER_PROFILING
        const ActorContext::Statistics &stats = context->GetStatistics();
        actor_info_log("statistics of actor %u: message_processed=%zu, task_processed=%zu, cpu_cost=%llu, message_queue_overload=%zu",
                context->GetId(), stats.message_processed, stats.task_processed,
                stats.cpu_cost, stats.message_queue_overload);
#endif
    }
}

static void actor_worker_thread(int worker_id) {
    // TODO: possibly bind thread to one cpu

    int acquire_fail_nb = 0;
    constexpr int acquire_fail_threshold = 10;

    for(;;) {

        if(s_post_exit && get_current_has_no_context()) {
            break;
        }

        std::shared_ptr<ActorMessageQueue> q = ActorMessageQueue::Acquire();

        if(!q) {
            ++acquire_fail_nb;

            if(acquire_fail_nb >= acquire_fail_threshold) {
                acquire_fail_nb = 0;

                usleep(1000);
            }
            continue;
        }

        unsigned id = q->Id();

        std::shared_ptr<ActorContext> c = get_context_by_id(id);

        // process message
        process_actor_message(c, q);

        q->Release();
    }
}

static void actor_timer_thread() {
    for(;;) {
        if(s_post_exit && get_current_has_no_context()) {
            break;
        }

        int update_nb = actor_timer_update();

        if(update_nb <= 0) {
            usleep(1000);
        }
    }
}

void actor_scheduler_init(int worker_nb) {
    actor_timer_init();

    s_timer_thread = new std::thread(actor_timer_thread);

    for(int i = 0; i < worker_nb && i < WORKERS_MAX_NUMBER; ++i) {
        actor_info_log("create worker thread: %d", i);
        s_workers[i] = new std::thread(actor_worker_thread, i);
    }
}

void actor_scheduler_destroy() {
    actor_timer_destroy();
}

void actor_scheduler_join() {
    for(int i = 0; i < WORKERS_MAX_NUMBER; ++i) {
        std::thread *worker = s_workers[i];

        if(worker && worker->joinable()) {
            actor_info_log("wait for worker thread: %d", i);
            worker->join();
            actor_info_log("worker thread has stopped: %d", i);

            delete worker;
            s_workers[i] = NULL;
        }
    }

    if(s_timer_thread->joinable()) {
        actor_info_log("wait for timer thread");
        s_timer_thread->join();
        actor_info_log("timer thread has stopped");

        delete s_timer_thread;
        s_timer_thread = NULL;
    }
}

void actor_scheduler_post_exit() {
    s_post_exit = 1;

    // notify all actor to exit
    std::vector<unsigned> actor_ids = get_all_context_ids();

    for(unsigned id: actor_ids) {
        actor_scheduler_stop(id);
    }
}

// end of init & execute

// message

void actor_scheduler_handler(unsigned type, ACTOR_MESSAGE_HANDLER handler) {
    get_current_context()->SetMessageHandler(type, handler);
}

static void actor_scheduler_send_to_context(ActorContext *context, const ActorMessage &message, unsigned priority) {
    if(priority >= ActorContext::MAX_PRIORITY) {
        actor_error_log("failed to send message to %u, priority %u is invalid",
                context->GetId(), priority);
        return;
    }

    context->PushMessage(message, priority);
}

void actor_scheduler_send_raw(unsigned id, const ActorMessage &message, unsigned priority) {
    std::shared_ptr<ActorContext> c = get_context_by_id(id);

    if(!c) {
        return;
    }

    actor_scheduler_send_to_context(c.get(), message, priority);
}

void actor_scheduler_send(unsigned id, unsigned type, std::shared_ptr<void> payload, unsigned priority) {
    ActorMessage msg;

    msg.type = type;
    msg.from_id = actor_scheduler_current();
    msg.seq_id = get_message_next_seq_id();
    msg.payload = payload;

    actor_scheduler_send_raw(id, msg, priority);
}

// end of message

// start & stop

unsigned actor_scheduler_start(const std::string &module_name, const std::string &start_params) {
    const LoadedClassDescriberInterface<Actor> *describer =
        class_loader_get_class_describer<Actor>(module_name, "Actor");

    if(!describer) {
        actor_error_log("can NOT get describer for module %s", module_name.c_str());
        return ACTOR_INVALID_ID;
    }

    Actor *actor = describer->CreateObject();

    unsigned id = get_next_actor_id();

    std::shared_ptr<ActorContext> context = std::make_shared<ActorContext>(id, actor, start_params);

    add_context(id, context);

    context->RegisterReservedMessageHandlers();

    // send start message to self actor
    ActorMessage msg;
    msg.from_id = actor_scheduler_current();
    msg.seq_id = get_message_next_seq_id();
    msg.type = ACTOR_START;
    actor_scheduler_send_to_context(context.get(), msg, 0);

    actor_info_log("send start request to actor, module_name=%s, id=%u",
            module_name.c_str(), id);

    return id;
}

void actor_scheduler_stop(unsigned id) {
    std::shared_ptr<ActorContext> context = get_context_by_id(id);

    if(!context) {
        actor_error_log("can NOT get actor context: %u", id);
        return;
    }

    // send stop message to target actor
    ActorMessage msg;
    msg.from_id = actor_scheduler_current();
    msg.seq_id = get_message_next_seq_id();
    msg.type = ACTOR_STOP;
    actor_scheduler_send_to_context(context.get(), msg, 0);

    actor_info_log("send stop request to actor, module_name=%s, id=%u", context->GetModuleName().c_str(), id);
}

void actor_scheduler_start_finished(int code) {
    get_current_context()->StartFinished(code);
}

void actor_scheduler_stop_finished(int code) {
    get_current_context()->StopFinished(code);
}

// end of start & stop

// coroutine

void actor_scheduler_coroutine_task(std::function<void(void)> callback, unsigned priority) {
    get_current_context()->CreateTask(callback, priority);
}

COROUTINE_ID actor_scheduler_coroutine_current() {
    return get_current_context()->CurrentCoroutine();
}

void actor_scheduler_coroutine_wakeup(COROUTINE_ID id) {
    get_current_context()->WakeupCoroutine(id);
}

void actor_scheduler_coroutine_suspend() {
    get_current_context()->SuspendCoroutine();
}

void actor_scheduler_coroutine_sleep(unsigned long timeout_ms) {
    get_current_context()->SleepCoroutine(timeout_ms);
}

// end of coroutine

// module

bool actor_scheduler_load_module(const std::string &module_name) {
    return class_loader_load_library(module_name);
}

// end of module

