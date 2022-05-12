#include "actor.h"
#include "actor_scheduler.h"

void Actor::StartFinished(int code) {
    actor_scheduler_start_finished(code);
}

void Actor::StopFinished(int code) {
    actor_scheduler_stop_finished(code);
}

