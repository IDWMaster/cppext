#include "cppext.h"
#include <thread>
#include <fcntl.h>


int main(int argc, char** argv) {
  std::shared_ptr<System::EventLoop> loop = std::make_shared<System::EventLoop>(); //Main event loop
  std::shared_ptr<System::IO::IOLoop> ioloop = std::make_shared<System::IO::IOLoop>();
  std::thread lthread([&](){
    //loop->Enter();
  });
lthread.join(); //Needed to initialize pthreads infrastructure. Otherwise stuff breaks.  

//TODO: Async IO with pselect on dedicated I/O thread
//http://linux.die.net/man/2/select
  
  System::SetTimeout([&](){
    printf("1 second elapsed\n");
  },1000,loop);
  size_t count = 4;
  std::shared_ptr<System::AbstractTimer> ival;
  ival = System::SetInterval([&](){
    printf("200 milliseconds elapsed\n");
    count--;
    if(count == 0) {
      ival->Cancel();
    }
  },200,loop);
  
  loop->AddRef(); //Increment reference count to perform async I/O
  int testfd = open("testfile",O_RDWR | O_CREAT,S_IRUSR | S_IWUSR);
  const char* buffy = "Hi world!";
  
  
  std::shared_ptr<System::IO::FileStream> str = std::make_shared<System::IO::FileStream>(testfd,ioloop,loop);
  str->Write(buffy,strlen(buffy),System::IO::IOCB([&](const System::IO::IOCallback& cb){
    printf("AIO completed, error = %i, written = %i\n",(int)cb.error,(int)cb.outlen);
  }));
  char mander[1024];
  memset(mander,0,1024);
  str->Read(mander,1024,System::IO::IOCB([&](const System::IO::IOCallback& cb){
    printf("Read %i bytes\n%s\n",(int)cb.outlen,mander);
  }));
  //printf("Sent write request\n");
  
  
  loop->Enter();
  
return 0;
}
 
