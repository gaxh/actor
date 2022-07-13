#include "actor.h"
#include "actor_scheduler.h"
#include "actor_scheduler_log.h"

#include <unistd.h>

// 给系统一点压力
// 初始发一个消息，在ping-pong之间来回传递

class TestPingActor : public Actor {
public:
    virtual void Start(const std::string &start_params) override {
        actor_scheduler_name("ping");

        unsigned actor_pong = actor_scheduler_start("TestPongActor", "__:)");

        actor_scheduler_send(actor_pong, 999, std::shared_ptr<void>(), 0);

        actor_scheduler_handler(999, [](unsigned from_id, unsigned type, std::shared_ptr<void> payload) {
                actor_scheduler_send(from_id, 999, payload, 0);
                });

        StartFinished(0);
    }

    virtual void Stop() override {
        StopFinished(0);
    }
};

class TestPongActor : public Actor {
public:
    virtual void Start(const std::string &start_params) override {
        actor_scheduler_name("pong");
 
        actor_scheduler_handler(999, [](unsigned from_id, unsigned type, std::shared_ptr<void> payload) {
                actor_scheduler_send(from_id, 999, payload, 0);
                });
        
        StartFinished(0);
    }

    virtual void Stop() override {
        StopFinished(0);
    }
};

ACTOR_MODULE_REGISTER(TestPingActor);
ACTOR_MODULE_REGISTER(TestPongActor);

