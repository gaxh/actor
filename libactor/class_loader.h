#ifndef __CLASS_LOADER_H__
#define __CLASS_LOADER_H__

#include <string>
#include <type_traits>
#include <map>
#include <memory>

#include <stdio.h>

#include "spinlock.h"

#define CLASS_LOADER_ERROR(fmt, args...) fprintf(stderr, "[%s:%d:%s] " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ##args);

// internal functions, can NOT call outside
std::shared_ptr<void> class_loader_get_sub_loader(const std::string &base_class_name);

void class_loader_set_sub_loader(const std::string &base_class_name, const std::shared_ptr<void> &sub_loader);

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

    std::shared_ptr<DescriberType> GetClassDescriber() const {
        return m_class_describer;
    }

    void SetClassDescriber(const std::shared_ptr<DescriberType> &describer) {
        m_class_describer = describer;
    }

private:
    std::shared_ptr<DescriberType> m_class_describer;
};

template<typename derived_class, typename base_class,
    typename std::enable_if<std::is_base_of<LoadedClassWithDescriber<base_class>, base_class>::value, int>::type = 0>
class LoadedClassDescriber : public LoadedClassDescriberInterface<base_class> {
public:
    using BaseClass = base_class;
    using DerivedClass = derived_class;
    using DescriberType = LoadedClassDescriberInterface<BaseClass>;

    LoadedClassDescriber(const std::string &class_name) : m_class_name(class_name) {

    }

    ~LoadedClassDescriber() {

    }

    virtual const std::string &GetClassName() const override {
        return m_class_name;
    }

    virtual BaseClass *CreateObject() const override {
        BaseClass *obj = new DerivedClass();
        obj->SetClassDescriber(m_weak_this.lock());
        return obj;
    }

    virtual void FreeObject(BaseClass *obj) const override {
        delete obj;
    }

    void EnableShared(const std::shared_ptr<DescriberType> &this_ptr) {
        m_weak_this = this_ptr;
    }

private:
    std::string m_class_name;
    std::weak_ptr<DescriberType> m_weak_this;
};

// internal class, do NOT call any method outside
template<typename base_class>
class SubClassLoader {
public:
    using BaseClass = base_class;
    using DescriberType = LoadedClassDescriberInterface<BaseClass>;

    std::shared_ptr<DescriberType> GetClassDescriber(const std::string &name) const {
        auto iter = m_class_describers.find(name);

        return iter != m_class_describers.end() ? iter->second : NULL;
    }

    void RegisterClassDescriber(const std::string &name, const std::shared_ptr<DescriberType> &describer) {
        if(m_class_describers.find(name) != m_class_describers.end()) {
            CLASS_LOADER_ERROR("class %s has been registered, it will be overrided by new class", name.c_str());
        }

        m_class_describers[name] = describer;
    }

private:
    std::map<std::string, std::shared_ptr<DescriberType>> m_class_describers;
};

bool class_loader_load_library(const std::string &path);

std::string class_loader_dump_meta();

template<typename derived_class, typename base_class>
void class_loader_register_class(const std::string &derived_class_name, const std::string &base_class_name) {
    WLockGuard g(class_loader_internal_lock());

    using DerivedClass = derived_class;
    using BaseClass = base_class;

    std::shared_ptr<void> sub_loader_vp = class_loader_get_sub_loader(base_class_name);

    if(!sub_loader_vp) {
        sub_loader_vp = std::make_shared<SubClassLoader<BaseClass>>(); // implicit cast to void *
        class_loader_set_sub_loader(base_class_name, sub_loader_vp);
    }

    std::shared_ptr<SubClassLoader<BaseClass>> sub_loader = std::static_pointer_cast<SubClassLoader<BaseClass>>(sub_loader_vp);

    std::shared_ptr<LoadedClassDescriber<DerivedClass, BaseClass>> describer =
        std::make_shared<LoadedClassDescriber<DerivedClass, BaseClass>>(derived_class_name);
    describer->EnableShared(describer);

    sub_loader->RegisterClassDescriber(derived_class_name, describer);

    class_loader_add_class_info(derived_class_name, base_class_name);
}

template<typename base_class>
std::shared_ptr<LoadedClassDescriberInterface<base_class>>
class_loader_get_class_describer(const std::string &derived_class_name, const std::string &base_class_name) {
    RLockGuard g(class_loader_internal_lock());

    using BaseClass = base_class;

    std::shared_ptr<void> sub_loader_vp = class_loader_get_sub_loader(base_class_name);

    return sub_loader_vp ?
        (std::static_pointer_cast<SubClassLoader<BaseClass>>(sub_loader_vp))->GetClassDescriber(derived_class_name) : NULL;
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
