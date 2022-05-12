#include "actor.h"
#include "actor_scheduler.h"
#include "actor_scheduler_log.h"

class TestCreateAnotherActor : public Actor {
public:
    TestCreateAnotherActor() {
        actor_info_log("create \"CREATE ANOTHER\" actor instance");
    }

    ~TestCreateAnotherActor() {
        actor_info_log("destroy \"CREATE ANOTHER\" actor instance");
    }

    virtual void Start(const std::string &start_params) override {
        actor_scheduler_load_module("actor_test_another1.so");

        actor_scheduler_coroutine_task([this]() {
                actor_scheduler_coroutine_sleep(5000);

                m_another_actor = actor_scheduler_start("TestAnotherActor", "__GOGO__");

                StartFinished(0);
                }, 0);
    }

    virtual void Stop() {
        actor_scheduler_stop(m_another_actor);

        for(;;) {

            if(actor_scheduler_state(m_another_actor) == ActorState::STOPPED) {
                break;
            }

            actor_scheduler_coroutine_sleep(1000);
        }

        StopFinished(0);
    }
private:
    unsigned m_another_actor = ACTOR_INVALID_ID;
};

ACTOR_MODULE_REGISTER(TestCreateAnotherActor);


