#include "cppext.h"
#include <thread>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char** argv) {
  
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
  
  
  std::shared_ptr<System::IO::Stream> str = System::IO::FD2S(testfd);
  str->Write(buffy,strlen(buffy),System::IO::IOCB([&](const System::IO::IOCallback& cb){
    printf("AIO completed, error = %i, written = %i\n",(int)cb.error,(int)cb.outlen);
  }));
  char mander[1024];
  memset(mander,0,1024);
  str->Read(mander,1024,System::IO::IOCB([&](const System::IO::IOCallback& cb){
    printf("Read %i bytes\n%s\n",(int)cb.outlen,mander);
  }));
  
  
  
  std::thread worker([&](){
    //This is an example of a worker thread, having its own event loop.
    System::SetTimeout([](){
      printf("2 seconds elapsed, on worker thread\n");
    },2000);
    System::Enter();
    printf("Worker thread exited\n");
  });
  
  
  System::Enter();
  printf("Main thread exited\n");
  worker.join();
return 0;
}
 
