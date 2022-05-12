#include "actor.h"
#include "actor_scheduler.h"
#include "actor_scheduler_log.h"

class TestAnotherActor : public Actor {
public:
    TestAnotherActor() {
        actor_info_log("create \"ANOTHER\" actor instance");
    }

    ~TestAnotherActor() {
        actor_info_log("destroy \"ANOTHER\" actor instance");
    }

    virtual void Start(const std::string &start_params) override {
        actor_info_log("\"ANOTHER\" actor has started");

        StartFinished(0);
    }

    virtual void Stop() override {
        actor_info_log("\"ANOTHER\" actor has stopped");

        StopFinished(0);
    }
};

ACTOR_MODULE_REGISTER(TestAnotherActor);
