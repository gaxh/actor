#include "actor_scheduler.h"
#include "actor_scheduler_log.h"

#include <signal.h>

static void post_exit_handler(int sig) {
    actor_info_log("receive exit signal");

    actor_scheduler_post_exit();
}

static void load_test_shared_objects() {
    actor_scheduler_load_module("actor_test_receiver1.so");
    actor_scheduler_load_module("actor_test_sender1.so");
    actor_scheduler_load_module("actor_test_coroutine1.so");
    actor_scheduler_load_module("actor_test_create_another1.so");
    actor_scheduler_load_module("actor_test_pingpong1.so");
}

static void create_custom_actors() {
    actor_scheduler_start("TestReceiverActor", "receiver__");
    actor_scheduler_start("TestSenderActor", "sender__");
    actor_scheduler_start("TestCoroutineActor", "coroutine__");
    actor_scheduler_start("TestCreateAnotherActor", "create__");
//    actor_scheduler_start("TestPingActor", "ping__");
}

static void run_actor_scheduler() {
    signal(SIGINT, post_exit_handler);

    actor_scheduler_init(8);

    create_custom_actors();

    actor_scheduler_join();

    actor_scheduler_destroy();
}

int main(int argc, char **argv) {
    load_test_shared_objects();

    run_actor_scheduler();

    return 0;
}
