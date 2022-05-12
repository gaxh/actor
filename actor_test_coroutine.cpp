#include "actor.h"
#include "actor_scheduler.h"
#include "actor_scheduler_log.h"

class TestCoroutineActor : public Actor {
public:
    TestCoroutineActor() {
        actor_info_log("create instance");
    }

    ~TestCoroutineActor() {
        actor_info_log("destroy instance");
    }

    virtual void Start(const std::string &start_params) override {
        actor_scheduler_name("coroutine");
        actor_info_log("call start, start_params=%s", start_params.c_str());

        StartFinished(0);

        actor_scheduler_coroutine_task(
                std::bind(&TestCoroutineActor::CoroutineTask, this, 999), 1);

        actor_scheduler_coroutine_task([]() {

                COROUTINE_ID co_id = 0;

                actor_info_log("test wakeup sleep coroutine");

                actor_scheduler_coroutine_task([&co_id]() {
                        co_id = actor_scheduler_coroutine_current();
                        actor_info_log("test wakeup sleep begin");

                        actor_scheduler_coroutine_sleep(2000);

                        actor_info_log("test wakeup sleep end");
                        }, 1);

                actor_scheduler_coroutine_sleep(1000);

                actor_info_log("test wakeup sleep coroutine: do wakeup");
                actor_scheduler_coroutine_wakeup(co_id);
                }, 0);

        actor_scheduler_coroutine_task([]() {

                COROUTINE_ID co_id = 0;

                actor_info_log("test wakeup suspended coroutine");

                actor_scheduler_coroutine_task([&co_id](){
                        co_id = actor_scheduler_coroutine_current();

                        actor_info_log("test wakeup suspended begin");

                        actor_scheduler_coroutine_suspend();

                        actor_info_log("test wakeup suspended end");
                        }, 0);

                actor_scheduler_coroutine_sleep(1000);

                actor_info_log("test wakeup suspended coroutine: do wakeup");
                actor_scheduler_coroutine_wakeup(co_id);
                }, 0);

        actor_scheduler_coroutine_task([]() {

                actor_scheduler_coroutine_sleep(10000);

                actor_scheduler_stop(actor_scheduler_current());

                }, 0);
    }

    virtual void Stop() override {
        actor_info_log("call stop");

        StopFinished(0);
    }
private:

    void CoroutineTask(int v) {
        actor_info_log("test coroutine sleep, v=%d", v);

        actor_scheduler_coroutine_sleep(2000);

        actor_info_log("test coroutine sleep, after sleep, v=%d", v);
    }
};

ACTOR_MODULE_REGISTER(TestCoroutineActor);
