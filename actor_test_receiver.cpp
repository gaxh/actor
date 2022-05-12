#include "actor.h"
#include "actor_scheduler.h"
#include "actor_scheduler_log.h"

class TestReceiverActor : public Actor {
public:
    TestReceiverActor() {
        actor_info_log("create instance");
    }

    ~TestReceiverActor() {
        actor_info_log("destroy instance");
    }

    virtual void Start(const std::string &start_params) override {
        actor_scheduler_name("receiver");
        actor_info_log("call start, start_params=%s", start_params.c_str());

        StartFinished(0);

        actor_scheduler_handler(888, [](ActorMessage &msg) {
                std::shared_ptr<std::string> payload = std::static_pointer_cast<std::string>(msg.payload);
                actor_info_log("receive message from %d: %s",msg.from_id, payload->c_str());
                });

    }

    virtual void Stop() override {
        actor_info_log("call stop");

        StopFinished(0);
    }
};

ACTOR_MODULE_REGISTER(TestReceiverActor);
