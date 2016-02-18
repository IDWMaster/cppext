#ifndef __CPPEXT
#define __CPPEXT
#include <stdint.h>
#include <memory>
#include <chrono>
#include <string.h>



namespace System {
  
  
  class BStream {
public:
    unsigned char* ptr;
    size_t length;
    BStream(unsigned char* buffer, size_t sz) {
        this->ptr = buffer;
        this->length = sz;
    }
    unsigned char* Increment(size_t sz) {
    	unsigned char* retval = ptr;
    	if(sz>length) {
    		throw "up";
    	}
    	length-=sz;
    	ptr+=sz;
    	return retval;
    }
    void Read(unsigned char* buffer, size_t len) {
        if(len>length) {
            throw "up";
        }
        memcpy(buffer,ptr,len);
        ptr+=len;
        length-=len;
    }
    template<typename T>
    T& Read(T& val) {
        Read((unsigned char*)&val,sizeof(T));
        return val;
    }
    char* ReadString() {
        char* retval = (char*)ptr;
        char mander;
        while(Read(mander) != 0){}
        return retval;
    }
    template<typename T>
    void Write(const T& val){
      memcpy(ptr,&val,sizeof(val));
      ptr+=sizeof(val);
      length-=sizeof(val);
    }
    void Write(const char* str) {
      size_t slen = strlen(str);
      memcpy(ptr,str,slen);
      ptr+=slen;
      length-=slen;
    }
};
  
  
  
  namespace ABI {
    template<typename R, typename F, typename... args>
static R unsafe_c_callback(void* thisptr, args... a) {
	return (*((F*)thisptr))(a...);
}



template<typename F, typename... args, typename R>
static void* C(const F& callback, R(*&fptr)(void*, args...)) {
	fptr = unsafe_c_callback<R, F, args...>;
	return (void*)&callback;
}
  }
  
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
      virtual void Pipe(const std::shared_ptr<Stream>& output, size_t bufflen = 4096);
    };
    /**
     * @summary Converts a platform-specific file descriptor to a Stream
     * */
    std::shared_ptr<Stream> FD2S(int fd);
    
    
    
    }
    
    class Message {
    public:
      virtual ~Message(){};
    };
    class MessageEvent:public Event {
    public:
      std::shared_ptr<Message> msg;
      virtual ~MessageEvent(){};
    };
    
    template<typename T>
    class MessageEventFunction:public MessageEvent {
    public:
      T function;
      MessageEventFunction(const T& func):function(func){}
      void Process() {
	function(msg);
      }
    };
    
    class MessageQueue {
    public:
      virtual void Post(const std::shared_ptr<Message>& message) = 0;
      virtual ~MessageQueue(){};
    };
    namespace Internal {
      std::shared_ptr<MessageQueue> MakeQueue(const std::shared_ptr<MessageEvent>& evt);
    }
    template<typename T>
    static std::shared_ptr<MessageQueue> MakeQueue(const T& callback) {
      return Internal::MakeQueue(std::make_shared<MessageEventFunction<T>>(callback));
    }
    namespace Net {
      /**
       * @summary Represents an IPv6 (Internet Protocol Version 6) address
       * */
      class IPAddress {
      public:
	/**
	 * @summary The raw IPv6 address
	 * */
	uint64_t raw[2];
	/**
	 * @summary Creates an IPv6 address from a string
	 * */
	IPAddress(const char* str);
	IPAddress(const uint64_t* raw);
	IPAddress(){};
      };
      class IPEndpoint {
      public:
	IPAddress ip;
	uint16_t port;
	bool operator<(const IPEndpoint& other) const {
	  return memcmp(this,&other,sizeof(other)) < 0;
	}
	IPEndpoint() {
	  
	}
	
      };
      class UDPCallback:public System::IO::IOCallback {
      public:
	IPEndpoint receivedFrom;
	virtual ~UDPCallback(){};
      };
      class UDPSocket {
      public:
	virtual void GetLocalEndpoint(IPEndpoint& out) = 0;
	virtual void Send(const void* buffer, size_t size, const IPEndpoint& ep) = 0;
	virtual void Receive(void* buffer, size_t size, const std::shared_ptr<UDPCallback>& cb) = 0;
	virtual ~UDPSocket(){};
      };
      
      std::shared_ptr<UDPSocket> CreateUDPSocket();
      std::shared_ptr<UDPSocket> CreateUDPSocket(const IPEndpoint& ep);
      template<typename T>
      class UDPCallbackFunction:public UDPCallback {
      public:
	T func;
	UDPCallbackFunction(const T& functor):func(functor) {
	  
	}
	void Process() {
	  func(*this);
	}
      };
      template<typename T>
      std::shared_ptr<UDPCallback> F2UDPCB(const T& functor) {
	return std::make_shared<UDPCallbackFunction<T>>(functor);
      }
      
    }
}

#endif
