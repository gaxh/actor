#include "actor.h"
#include "actor_scheduler.h"
#include "actor_scheduler_log.h"

class TestSenderActor : public Actor {
public:
    TestSenderActor() {
        actor_info_log("create instance");
    }

    ~TestSenderActor() {
        actor_info_log("destroy instance");
    }

    virtual void Start(const std::string &start_params) override {
        actor_scheduler_name("sender");
        actor_info_log("call start, start_params=%s", start_params.c_str());

        unsigned receiver_id = actor_scheduler_query_blocked("receiver");

        actor_scheduler_coroutine_task([receiver_id]() {
                int counter = 0;
                for(;;) {
                    char buffer[64];
                    snprintf(buffer, sizeof(buffer), "hello world: %d", counter++);
                    std::shared_ptr<std::string> payload = std::make_shared<std::string>(buffer);
                    actor_scheduler_send(receiver_id, 888, payload, 1);
                    actor_info_log("send message to %d: %s", receiver_id, payload->c_str());

                    actor_scheduler_coroutine_sleep(1000);
                }
                }, 0);

        StartFinished(0);
    }

    virtual void Stop() override {
        actor_info_log("call stop");

        StopFinished(0);
    }
};

ACTOR_MODULE_REGISTER(TestSenderActor);
