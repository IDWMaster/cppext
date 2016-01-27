#include "cppext.h"
#include <thread>
int main(int argc, char** argv) {
  std::shared_ptr<System::EventLoop> loop = std::make_shared<System::EventLoop>(); //Main event loop
  std::thread lthread([&](){
    loop->Enter();
  });
  loop->Push(System::MakeCallbackFunction([&](){
    printf("Hello world from the event loop!\n");
    loop->Exit();
  }));
  
  
  
lthread.join();
return 0;
}
 