#include <iostream>
#include "ALooper.h"
#include "AMessage.h"
#include "AHandler.h"

class HelloWorld{
public:
    HelloWorld(){};
    HelloWorld(int a,std::string s,int len){
        buf = (uint8_t*)malloc(len);
        for(int i=0;i<len;i++){
            buf[i] = i;
        }
        str = s;
        size = len;
    }
    ~HelloWorld(){
        if(buf){
            free(buf);
        }
    };

    void show(){
        ALOGE("val:%d  str:%s",val,str.c_str());
        for(int i=0;i<size;i++){
            ALOGE("%d ",buf[i]);
        }
    }

private:
    int val;
    std::string str;
    uint8_t *buf;
    int size;
};

class HandleTest:public android::AHandler{
    void onMessageReceived(const std::shared_ptr<android::AMessage> &msg){
        
        std::shared_ptr<android::AReplyToken> token;
        msg->senderAwaitsResponse(token);
        std::string str;
        msg->findString("test",str);
        ALOGE("what:%d  str:%s\n",msg->what(),str.c_str());

        std::shared_ptr<android::AMessage> respon = std::make_shared<android::AMessage>();
        respon->setWhat(44);
        respon->setString("hello","world x");
        std::shared_ptr<HelloWorld> hi = std::make_shared<HelloWorld>(77,"hello test",10);
        respon->setObject("objectTest",hi);
        respon->postReply(token);
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
    msg->setString("test",std::string("hello world"));

    std::shared_ptr<android::AMessage> respone;
    msg->postAndAwaitResponse(&respone);

    std::string recStr;
    respone->findString("hello",recStr);
    std::shared_ptr<void> tmp;
    respone->findObject("objectTest",&tmp);
    std::shared_ptr<HelloWorld> hello = std::static_pointer_cast<HelloWorld>(tmp);
    hello->show();
    ALOGE("### recevie what:%d  str:%s",respone->what(),recStr.c_str());
    system("read -p \"Press [Enter] key to exit...\"");
    return 0;
}