#ifndef __CLASS_LOADER_H__
#define __CLASS_LOADER_H__

#include <string>
#include <type_traits>
#include <map>

#include <stdio.h>

#include "spinlock.h"

#define CLASS_LOADER_ERROR(fmt, args...) fprintf(stderr, "[%s:%d:%s] " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ##args);

// internal functions, can NOT call outside
void *class_loader_get_sub_loader(const std::string &base_class_name);

void class_loader_set_sub_loader(const std::string &base_class_name, void *sub_loader);

void class_loader_add_class_info(const std::string &class_name, const std::string &base_class_name);

RWLock &class_loader_internal_lock();
// end of internal functions

template<typename base_class>
class LoadedClassDescriberInterface {
public:
    using BaseClass = base_class;

    virtual ~LoadedClassDescriberInterface() {};

    virtual const std::string &GetClassName() const = 0;

    virtual BaseClass *CreateObject() const = 0;

    virtual void FreeObject(BaseClass *obj) const = 0;
};

template<typename base_class>
class LoadedClassWithDescriber {
public:
    using BaseClass = base_class;
    using DescriberType = LoadedClassDescriberInterface<BaseClass>;

    virtual ~LoadedClassWithDescriber() {};

    virtual const DescriberType *GetClassDescriber() const final {
        return m_class_describer;
    }

    virtual void SetClassDescriber(const DescriberType *describer) final {
        m_class_describer = describer;
    }

private:
    const DescriberType *m_class_describer = NULL;
};

template<typename derived_class, typename base_class,
    typename std::enable_if<std::is_base_of<LoadedClassWithDescriber<base_class>, base_class>::value, int>::type = 0,
    typename std::enable_if<std::is_base_of<base_class, derived_class>::value, int>::type = 0
    >
class LoadedClassDescriber : public LoadedClassDescriberInterface<base_class> {
public:
    using BaseClass = base_class;
    using DerivedClass = derived_class;

    LoadedClassDescriber(const std::string &class_name) : m_class_name(class_name) {

    }

    ~LoadedClassDescriber() {

    }

    virtual const std::string &GetClassName() const override {
        return m_class_name;
    }

    virtual BaseClass *CreateObject() const override {
        BaseClass *obj = new DerivedClass();
        obj->SetClassDescriber(this);
        return obj;
    }

    virtual void FreeObject(BaseClass *obj) const override {
        delete obj;
    }
private:
    std::string m_class_name;
};

// internal class, do NOT call any method outside
template<typename base_class>
class SubClassLoader {
public:
    using BaseClass = base_class;
    using DescriberType = LoadedClassDescriberInterface<BaseClass>;

    const DescriberType *GetClassDescriber(const std::string &name) const {
        auto iter = m_class_describers.find(name);

        return iter != m_class_describers.end() ? iter->second : NULL;
    }

    void RegisterClassDescriber(const std::string &name, DescriberType *describer) {
        if(m_class_describers.find(name) != m_class_describers.end()) {
            CLASS_LOADER_ERROR("class %s has been registered, it will be overrided by new class", name.c_str());
        }

        m_class_describers[name] = describer;
    }

private:
    std::map<std::string, DescriberType *> m_class_describers; // do NOT free pointers of DescriberType
};

bool class_loader_load_library(const std::string &path);

std::string class_loader_dump_meta();

template<typename derived_class, typename base_class>
void class_loader_register_class(const std::string &derived_class_name, const std::string &base_class_name) {
    WLockGuard g(class_loader_internal_lock());

    using DerivedClass = derived_class;
    using BaseClass = base_class;

    void *sub_loader_vp = class_loader_get_sub_loader(base_class_name);

    if(!sub_loader_vp) {
        sub_loader_vp = (void *)(new SubClassLoader<BaseClass>());
        class_loader_set_sub_loader(base_class_name, sub_loader_vp);
    }

    SubClassLoader<BaseClass> *sub_loader = (SubClassLoader<BaseClass> *)sub_loader_vp;

    sub_loader->RegisterClassDescriber(derived_class_name,
            new LoadedClassDescriber<DerivedClass, BaseClass>(derived_class_name));

    class_loader_add_class_info(derived_class_name, base_class_name);
}

template<typename base_class>
const LoadedClassDescriberInterface<base_class> *
class_loader_get_class_describer(const std::string &derived_class_name, const std::string &base_class_name) {
    RLockGuard g(class_loader_internal_lock());

    using BaseClass = base_class;

    void *sub_loader_vp = class_loader_get_sub_loader(base_class_name);

    return sub_loader_vp ?
        ((SubClassLoader<BaseClass> *)sub_loader_vp)->GetClassDescriber(derived_class_name) : NULL;
}

#define CLASS_LOADER_REGISTER_CLASS(derived_class, base_class)\
    struct __CLASS_LOADER_STRUCT_FOR_##derived_class##_DERIVED_FROM_##base_class##__ {\
        __CLASS_LOADER_STRUCT_FOR_##derived_class##_DERIVED_FROM_##base_class##__() {\
            class_loader_register_class<derived_class, base_class>(#derived_class, #base_class);\
        }\
        ~__CLASS_LOADER_STRUCT_FOR_##derived_class##_DERIVED_FROM_##base_class##__() {}\
    };\
    static __CLASS_LOADER_STRUCT_FOR_##derived_class##_DERIVED_FROM_##base_class##__ \
        __CLASS_LOADER_INITIALER_FOR_##derived_class##_DERIVED_FROM_##base_class##__;


#endif
