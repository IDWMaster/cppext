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
#include <aio.h>
#include <string.h>
#include <signal.h>
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
  namespace IO {
    class IOCallback:public System::Event {
    public:
      size_t outlen; //Output length (number of bytes processed)
      bool error; //Whether or not error occured.
      IOCallback() {
	outlen = 0;
	error = false;
      }
      virtual void Process() = 0;
    };
  }
  
  
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
      std::shared_ptr<System::IO::IOCallback> event;
      IOReadyCallback(const std::shared_ptr<System::EventLoop>& loop, const std::shared_ptr<System::IO::IOCallback>& event) {
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
      std::map<struct aiocb*,std::shared_ptr<IOReadyCallback>> callbacks;
      std::mutex mtx;
      IOLoop() {
	int pfds[2]; //read,write ends
	pipe(pfds);
	ntfyfd = pfds[1];
	running = true;
	int fd = pfds[0];
	thread = new std::thread([=](){
	  
	  while(running) {
	    struct aiocb** cblist;
	    size_t csz;
	    {
	      std::unique_lock<std::mutex> l(mtx);
	      
	      csz = callbacks.size()+1;
	      cblist = new struct aiocb*[csz];
	      cblist[0] = new struct aiocb();
	      memset(cblist[0],0,sizeof(cblist[0]));
	      unsigned char mander;
	      cblist[0]->aio_buf = &mander;
	      cblist[0]->aio_nbytes = 1;
	      cblist[0]->aio_fildes = fd;
	      aio_read(cblist[0]);
	      timespec timeout;
	      timeout.tv_sec = -1;
	      size_t cpos = 1;
	      for(auto i = callbacks.begin();i!= callbacks.end();i++) {
		cblist[cpos] = i->first;
		cpos++;
	      }
	    }
	    aio_suspend(cblist,csz,0);
	    for(size_t i = 1;i<csz;i++) {
	      if(aio_error(cblist[i]) != EINPROGRESS) {
		//IO completion
		std::unique_lock<std::mutex> l(mtx); //It's a VERY unique lock!
		std::shared_ptr<IOReadyCallback> cb = callbacks[cblist[i]];
		callbacks.erase(cblist[i]);
		ssize_t rval = aio_return(cblist[i]);
		if(rval == -1) {
		  cb->event->error = true;
		}else {
		  cb->event->outlen = (size_t)rval;
		}
		delete cblist[i];
		cb->loop->Push(cb->event);
		
	      }
	    }
	    
	      delete[] cblist;
	  }
	});
      }
      void Ntfy() {
	unsigned char mander;
	write(ntfyfd,&mander,1);
      }
      void AddFd(struct aiocb* fd, const std::shared_ptr<System::EventLoop>& loop, const std::shared_ptr<System::IO::IOCallback>& event) {
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
    
    template<typename T>
    class IOCallbackFunction:public IOCallback {
    public:
      T functor;
    IOCallbackFunction(const T& func):functor(func) {
      
      };
      void Process() {
	functor(*this);
      }
    };
    template<typename T>
    static std::shared_ptr<IOCallback> IOCB(const T& functor) {
      return std::make_shared<IOCallbackFunction<T>>(functor);
    }
    
    /**
     * A stream for performing file I/O
     * */
    class Stream {
    public:
      /**
       * @summary Asynchronously reads from a stream
       * @param buffer The buffer to store the data in
       * @param len The size of the buffer (max amount of data to be read)
       * @param callback The callback function to invoke when the operation completes
       * */
      virtual void Read(void* buffer, size_t len, const std::shared_ptr<IOCallback>& callback) = 0;
      /**
       * @summary Adds a new buffer into the ordered write queue for this stream
       * @param buffer The buffer to write to the stream
       * @param len The length of the buffer to write
       * @param callback A callback to invoke when the write operation completes
       * */
      virtual void Write(const void* buffer, size_t len, const std::shared_ptr<IOCallback>& callback) = 0;
      virtual void Pipe(const std::shared_ptr<Stream>& output, size_t bufflen = 4096){
	int pfds[2];
	pipe(pfds);
	unsigned char* buffer = new unsigned char[bufflen];
	std::shared_ptr<IOCallback>* cb = new std::shared_ptr<IOCallback>();
	*cb = IOCB([&](const IOCallback& info){
	  if(info.outlen == 0) {
	    //End of stream, close pipe
	    delete[] buffer;
	    delete cb;
	  }else {
	    Write(buffer,info.outlen,IOCB([=](const IOCallback& info){
		Read(buffer,bufflen,*cb);
	    }));
	  }
	});
      }
    };
    /**
     * Represents a Stream corresponding to a file descriptor
     * */
    class FileStream:public Stream, public std::enable_shared_from_this<FileStream> {
    public:
      std::shared_ptr<IOLoop> iol;
      std::shared_ptr<System::EventLoop> evl;
      int fd;
      uint64_t offset;
      /**
       * @summary Creates a new FileStream
       * @param fd The file descriptor -- must be opened in non-blocking mode.
       * */
      FileStream(int fd,  const std::shared_ptr<IOLoop>& loop, const std::shared_ptr<System::EventLoop>& evloop) {
	iol = loop;
	this->fd = fd;
	this->offset = 0;
	evl = evloop;
	
      }
      void Write(const void* buffer, size_t len, const std::shared_ptr<IOCallback>& callback) {
	struct aiocb* req = new struct aiocb();
	memset(req,0,sizeof(*req));
	req->aio_buf = (void*)buffer;
	req->aio_fildes = fd;
	req->aio_nbytes = len;
	req->aio_offset = offset;
	std::shared_ptr<FileStream> thisptr = shared_from_this();
	iol->AddFd(req,evl,IOCB([=](const IOCallback& cb){
	  if(!cb.error) {
	    thisptr->offset+=cb.outlen;
	  }
	  callback->error = cb.error;
	  callback->outlen = cb.outlen;
	  callback->Process();
	}));
	
	aio_write(req);
      }
      void Read(void* buffer, size_t len, const std::shared_ptr<IOCallback>& callback) {
	struct aiocb* req = new struct aiocb();
	memset(req,0,sizeof(*req));
	req->aio_buf = (void*)buffer;
	req->aio_fildes = fd;
	req->aio_nbytes = len;
	req->aio_offset = offset;
	
	std::shared_ptr<FileStream> thisptr = shared_from_this();
	iol->AddFd(req,evl,IOCB([=](const IOCallback& cb){
	  if(!cb.error) {
	    thisptr->offset+=cb.outlen;
	  }
	  callback->error = cb.error;
	  callback->outlen = cb.outlen;
	  callback->Process();
	}));
	aio_read(req);
      }
    };
    
  }
}

#endif
