#ifndef __CPPEXT
#define __CPPEXT
#include <stdint.h>
#include <memory>
#include <chrono>
#include <string.h>
#include <queue>
#include <mutex>


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
      void Write(const void* data, size_t len) {
          if(len>length) {
              throw "up";
          }
          memcpy(ptr,data,len);
          ptr+=len;
          length-=len;
      }
    template<typename T>
    void Write(const T& val){
      Write(&val,sizeof(val));
    }
    void Write(const char* str) {
      size_t slen = strlen(str);
      Write(str,slen);
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
	IPAddress(const IPAddress& other) {
	  raw[0] = other.raw[0];
	  raw[1] = other.raw[1];
	}
	IPAddress(){};
	void ToString(char* out) const;
      };
      class IPEndpoint {
      public:
	IPAddress ip;
	uint16_t port;
	
	bool operator<(const IPEndpoint& other) const {
	  uint64_t cop[3];
	  cop[0] = ip.raw[0];
	  cop[1] = ip.raw[1];
	  cop[2] = (uint64_t)port;
	  uint64_t bop[3];
	  bop[0] = other.ip.raw[0];
	  bop[1] = other.ip.raw[1];
	  bop[2] = (uint64_t)other.port;
	  
	  
	  return memcmp(cop,bop,3*8) < 0;
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
	virtual void JoinMulticastGroup(const IPAddress& group) = 0;
	virtual ~UDPSocket(){};
      };
      
      class TCPServer {
      public:
	virtual void GetLocalEndpoint(IPEndpoint& out) = 0;
	virtual ~TCPServer(){};
      };
      class IPCServer {
      public:
	virtual ~IPCServer(){};
      };
      class IPCConnectCallback {
      public:
	virtual void Process(const std::shared_ptr<System::IO::Stream>& str) = 0;
	virtual ~IPCConnectCallback(){}
      };
      /**
       * @summary Creates an IPC server used for communication with processes on the local computer
       * @param name The path on the filesystem to create the IPC channel at.
       * @param onProcessConnect A callback which is invoked when a process connects to this IPC channel
       * */
      std::shared_ptr<IPCServer> CreateIPCServer(const char* name, const std::shared_ptr<IPCConnectCallback>& onProcessConnect);
      class TCPConnectCallback {
      public:
	virtual void Process(const std::shared_ptr<System::IO::Stream>& str, const IPEndpoint& ep) = 0;
	virtual ~TCPConnectCallback(){};
      };
      template<typename T>
      class IPCConnectCallbackFunction:public IPCConnectCallback {
      public:
	T functor;
	IPCConnectCallbackFunction(const T& func):functor(func) {
	}
	void Process(const std::shared_ptr< IO::Stream >& str) {
	  functor(str);
	}
      };
      std::shared_ptr<TCPServer> CreateTCPServer(const IPEndpoint& ep, const std::shared_ptr<TCPConnectCallback>& onClientConnect);
      void ConnectToServer(const IPEndpoint& ep, const std::shared_ptr<TCPConnectCallback>& cb);
      void ConnectToServer(const char* path, const std::shared_ptr<IPCConnectCallback>& cb);
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
      class TCPCallbackFunction:public TCPConnectCallback {
      public:
	T func;
	TCPCallbackFunction(const T& functor):func(functor) {
	}
	void Process(const std::shared_ptr<System::IO::Stream>& str, const IPEndpoint& ep) {
	  func(str,ep);
	}
      };
      template<typename T>
      std::shared_ptr<UDPCallback> F2UDPCB(const T& functor) {
	return std::make_shared<UDPCallbackFunction<T>>(functor);
      }
      template<typename T>
      std::shared_ptr<TCPConnectCallback> F2TCPCB(const T& functor) {
	return std::make_shared<TCPCallbackFunction<T>>(functor);
      }
      template<typename T>
      std::shared_ptr<IPCConnectCallback> F2IPCCB(const T& functor) {
	return std::make_shared<IPCConnectCallbackFunction<T>>(functor);
      }
    }


    class Packet {
    public:
        unsigned char* data;
        size_t len;
        size_t offset;
        Packet(unsigned char* data, size_t len) {
            this->data = data;
            this->len = len;
            offset = 0;
        }
    };


    class DispatchMessage:public System::Message {
    public:
        std::shared_ptr<System::Event> cb;
        DispatchMessage(const std::shared_ptr<System::Event>& cb):cb(cb){

        }
    };




    /**
     * UNSTABLE/Experimental -- Do not use: A PushStream is a thread-safe stream capable of dispatching binary data directly into a System::Stream object. This stream is read-only.
     * @brief The PushStream class
     */
    class PushStream:public System::IO::Stream {
    public:
        std::queue<Packet> packets;
        std::shared_ptr<System::IO::IOCallback> cb;
        unsigned char* buffy;
        size_t len;
        std::shared_ptr<System::MessageQueue> q;
        PushStream() {
            q = System::MakeQueue([=](const std::shared_ptr<System::Message>& msg){
                ((DispatchMessage*)msg.get())->cb->Process();
            });
        }
        ~PushStream() {
            while(packets.size()) {
                Packet packet = packets.front();
                packets.pop();
                delete[] packet.data;
            }
        }
std::mutex mtx;
bool pushing;
        /**
         * @brief Push Pushes data into the stream. This method is thread-safe.
         * @param buffy The buffer to push into the stream (owned by the caller)
         * @param len The length of data to push into the stream.
         */
        void Push(const void* __buffy, size_t len) {
            unsigned char* buffy = 0;

            std::unique_lock<std::mutex> l(mtx);
            if(__buffy) {
                buffy =new unsigned char[len];
                memcpy(buffy,__buffy,len);
                packets.push(Packet(buffy,len));
            }
            q->Post(std::make_shared<DispatchMessage>(F2E([=](){
                    std::unique_lock<std::mutex> l(mtx);

                    if(this->cb) {
                        std::shared_ptr<System::IO::IOCallback> cb = this->cb;
                        this->cb = 0;
                        size_t totalRead = 0;
                        while(packets.size() && this->len) {
                            Packet packet = packets.front();
                            size_t avail = packet.len;
                            if(avail>this->len) {
                                avail = this->len;
                            }
                            memcpy(this->buffy,packet.data+packet.offset,avail);
                            packet.offset+=avail;
                            totalRead+=avail;
                            this->buffy+=avail;
                            packet.len-=avail;
                            this->len-=avail;
                            if(!packet.len) {
                                delete[] packet.data;
                                packets.pop();
                            }
                        }
                        cb->error = false;
                        cb->outlen = totalRead;
                        if(totalRead) {
                            cb->Process();
                        }
                    }
            })));

        }

        void Read(void* buffy, size_t len, const std::shared_ptr<System::IO::IOCallback>& cb) {
            if(this->cb) {
                throw "Overlapped error";
            }
            this->cb = cb;
            this->len = len;
            this->buffy = (unsigned char*)buffy;
            if(len && packets.size()) {
                Push(0,0);
            }
        }
        void Write(const void* buffy, size_t len, const std::shared_ptr<System::IO::IOCallback>& cb) {
            //Operation not supported
            cb->outlen = 0;
            cb->error = true;
            cb->Process();
        }
    };

}

#endif
