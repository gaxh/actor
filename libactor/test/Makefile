
all : actor_scheduler_timer_test.out

actor_scheduler_timer_test.out : actor_scheduler_timer_test.cpp ../actor_scheduler_timer.cpp ../actor_scheduler_timer.h ../spinlock.h ../spinlock.cpp
	${CXX} -o $@ actor_scheduler_timer_test.cpp ../actor_scheduler_timer.cpp ../spinlock.cpp -Os -g -I../ -Wall

clean:
	@find . -name "*.out" -print0 | xargs -0 -n1 -t -I{} rm -f {}
