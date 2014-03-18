#pragma once
//------------------------------------------------------------------------------
/**
    @class Oryol::Resource::Factory
    @brief generic resource factory class

    Resource factories create resource objects from setup objects.
    The actual creation/initialization is delegated to custom resource
    loader objects. Resource objects must be in the Setup state (which
    means they must have a valid Setup object, which describes how
    the resource should be created).
    
    Factories can setup resources both synchronously
    or asynchronously. For asynchronous setup, just continue calling
    the Setup method until the resource is no longer in the Pending
    state on return.
    
    The first registered Loader which accepts the resource object
    (returns true in its Accept method) is tasked with setting
    up the resource, so the order at which loaders are attached 
    may be important.

    The LOADER base class must be derived Core::RefCounted. All actual
    loaders attached to the factory must in turn be derived from the
    LOADER base class and implement the following methods:
 
    virtual bool Accepts(const RESOURCE& res) const;
    virtual bool Setup(RESOURCE& res);
    virtual bool SetupWithData(RESOURCE& res, const Ptr<IO::Stream>& data);
 
*/
#include "Core/Types.h"
#include "Core/Containers/Array.h"
#include "Core/Log.h"
#include "IO/Stream.h"
#include "Resource/State.h"

namespace Oryol {
namespace Resource {

template<class RESOURCE, class LOADER> class Factory {
public:
    /// destructor
    ~Factory();

    /// attach a resource loader
    void AttachLoader(const Core::Ptr<LOADER>& loader);
    /// test if setup should be called for a resource
    bool NeedsSetupResource(const RESOURCE& resource) const;
    /// setup resource, continue calling until res state is not Pending
    void SetupResource(RESOURCE& resource);
    /// setup with input data, continue calling until res state is not Pending
    void SetupResource(RESOURCE& resource, const Core::Ptr<IO::Stream>& data);
    /// destroy the resource
    void DestroyResource(RESOURCE& resource);

    /// get factory name
    const Core::StringAtom& GetName() const;
    
protected:
    /// set factory name (for debugging)
    void setName(const Core::StringAtom& name);

    Core::Array<Core::Ptr<LOADER>> loaders;
    Core::StringAtom name;
};

//------------------------------------------------------------------------------
template<class RESOURCE, class LOADER>
Factory<RESOURCE,LOADER>::~Factory() {
    for (const auto& loader : this->loaders) {
        loader->onDetachFromFactory();
    }
}

//------------------------------------------------------------------------------
template<class RESOURCE, class LOADER> void
Factory<RESOURCE,LOADER>::setName(const Core::StringAtom& n) {
    this->name = n;
}

//------------------------------------------------------------------------------
template<class RESOURCE, class LOADER> const Core::StringAtom&
Factory<RESOURCE,LOADER>::GetName() const {
    return this->name;
}

//------------------------------------------------------------------------------
/**
 The NeedsSetup() method is needed when loading resources asynchronously.
 In this case the first call to Setup() will set the resource object
 to the Pending state, and a second call is needed to actually intialize
 the resource after the asynchronous load has finished. To determine when
 Setup must be called a second time, call the NeedsSetup() method.
 
*/
template<class RESOURCE, class LOADER> bool
Factory<RESOURCE,LOADER>::NeedsSetupResource(const RESOURCE& res) const {
    // this method must be implemented in a subclass, because
    // there may be different ways to determine when asynchronous
    // intialization has finished
}


//------------------------------------------------------------------------------
template<class RESOURCE, class LOADER> void
Factory<RESOURCE,LOADER>::AttachLoader(const Core::Ptr<LOADER>& loader) {
    // must override in Factory subclass matching the LOADER class,
    // otherwise we would need to do some dirty pointer casting
    o_error("Factory::AttachLoader() must be overridden in subclass!\n");
}

//------------------------------------------------------------------------------
/**
 Setup a resource object synchronously or asynchronously. The resource
 object must be in the Setup state when the method is first called (which
 means that a valid Setup object must be set in the resource object).
 After the Setup method is called, the resource must be in the
 Pending, Valid or Failed state. If the resource is in Pending state,
 the NeedsSetup() method must be called frequently, and if this
 returns true, the Setup() method must be called again to finish
 the asynchronous load. In this case, the resource must be in the
 Valid or Failed state after Setup returns.
*/
template<class RESOURCE, class LOADER> void
Factory<RESOURCE,LOADER>::SetupResource(RESOURCE& res) {

    const State::Code state = res.GetState();
    o_assert((State::Setup == state) || (State::Pending == state));
    
    // if the resource already has the loader index set, just call
    // the right loader, this should only happen when continouing to
    // load asynchronous resources
    int32 loaderIndex = res.getLoaderIndex();
    if (InvalidIndex != loaderIndex) {
        o_assert(State::Pending == state);
        this->loaders[loaderIndex]->Load(res);
        o_assert((res.GetState() == State::Pending) || (res.GetState() == State::Valid) || (res.GetState() == State::Failed));
        return;
    }
    else {
        // no loader yet, delegate to first loader which accepts the resource
        o_assert(State::Setup == state);
        for (loaderIndex = 0; loaderIndex < this->loaders.Size(); loaderIndex++) {
            if (this->loaders[loaderIndex]->Accepts(res)) {
                res.setLoaderIndex(loaderIndex);
                this->loaders[loaderIndex]->Load(res);
                o_assert((res.GetState() == State::Pending) || (res.GetState() == State::Valid) || (res.GetState() == State::Failed));
                return;
            }
        }
        // fallthrough: no suitable loader found
        Core::Log::Warn("Resource::Factory(%s): No suitable loader for resource '%s'\n",
                        this->name.AsCStr(), res.GetLocator().Location().AsCStr());
        res.setState(State::Failed);
    }
}

//------------------------------------------------------------------------------
/**
 This method works just like Setup, but takes an additional Ptr<Stream>
 with input data. This is useful when the resource should be initialized
 from data in memory instead of loading the input data through the IO system.
*/
template<class RESOURCE, class LOADER> void
Factory<RESOURCE,LOADER>::SetupResource(RESOURCE& res, const Core::Ptr<IO::Stream>& data) {

    const State::Code state = res.GetState();
    o_assert((State::Setup == state) || (State::Pending == state));
    
    int32 loaderIndex = res.getLoaderIndex();
    if (InvalidIndex != loaderIndex) {
        o_assert(State::Pending == state);
        this->loaders[loaderIndex]->Load(res, data);
        o_assert((res.GetState() == State::Valid) || (res.GetState() == State::Failed));
        return;
    }
    else {
        o_assert(State::Setup == state);
        for (loaderIndex = 0; loaderIndex < this->loaders.Size(); loaderIndex++) {
            if (this->loaders[loaderIndex]->Accepts(res, data)) {
                res.setLoaderIndex(loaderIndex);
                this->loaders[loaderIndex]->Load(res, data);
                o_assert((res.GetState() == State::Pending) || (res.GetState() == State::Valid) || (res.GetState() == State::Failed));
                return;
            }
        }
        // fallthrough: no suitable loader found
        Core::Log::Warn("Resource::Factory(%s): No suitable loader for resource '%s'\n", this->name.AsCStr(), res.GetLocator().Location().AsCStr());
        res.setState(State::Failed);
    }
}

//------------------------------------------------------------------------------
/**
 NOTE: The Destroy method must reset the resource to the Setup state.
*/
template<class RESOURCE, class LOADER> void
Factory<RESOURCE,LOADER>::DestroyResource(RESOURCE& res) {
    // implement in derived class
}

} // namespace Resource
} // namespace Oryol