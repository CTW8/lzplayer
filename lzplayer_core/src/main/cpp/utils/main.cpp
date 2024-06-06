#include <iostream>
#include "ALooper.h"
#include "AMessage.h"
#include "AHandler.h"

class HandleTest:public android::AHandler{
    void onMessageReceived(const std::shared_ptr<android::AMessage> &msg){
        ALOGE("what:%d\n",msg->what());
    }
};

int main() {
    ALOGD("start test");
    std::shared_ptr<android::ALooper> looper = std::make_shared<android::ALooper>();
    looper->start(false);
    ALOGE("start loop");
    std::shared_ptr<HandleTest> test = std::make_shared<HandleTest>();

    looper->registerHandler(test);

    std::shared_ptr<android::AMessage> msg = std::make_shared<android::AMessage>(32,test);
    ALOGE("post message");
    msg->post();

    system("read -p \"Press [Enter] key to exit...\"");
    return 0;
}