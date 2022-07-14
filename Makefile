# libactor
libactor_root := ./libactor

libactor_src := $(wildcard ${libactor_root}/*.cpp)
	
libactor_headers := $(wildcard ${libactor_root}/*.h)

depend_libs := -ldl -lpthread

all : actor_test_main.out

cxx_flag := -g -O2 -Wall -I${libactor_root} ${my_cxx_flag}

ld_flag := -rdynamic

shared_flag := -fPIC --shared

%.o : %.cpp
	${CXX} -o $@ -c $^ ${cxx_flag}

libactor_src_object := $(patsubst %.cpp, %.o, ${libactor_src})

libactor_lib := ${libactor_root}/libactor1.a

${libactor_lib} : ${libactor_src_object}
	${AR} rcs $@ $^

actor_test_main.out : actor_test_main.o ${libactor_headers} ${libactor_lib}
	${CXX} -o $@ actor_test_main.o ${ld_flag} -Wl,--whole-archive ${libactor_lib} -Wl,--no-whole-archive ${depend_libs}

# end of libactor

# libactor test
all : libactor_test

libactor_test:
	cd ${libactor_root}/test && ${MAKE}
# end of libactor test

# custom actors

actor_test_sender1.so : actor_test_sender.cpp ${libactor_headers}
	${CXX} -o $@ actor_test_sender.cpp ${cxx_flag} ${ld_flag} ${shared_flag} ${depend_libs}

all : actor_test_sender1.so

actor_test_receiver1.so : actor_test_receiver.cpp ${libactor_headers}
	${CXX} -o $@ actor_test_receiver.cpp ${cxx_flag} ${ld_flag} ${shared_flag} ${depend_libs}

all : actor_test_receiver1.so

actor_test_coroutine1.so : actor_test_coroutine.cpp ${libactor_headers}
	${CXX} -o $@ actor_test_coroutine.cpp ${cxx_flag} ${ld_flag} ${shared_flag} ${depend_libs}

all : actor_test_coroutine1.so

actor_test_another1.so : actor_test_another_actor.cpp ${libactor_headers}
	${CXX} -o $@ actor_test_another_actor.cpp ${cxx_flag} ${ld_flag} ${shared_flag} ${depend_libs}

all : actor_test_another1.so

actor_test_create_another1.so : actor_test_create_another_actor.cpp ${libactor_headers}
	${CXX} -o $@ actor_test_create_another_actor.cpp ${cxx_flag} ${ld_flag} ${shared_flag} ${depend_libs}

all : actor_test_create_another1.so

actor_test_pingpong1.so : actor_test_pingpong.cpp ${libactor_headers}
	${CXX} -o $@ actor_test_pingpong.cpp ${cxx_flag} ${ld_flag} ${shared_flag} ${depend_libs}

all : actor_test_pingpong1.so

# end of actors

clean:
	@find . -name "*.o" -print0 | xargs -0 -n1 -t -I{} rm -f {}
	@find . -name "*.a" -print0 | xargs -0 -n1 -t -I{} rm -f {}
	@find . -name "*.out" -print0 | xargs -0 -n1 -t -I{} rm -f {}
	@find . -name "*.so" -print0 | xargs -0 -n1 -t -I{} rm -f {}
	cd ${libactor_root}/test && ${MAKE} clean
