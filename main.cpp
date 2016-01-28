#include "cppext.h"
#include <thread>
int main(int argc, char** argv) {
  std::shared_ptr<System::EventLoop> loop = std::make_shared<System::EventLoop>(); //Main event loop
  std::thread lthread([&](){
    //loop->Enter();
  });
lthread.join(); //Needed to initialize pthreads infrastructure. Otherwise stuff breaks.  
  
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
  loop->Enter();
  

return 0;
}
 