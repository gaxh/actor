#ifndef __ACTOR_H__
#define __ACTOR_H__

#include "class_loader.h"

class Actor : public LoadedClassWithDescriber<Actor> {
public:
    // ask Actor to start, do something like initializing
    // call StartFinished to tell framework it has started
    virtual void Start(const std::string &start_params) = 0;

    // ask Actor to stop
    // call StopFinished to tell framework it has stoped
    virtual void Stop() = 0;

protected:

    virtual void StartFinished(int code) final;

    virtual void StopFinished(int code) final;
};

#define ACTOR_MODULE_REGISTER(ClassName) CLASS_LOADER_REGISTER_CLASS(ClassName, Actor)

#endif
