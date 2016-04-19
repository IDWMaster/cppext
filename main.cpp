#include "cppext.h"
#include <thread>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>


class CustomMessage:public System::Message {
public:
  std::string txt;
  CustomMessage(const std::string& msg) {
    this->txt = msg;
  }
};

int main(int argc, char** argv) {
  
  
  std::shared_ptr<System::MessageQueue> inbox = System::MakeQueue([=](const std::shared_ptr<System::Message>& msg){
    std::string safestr = ((CustomMessage*)msg.get())->txt;
    printf("%s",safestr.data());
  });
  
  
  System::SetTimeout([&](){
    printf("1 second elapsed\n");
  },1000);
  size_t count = 4;
  std::shared_ptr<System::AbstractTimer> ival;
  ival = System::SetInterval([&](){
    printf("200 milliseconds elapsed\n");
    count--;
    if(count == 0) {
      ival->Cancel();
    }
  },200);
  
  int testfd = open("testfile",O_RDWR | O_CREAT,S_IRUSR | S_IWUSR);
  const char* buffy = "Hi world!";
  
  
  char mander[1024];
  memset(mander,0,1024);
  std::shared_ptr<System::IO::Stream> str = System::IO::FD2S(testfd);
  str->Write(buffy,strlen(buffy),System::IO::IOCB([&](const System::IO::IOCallback& cb){
    printf("AIO completed, error = %i, written = %i\n",(int)cb.error,(int)cb.outlen);
    std::shared_ptr<System::IO::Stream> readstr = System::IO::FD2S(testfd);
      readstr->Read(mander,1024,System::IO::IOCB([&](const System::IO::IOCallback& cb){
      printf("Read %i bytes\n%s\n",(int)cb.outlen,mander);
    }));
  }));
  
  std::thread worker([&](){
    inbox->Post(std::make_shared<CustomMessage>("This is a message from another thread\n"));
    //This is an example of a worker thread, having its own event loop.
    System::SetTimeout([](){
      printf("2 seconds elapsed, on worker thread\n");
    },2000);
    System::Enter();
    printf("Worker thread exited\n");
  });
  
  std::shared_ptr<System::Net::UDPSocket> s = System::Net::CreateUDPSocket();
  unsigned char recvbuff[1024];
  memset(recvbuff,0,1024);
  unsigned char* rptr = recvbuff;
  s->Receive(recvbuff,1024,System::Net::F2UDPCB([=](System::Net::UDPCallback& bot){
    printf("Network client (%i byte message) says: %s\n",(int)bot.outlen,(char*)rptr);
  }));
  System::Net::IPEndpoint ep;
  s->GetLocalEndpoint(ep);
  
  ep.ip = "::1";
  s->Send("Hi world!",9,ep);
  
  System::Net::IPEndpoint serverAddr;
serverAddr.ip = "::";
serverAddr.port = 0;
char tcpbuffer[256];
  memset(tcpbuffer,0,256);
std::shared_ptr<System::Net::TCPServer> tcpServer = System::Net::CreateTCPServer(serverAddr,System::Net::F2TCPCB([&](const std::shared_ptr<System::IO::Stream>& clientStr, const System::Net::IPEndpoint& clientAddr){
  char* tcp_ptr = tcpbuffer;
  clientStr->Read(tcpbuffer,256,System::IO::IOCB([=](const System::IO::IOCallback& cb){
    printf("From TCP client (%i bytes): %s\n",(int)cb.outlen,tcp_ptr);
  }));
}));
tcpServer->GetLocalEndpoint(serverAddr);

System::Net::ConnectToServer(serverAddr,System::Net::F2TCPCB([&](const std::shared_ptr<System::IO::Stream>& str, const System::Net::IPEndpoint& addr){
  const char* mander = "Hi NSA!";
  str->Write(mander,strlen(mander),System::IO::IOCB([=](const System::IO::IOCallback& cbinfo){
    //Hold a reference to our stream (otherwise we could close before the message gets sent)
    
    std::shared_ptr<System::IO::Stream> sref = str;
  }));
}));

  
  
  System::Enter();
  printf("Main thread exited\n");
  worker.join();
return 0;
}
 
