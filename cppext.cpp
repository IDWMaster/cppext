#include "cppext.h"
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
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>


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
	if(pendingEvents.size()) {
	  std::shared_ptr<Event>* evts = new std::shared_ptr<Event>[pendingEvents.size()];
	  size_t sz = pendingEvents.size();
	  for(size_t i = 0;i<sz;i++) {
	    evts[i] = pendingEvents.front();
	    pendingEvents.pop();
	  }
	  l.unlock();
	  for(size_t i = 0;i<sz;i++) {
	    evts[i]->Process();
	  }
	  l.lock();
	  
	  delete[] evts;
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
    
    class GenericIOCallback {
    public:
      std::shared_ptr<System::EventLoop> loop;
      std::shared_ptr<System::Event> event;
      GenericIOCallback(const std::shared_ptr<System::EventLoop>& loop, const std::shared_ptr<System::Event>& event) {
	this->loop = loop;
	this->event = event;
	this->loop->AddRef();
      }
      ~GenericIOCallback() {
	this->loop->RemoveRef();
      }
    };
    
    class NetIOLoop {
    public:
      int ntfyfd;
      std::thread* thread;
      std::mutex mtx;
      std::map<int,std::shared_ptr<GenericIOCallback>> callbacks;
      bool running;
      NetIOLoop() {
	int pipes[2];
	pipe(pipes);
	ntfyfd = pipes[1];
	running = true;
	this->thread = new std::thread([=](){
	  while(running) {
	    fd_set fds;
	    FD_ZERO(&fds);
	    FD_SET(pipes[0],&fds);
	    int highestfd = pipes[0];
	    std::vector<int> fdlist;
	    
	    {
	      std::unique_lock<std::mutex> l(mtx);
	      fdlist.resize(callbacks.size());
	      int pos = 0;
	      for(auto i = callbacks.begin(); i != callbacks.end();i++) {
		FD_SET(i->first,&fds);
		if(i->first>highestfd) {
		  highestfd = i->first;
		}
		fdlist[pos] = i->first;
		pos++;
	      }
	    }
	    int rval = select(highestfd+1,&fds,0,0,0);
	    if(rval == -1) {
	      continue;
	    }
	    
	    for(size_t i = 0;i<fdlist.size();i++) {
	      if(FD_ISSET(fdlist[i],&fds)) {
		std::unique_lock<std::mutex> l(mtx);
		std::shared_ptr<GenericIOCallback> iocb = callbacks[fdlist[i]];
		iocb->loop->Push(iocb->event);
		callbacks.erase(fdlist[i]);
	      }
	    }
	    if(FD_ISSET(pipes[0],&fds)) {
	      unsigned char mander;
	      read(pipes[0],&mander,1);
	    }
	  }
	  close(pipes[0]);
	});
      }
      void Ntfy() {
	unsigned char izard;
	write(ntfyfd,&izard,1);
      }
      void AddFD(int fd, const std::shared_ptr<GenericIOCallback>& evt) {
	std::unique_lock<std::mutex> l(mtx);
	callbacks[fd] = evt;
	Ntfy();
      }
      ~NetIOLoop() {
	running = false;
	Ntfy();
	thread->join();
	delete thread;
	close(ntfyfd);
	
      }
    };
    
    static NetIOLoop netloop;
    
    
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
	  struct aiocb* current = 0;
	  while(running) {
	    struct aiocb** cblist;
	    size_t csz;
	    {
	      std::unique_lock<std::mutex> l(mtx);
	      
	      csz = callbacks.size()+1;
	      cblist = new struct aiocb*[csz];
	      if(current == 0) {
	      cblist[0] = new struct aiocb();
	      memset(cblist[0],0,sizeof(cblist[0]));
	      unsigned char mander;
	      cblist[0]->aio_buf = &mander;
	      cblist[0]->aio_nbytes = 1;
	      cblist[0]->aio_fildes = fd;
	      aio_read(cblist[0]);
	      current = cblist[0];
	      }else {
		cblist[0] = current;
	      }
	      
	      timespec timeout;
	      timeout.tv_sec = -1;
	      size_t cpos = 1;
	      for(auto i = callbacks.begin();i!= callbacks.end();i++) {
		cblist[cpos] = i->first;
		cpos++;
	      }
	    }
	    aio_suspend(cblist,csz,0);
	    
	      if(aio_error(cblist[0]) != EINPROGRESS) {
		current = 0;
	      }
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
	  close(fd);
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
	close(ntfyfd);
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
    

void Stream::Pipe(const std::shared_ptr< Stream >& output, size_t bufflen)
{
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
  thread_local Runtime runtime;
  class Initializer {
  public:
    Initializer() {
    
	giol = std::make_shared<IO::IOLoop>();
      std::thread mtr([=](){}); //Fix for pthreads issue
      mtr.join();
      
    }
  };
  static Initializer lib_init;

std::shared_ptr< IO::Stream > IO::FD2S(int fd)
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

namespace Net {
IPAddress::IPAddress(const char* str)
{
  inet_pton(AF_INET6,str,raw);
}
IPAddress::IPAddress(const uint64_t* raw)
{
  this->raw[0] = raw[0]; //Can't get much more efficient than this; although maybe SIMD would be faster.
  this->raw[1] = raw[1];
}

class InternalUDPSocket:public UDPSocket {
public:
  int fd;
  InternalUDPSocket() {
    fd = socket(PF_INET6,SOCK_DGRAM,IPPROTO_UDP);
  }
  void Send(const void* buffer, size_t size, const IPEndpoint& ep) {
       sockaddr_in6 saddr;
      memset(&saddr,0,sizeof(saddr));
      saddr.sin6_family = AF_INET6;
      memcpy(&saddr.sin6_addr,ep.ip.raw,16);
      saddr.sin6_port = htons(ep.port);
      sendto(fd,buffer,size,0,(sockaddr*)&saddr,sizeof(saddr));
      
  }
   void Receive(void* buffer, size_t size, const std::shared_ptr< UDPCallback >& _cb) {
     runtime.loop->AddRef();
     System::IO::netloop.AddFD(fd,std::make_shared<System::IO::GenericIOCallback>(runtime.loop,F2E([=](){
       std::shared_ptr<UDPCallback> cb = _cb;
       sockaddr_in6 saddr;
      memset(&saddr,0,sizeof(saddr));
      saddr.sin6_family = AF_INET6;
      socklen_t slen = sizeof(saddr);
       int bytes = recvfrom(fd,buffer,size,0,(sockaddr*)&saddr,&slen);
       cb->error = bytes<0;
       cb->outlen = bytes;
       memcpy(cb->receivedFrom.ip.raw,&saddr.sin6_addr,16);
       cb->receivedFrom.port = ntohs(saddr.sin6_port);
       cb->Process();
       runtime.loop->RemoveRef();
    })));
   }
  void GetLocalEndpoint(IPEndpoint& out) {
    sockaddr_in6 saddr;
    memset(&saddr,0,sizeof(saddr));
    saddr.sin6_family = AF_INET6;
    socklen_t slen = sizeof(saddr);
    getsockname(fd,(sockaddr*)&saddr,&slen);
    out.port = ntohs(saddr.sin6_port);
    
    memcpy(out.ip.raw,&saddr.sin6_addr,16);
  }
  ~InternalUDPSocket(){
    close(fd);
  }
};

std::shared_ptr< UDPSocket > CreateUDPSocket(const IPEndpoint& ep)
{
  std::shared_ptr<InternalUDPSocket> retval = std::make_shared<InternalUDPSocket>();
  sockaddr_in6 addr;
  memset(&addr,0,sizeof(addr));
  memcpy(&addr.sin6_addr,ep.ip.raw,16);
  addr.sin6_port = htons(ep.port);
  addr.sin6_family = AF_INET6;
  bind(retval->fd,(sockaddr*)&addr,sizeof(addr));
  
  return retval;
}
std::shared_ptr< UDPSocket > CreateUDPSocket()
{
  
  std::shared_ptr<InternalUDPSocket> retval = std::make_shared<InternalUDPSocket>();
  sockaddr_in6 addr;
  memset(&addr,0,sizeof(addr));
  addr.sin6_addr = in6addr_any;
  addr.sin6_family = AF_INET6;
  addr.sin6_port = 0;
  bind(retval->fd,(sockaddr*)&addr,sizeof(addr));

  return retval;
}




}


}






