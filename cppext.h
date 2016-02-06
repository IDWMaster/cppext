#ifndef __CPPEXT
#define __CPPEXT
#include <stdint.h>
#include <memory>
#include <chrono>
#include <Windows.h>
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
    std::shared_ptr<Stream> FD2S(HANDLE fd);
    
    
    
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
    
}

#endif