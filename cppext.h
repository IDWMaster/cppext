#ifndef __CPPEXT
#define __CPPEXT
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <map>
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
    uint64_t timeout;
    virtual void Cancel() = 0; //Cancels this timer
    virtual ~AbstractTimer(){};
  };
  
  template<typename F, typename L>
  class Timer:public AbstractTimer {
  public:
    F function;
    std::shared_ptr<L> loop;
    Timer(const F& functor,uint64_t tint,const std::shared_ptr<L>& _loop):function(functor),loop(_loop) {
      timeout = tint;
    }
    void Process() {
      function();
    }
  };
  
  
  template<typename F>
  static std::shared_ptr<Event> MakeCallbackFunction(const F& functor) {
    return std::make_shared<CallbackFunction<F>>(functor);
  }
  template<typename F, typename L>
  static std::shared_ptr<AbstractTimer> SetTimeout(const F& functor, uint64_t milliseconds, std::shared_ptr<L> loop) {    
    std::shared_ptr<AbstractTimer> retval = std::make_shared<Timer<F,L>>(functor,loop);
    loop->Push(retval);
    return retval;
  }
  
  
  
  /**
   * A thread-local event loop
   * */
  class EventLoop {
  public:
    
    std::mutex pendingEvents_mutex; //Mutex protecting pendingEvents
    std::condition_variable evt;
    std::queue<std::shared_ptr<Event>> pendingEvents; //Pending events for this loop
    
    EventLoop() {
      running = false;
      
    }
    void Push(const std::shared_ptr<AbstractTimer>& timer) {
      //TODO: Register timer
      
    }
    void Push(const std::shared_ptr<Event>& evt) {
      std::unique_lock<std::mutex> l(pendingEvents_mutex);
      pendingEvents.push(evt);
      l.unlock();
      this->evt.notify_one();
    }
    bool running;
    /**
     * @summary Exits the event loop. Must be called from the current owner of the loop.
     * */
    void Exit() {
      running = false;
    }
    /**
     * @summary Enters the event loop. The calling thread will become the owner of this event loop.
     * */
    void Enter() {
      running = true;
      while(running) {
	std::mutex mtx;
	std::unique_lock<std::mutex> l(pendingEvents_mutex);
	while(pendingEvents.size()) {
	  std::shared_ptr<Event> evt = pendingEvents.front();
	  pendingEvents.pop();
	  evt->Process();
	}
	if(running) {
	  this->evt.wait(l);
	}
      }
    }
  };
  
  
  
}

#endif
