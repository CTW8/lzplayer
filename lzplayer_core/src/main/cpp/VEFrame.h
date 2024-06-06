#ifndef __VE_FRAME__
#define __VE_FRAME__
#include<iostream>

extern "C"{
    #include"libavcodec/avcodec.h"
}
typedef struct _VEFrame{
    int type;
    AVFrame *frame;
}VEFrame;

#endif