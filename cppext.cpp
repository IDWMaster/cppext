#include "cppext.h"
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <map>
#include <atomic>
#include <thread>
#include <string.h>
#include <signal.h>
#include <sstream>
namespace System {
  
  
  class NullEvent:public Event {
    void Process(){};
  };
  
  
  
  
  
  
  

  
  
  
  
  
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
  
   class Timer:public AbstractTimer {
  public:
    std::shared_ptr<Event> function;
    std::shared_ptr<EventLoop> loop;
    bool interval;
    uint64_t ival; //interval value
    bool enabled;
    Timer(const std::shared_ptr<Event>& functor,std::chrono::steady_clock::time_point tint,const std::shared_ptr<EventLoop>& _loop):function(functor),loop(_loop) {
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
	function->Process();
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
  
 

 
  namespace IO {
    class IOReadyCallback {
    public:
      std::shared_ptr<System::EventLoop> loop;
      std::shared_ptr<System::IO::IOCallback> event;
      IOReadyCallback(const std::shared_ptr<System::EventLoop>& loop, const std::shared_ptr<System::IO::IOCallback>& event) {
	this->loop = loop;
	this->event = event;
	this->loop->AddRef();
      }
      ~IOReadyCallback() {
	this->loop->RemoveRef();
      }
      
    };
	class AsyncIOOperation {
	public:
		LPOVERLAPPED overlapped;
		HANDLE fd;
		void* buffer;
		size_t buffsz;
	};
    /**
     * Represents a backround I/O thread
     * */
    class IOLoop {
    public:
      std::thread* thread;
      bool running;
      HANDLE ntfyfd;
      std::map<AsyncIOOperation*,std::shared_ptr<IOReadyCallback>> callbacks;
      std::mutex mtx;
      IOLoop() {
		  HANDLE pipe; 
	std::wstringstream pipename;
	pipename<<L"\\\\.\\pipe\\";
	UUID pipeid;
	UuidCreate(&pipeid);
	RPC_WSTR str;
	UuidToStringW(&pipeid, &str);
	pipename<<str;
	RpcStringFreeW(&str);
	std::wstring fullname = pipename.str();
	//Create write end of pipe
	pipe = CreateNamedPipeW(fullname.data(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE, 2, 1, 1, 0, 0);


	ntfyfd = pipe;
	running = true;
	HANDLE fd = CreateFileW(fullname.data(), GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
	thread = new std::thread([=](){
		AsyncIOOperation* current = 0;
	  while(running) {
	    AsyncIOOperation** cblist;
	    size_t csz;
	    {
	      std::unique_lock<std::mutex> l(mtx);
	      
	      csz = callbacks.size()+1;
	      cblist = new AsyncIOOperation*[csz];
		  if (current == 0) {
			  cblist[0] = new AsyncIOOperation();
			  cblist[0]->overlapped = new OVERLAPPED();
			  memset(cblist[0]->overlapped, 0, sizeof(OVERLAPPED));
			  cblist[0]->overlapped->hEvent = CreateEventW(0, true, false, 0);
	      unsigned char mander;
			  ReadFile(fd, &mander, 1, 0, cblist[0]->overlapped);
			  current = cblist[0];
		  }
		  else {
			  cblist[0] = current;
		  }
	      size_t cpos = 1;
	      for(auto i = callbacks.begin();i!= callbacks.end();i++) {
		cblist[cpos] = i->first;
		cpos++;
	      }
	    }
		HANDLE* waithandles = new HANDLE[csz];
		for (size_t i = 0; i < csz; i++) {
			waithandles[i] = cblist[i]->overlapped->hEvent;
		}
		WaitForMultipleObjects(csz, waithandles, false, -1);
		delete[] waithandles;
		if (HasOverlappedIoCompleted(cblist[0]->overlapped)) {
			DWORD transferred;
			GetOverlappedResult(fd, cblist[0]->overlapped, &transferred, false);
			current = 0;
		}
	    for(size_t i = 1;i<csz;i++) {
	      if(HasOverlappedIoCompleted(cblist[i]->overlapped)) {
		//IO completion
		std::unique_lock<std::mutex> l(mtx); //It's a VERY unique lock!
		std::shared_ptr<IOReadyCallback> cb = callbacks[cblist[i]];
		callbacks.erase(cblist[i]);
		DWORD transferred = 0;
		bool s = GetOverlappedResult(cblist[i]->fd,cblist[i]->overlapped,&transferred,false);
		
		if(!s) {
		  cb->event->error = true;
		}else {
		  cb->event->outlen = (size_t)transferred;
		}
		CloseHandle(cblist[i]->overlapped->hEvent);
		delete cblist[i]->overlapped;
		delete cblist[i];
		cb->loop->Push(cb->event);
		
	      }
	    }
	    
	      delete[] cblist;
	  }
	});
      }
      void Ntfy() {
		  OVERLAPPED overlapped;
		  memset(&overlapped, 0, sizeof(overlapped));
	unsigned char mander;
	DWORD written;
	WriteFile(ntfyfd, &mander, 1, 0, &overlapped);
	GetOverlappedResult(ntfyfd, &overlapped, &written, true);
      }
      void AddFd(AsyncIOOperation* fd, const std::shared_ptr<System::EventLoop>& loop, const std::shared_ptr<System::IO::IOCallback>& event) {
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
    
    
    
    
    /**
     * Represents a Stream corresponding to a file descriptor
     * */
    class FileStream:public Stream, public std::enable_shared_from_this<FileStream> {
    public:
      std::shared_ptr<IOLoop> iol;
      std::shared_ptr<System::EventLoop> evl;
      HANDLE fd;
      uint64_t offset;
      /**
       * @summary Creates a new FileStream
       * @param fd The file descriptor -- must be opened in non-blocking mode.
       * */
      FileStream(HANDLE fd,  const std::shared_ptr<IOLoop>& loop, const std::shared_ptr<System::EventLoop>& evloop) {
	iol = loop;
	this->fd = fd;
	this->offset = 0;
	evl = evloop;
	
      }
      void Write(const void* buffer, size_t len, const std::shared_ptr<IOCallback>& callback) {
	AsyncIOOperation* req =  new AsyncIOOperation();
	req->overlapped = new OVERLAPPED();
	memset(req->overlapped,0,sizeof(OVERLAPPED));
	req->buffer = (void*)buffer;
	req->fd = fd;
	req->buffsz = len;
	req->overlapped->Offset = (uint32_t)offset;
	req->overlapped->OffsetHigh = (uint32_t)(offset >> 32); //WHY?!?!?!? This is just plain stupid. Why not use a 64-bit integer?
	std::shared_ptr<FileStream> thisptr = shared_from_this();
	WriteFile(fd, req->buffer, req->buffsz, 0, req->overlapped);
	iol->AddFd(req,evl,IOCB([=](const IOCallback& cb){
	  if(!cb.error) {
	    thisptr->offset+=cb.outlen;
	  }
	  callback->error = cb.error;
	  callback->outlen = cb.outlen;
	  callback->Process();
	}));
      }
      void Read(void* buffer, size_t len, const std::shared_ptr<IOCallback>& callback) {
		  AsyncIOOperation* req = new AsyncIOOperation();
		  req->overlapped = new OVERLAPPED();
		  memset(req->overlapped, 0, sizeof(OVERLAPPED));
		  req->buffer = (void*)buffer;
		  req->fd = fd;
		  req->buffsz = len;
		  req->overlapped->Offset = (uint32_t)offset;
		  req->overlapped->OffsetHigh = (uint32_t)(offset >> 32); //WHY?!?!?!? This is just plain stupid. Why not use a 64-bit integer?
	std::shared_ptr<FileStream> thisptr = shared_from_this();
		  ReadFile(fd, req->buffer, req->buffsz, 0, req->overlapped);
		  iol->AddFd(req, evl, IOCB([=](const IOCallback& cb) {
			  if (!cb.error) {
				  thisptr->offset += cb.outlen;
	  }
	  callback->error = cb.error;
	  callback->outlen = cb.outlen;
	  callback->Process();
	}));
      }
    };
    

void Stream::Pipe(const std::shared_ptr< Stream >& output, size_t bufflen)
{
	


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

  }
  
  static std::shared_ptr<IO::IOLoop> giol;
  
  class Runtime {
  public:
   std::shared_ptr<EventLoop> loop;
    std::shared_ptr<IO::IOLoop> iol;
    Runtime() {
      loop = std::make_shared<EventLoop>();
      iol = giol;
      
    }
    ~Runtime() {
      
    }
  };
  //TODO: Fix infinite recursion problem (infinite creation of threads). 
  thread_local Runtime runtime;
  class Initializer {
  public:
    Initializer() {
    
	giol = std::make_shared<IO::IOLoop>();
      std::thread mtr([=](){}); //Fix for pthreads issue
      mtr.join();
	  giol = std::make_shared<IO::IOLoop>();
    }
  };
  static Initializer lib_init;

std::shared_ptr< IO::Stream > IO::FD2S(HANDLE fd)
{

  return std::make_shared<FileStream>(fd,runtime.iol,runtime.loop);
}

 
   
std::shared_ptr<AbstractTimer> System::Internal::SetTimeout(const std::shared_ptr< Event >& event, size_t duration)
{
  
    //Compute desired time to completion by using steady_clock::now()+milliseconds
    
    std::shared_ptr<AbstractTimer> retval = std::make_shared<Timer>(event,std::chrono::steady_clock::now()+std::chrono::milliseconds(duration),runtime.loop);
    runtime.loop->Push(retval);
    return retval;
}

   
std::shared_ptr<AbstractTimer> System::Internal::SetInterval(const std::shared_ptr< Event >& event, size_t duration)
{
  
    //Compute desired time to completion by using steady_clock::now()+milliseconds
    
    std::shared_ptr<Timer> retval = std::make_shared<Timer>(event,std::chrono::steady_clock::now()+std::chrono::milliseconds(duration),runtime.loop);
    retval->interval = true;
    retval->ival = duration;
    
    runtime.loop->Push(std::shared_ptr<AbstractTimer>(retval));
    return retval;
}

void Enter()
{
  runtime.loop->Enter();
}

class InternalMessageEvent:public MessageEvent {
public:
  std::shared_ptr<MessageEvent> evt;
  std::shared_ptr<Message> msg;
  void Process(){
    evt->msg = msg;
    evt->Process();
  }
};

class MessageQueueInternal:public MessageQueue {
public:
  std::shared_ptr<MessageEvent> listener;
  std::shared_ptr<EventLoop> boundloop;
  void Post(const std::shared_ptr<Message>& msg) {
    std::shared_ptr<InternalMessageEvent> evt = std::make_shared<InternalMessageEvent>();
    evt->evt = listener;
    evt->msg = msg;
    boundloop->Push(evt);
  }
};

std::shared_ptr< MessageQueue > Internal::MakeQueue(const std::shared_ptr< MessageEvent >& evt)
{
  std::shared_ptr<MessageQueueInternal> retval = std::make_shared<MessageQueueInternal>();
  retval->listener = evt;
  retval->boundloop = runtime.loop;
  return retval;
}




}





