#ifndef __CPPEXT
#define __CPPEXT
#include <stdint.h>
#include <memory>
#include <chrono>
namespace System {
  class Event {
  public:
    virtual ~Event(){};
    /**
     * This method is invoked when an Event is processed
     * */
    virtual void Process() = 0;
  };
  /**
   * @summary A raw callback function
   * */
  template<typename F>
  class CallbackFunction:public Event {
  public:
    F function;
    CallbackFunction(const F& functor):function(functor) {
      
    }
    void Process() {
      function();
    }
  };
  class AbstractTimer:public Event {
  public:
    std::chrono::steady_clock::time_point timeout;
    virtual void Cancel() = 0; //Cancels this timer
    virtual ~AbstractTimer(){};
  };
 
  
  template<typename F>
  static std::shared_ptr<Event> F2E(const F& functor) {
    return std::make_shared<CallbackFunction<F>>(functor);
  }
  
  
  
  namespace Internal {
    std::shared_ptr<AbstractTimer> SetTimeout(const std::shared_ptr<Event>& functor, size_t duration);
    std::shared_ptr<AbstractTimer> SetInterval(const std::shared_ptr<Event>& functor, size_t duration);
    
  }
  
    static std::shared_ptr<AbstractTimer> SetTimeout(const std::shared_ptr<Event>& functor, size_t duration) {
      return Internal::SetTimeout(functor,duration);
      
    }
    static std::shared_ptr<AbstractTimer> SetInterval(const std::shared_ptr<Event>& functor, size_t duration) {
      return Internal::SetInterval(functor,duration);
      
    }
    template<typename T>
    static std::shared_ptr<AbstractTimer> SetInterval(const T& functor, size_t duration) {
      return Internal::SetInterval(F2E(functor),duration);
      
    }
    template<typename T>
    static std::shared_ptr<AbstractTimer> SetTimeout(const T& functor, size_t duration) {
      return SetTimeout(F2E(functor),duration);
    }
    /**
     * Enters the calling thread into an event loop.
     * */
    void Enter();
}

#endif
