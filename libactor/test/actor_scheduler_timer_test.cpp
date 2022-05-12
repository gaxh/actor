#include "actor_scheduler_timer.h"
#include "spinlock.h"

#include <unistd.h>
#include <stdio.h>

int main() {

    ActorSchedulerTimer timer;
    using TIMER_ID = ActorSchedulerTimer::TIMER_ID;

    SpinLock lock;

    timer.Init(10);

    timer.New(lock, 0, [](TIMER_ID id){ printf("delay 0: 0\n"); });
    timer.New(lock, 0, [](TIMER_ID id){ printf("delay 0: 1\n"); });
    timer.New(lock, 0, [](TIMER_ID id){ printf("delay 0: 2\n"); });

    timer.New(lock, 1000, [&timer, &lock](TIMER_ID id){ printf("dalay 1000\n"); timer.New(lock, 1000, [](TIMER_ID id){ printf("delay 1000, after 1000\n"); }); });

    timer.New(lock, 2000, [](TIMER_ID id){ printf("delay 2000\n"); });

    timer.New(lock, 3000, [](TIMER_ID id){ printf("delay 3000\n"); });

    timer.New(lock, 30000, [](TIMER_ID id){ printf("delay 30000\n"); });

    for(;;) {

        int up = timer.Update(lock);
        if(up) {
            printf("update count: %d\n", up);
        }

        usleep(1000);
    }

    timer.Destroy();

    return 0;
}
