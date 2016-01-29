#ifndef __CPPEXT
#define __CPPEXT
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <map>
#include <atomic>
#include <thread>
#include <unistd.h>
namespace System {
  
  
  class Event {
  public:
    virtual ~Event(){};
    /**
     * This method is invoked when an Event is processed
     * */
    virtual void Process() = 0;
  };
  class NullEvent:public Event {
    void Process(){};
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
  
  template<typename F, typename L>
  class Timer:public AbstractTimer {
  public:
    F function;
    std::shared_ptr<L> loop;
    bool interval;
    uint64_t ival; //interval value
    bool enabled;
    Timer(const F& functor,std::chrono::steady_clock::time_point tint,const std::shared_ptr<L>& _loop):function(functor),loop(_loop) {
      timeout = tint;
      loop->AddRef();
      interval = false;
      enabled = true;
    }
    
    void Cancel() {
      enabled = false;
      loop->RemoveRef();
    }
    void Process() {
	loop->_internal_reregister_timer = false;
      if(enabled) {
	function();
	if(interval && enabled) {
	  timeout = std::chrono::steady_clock::now()+std::chrono::milliseconds(ival);
	  loop->_internal_reregister_timer = true;
	}
      }
    }
    ~Timer() {
      if(enabled) {
	loop->RemoveRef();
      }
    }
  };
  
  
  template<typename F>
  static std::shared_ptr<Event> MakeCallbackFunction(const F& functor) {
    return std::make_shared<CallbackFunction<F>>(functor);
  }
  template<typename F, typename L>
  static std::shared_ptr<AbstractTimer> SetTimeout(const F& functor, uint64_t milliseconds, std::shared_ptr<L> loop) {  
    //TODO: Implement timeouts using chrono::ateady_clock
    
    //Compute desired time to completion by using steady_clock::now()+milliseconds
    
    std::shared_ptr<AbstractTimer> retval = std::make_shared<Timer<F,L>>(functor,std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds),loop);
    loop->Push(retval);
    return retval;
  }
  template<typename F, typename L>
  static std::shared_ptr<AbstractTimer> SetInterval(const F& functor, uint64_t milliseconds, std::shared_ptr<L> loop) {  
    //TODO: Implement timeouts using chrono::ateady_clock
    
    //Compute desired time to completion by using steady_clock::now()+milliseconds
    
    std::shared_ptr<Timer<F,L>> retval = std::make_shared<Timer<F,L>>(functor,std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds),loop);
    retval->interval = true;
    retval->ival = milliseconds;
    loop->Push(std::shared_ptr<AbstractTimer>(retval));
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
    std::map<std::chrono::steady_clock::time_point,std::vector<std::shared_ptr<AbstractTimer>>> timers;
    EventLoop() {
      running = false;
      refcount = 0;
    }
    std::atomic<size_t> refcount;
    void AddRef() {
      
      refcount++;
    }
    void RemoveRef() {
      refcount--;
    }
    
    bool _internal_reregister_timer;
    
    
    void Push(const std::shared_ptr<AbstractTimer>& timer) {
      //TODO: Register timer
      std::chrono::steady_clock::time_point timeout = timer->timeout;
      std::unique_lock<std::mutex> l(pendingEvents_mutex);
      if(timers.find(timeout) == timers.end()) {
	std::vector<std::shared_ptr<AbstractTimer>> vect;
	timers[timeout] = vect;
      }
      timers[timeout].push_back(timer);
      l.unlock();
      Push(std::make_shared<NullEvent>()); //Force update to timers
      
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
      while(running && refcount) {
	std::mutex mtx;
	std::unique_lock<std::mutex> l(pendingEvents_mutex);
	while(pendingEvents.size()) {
	  std::shared_ptr<Event> evt = pendingEvents.front();
	  pendingEvents.pop();
	  evt->Process();
	}
	if(running && refcount) {
	  //Check for timers
	  if(timers.size()) {
	    //get timer
	    
	    std::chrono::steady_clock::time_point point = timers.begin()->first;
	    std::vector<std::shared_ptr<AbstractTimer>> ctimers = timers.begin()->second;
	    //Invoke all timers
	    auto invoke = [&](){
	      std::shared_ptr<AbstractTimer>* ctimers_raw = ctimers.data();
	      size_t len = ctimers.size();
	      for(size_t i = 0;i<len;i++) {
		_internal_reregister_timer = false;
		ctimers_raw[i]->Process();
		if(_internal_reregister_timer) {
		  Push(ctimers_raw[i]);
		}
	      }
	      timers.erase(point);
	      
	    };
	    if(std::chrono::steady_clock::now()>=point) {
	      //Invoke immediately
	      l.unlock();
	      invoke();
	    }else {
	      if(this->evt.wait_for(l,point-std::chrono::steady_clock::now()) == std::cv_status::timeout) {
		l.unlock();
		invoke();
	      }
	      
	    }
	  }else {
	    this->evt.wait(l);
	  }
	}
      }
    }
  };
  
  
  namespace IO {
    class IOReadyCallback {
    public:
      std::shared_ptr<System::EventLoop> loop;
      std::shared_ptr<System::Event> event;
      IOReadyCallback(const std::shared_ptr<System::EventLoop>& loop, const std::shared_ptr<System::Event>& event) {
	this->loop = loop;
	this->event = event;
      }
      
    };
    /**
     * Represents a backround I/O thread
     * */
    class IOLoop {
    public:
      std::thread* thread;
      bool running;
      int ntfyfd;
      std::map<int,std::shared_ptr<IOReadyCallback>> callbacks;
      std::mutex mtx;
      IOLoop() {
	int pfds[2]; //read,write ends
	pipe(pfds);
	ntfyfd = pfds[1];
	running = true;
	thread = new std::thread([=](){
	  
	  while(running) {
	    
	  int fdmaxplus1 = 0;
	  fd_set fds;
	  FD_ZERO(&fds);
	  
	  int fd = pfds[0]; //Notification fd
	  fdmaxplus1 = fd+1;
	  FD_SET(fd,&fds);
	  {
	    std::unique_lock<std::mutex> l(mtx);
	    for(auto i = callbacks.begin();i!= callbacks.end();i++) {
	      if((i->first)+1>fdmaxplus1) {
		fdmaxplus1 = i->first+1;
	      }
	      FD_SET(i->first,&fds);
	    }
	  }
	  struct timeval tv;
	  FD_SET(fd,&fds);
	    tv.tv_sec = -1;
	    if(select(fdmaxplus1,&fds,0,0,&tv) != -1) {
	      {
		if(FD_ISSET(fd,&fds)) {
		  unsigned char izard;
		  read(fd,&izard,1);
		}
		std::unique_lock<std::mutex> l(mtx);
		for(auto i = callbacks.begin();i!= callbacks.end();i++) {
		  if(FD_ISSET(i->first,&fds)) {
		    //Post to event queue
		    i->second->loop->Push(i->second->event);
		  }
		}
	      }
	    }
	    
	  }
	});
      }
      void Ntfy() {
	unsigned char mander;
	write(ntfyfd,&mander,1);
      }
      void AddFd(int fd, const std::shared_ptr<System::EventLoop>& loop, const std::shared_ptr<System::Event>& event) {
	    std::unique_lock<std::mutex> l(mtx);
	    std::shared_ptr<IOReadyCallback> cb = std::make_shared<IOReadyCallback>(loop,event);
	    callbacks[fd] = cb;
	    Ntfy();
      }
      ~IOLoop() {
	running = false;
	Ntfy();
	thread->join();
	
      }
    };
  }
}

#endif
