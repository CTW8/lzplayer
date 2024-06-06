#include"VEAudioDecoder.h"

VEAudioDecoder::VEAudioDecoder(){
    mAudioCtx = nullptr;
    mMediaInfo = nullptr;
}

VEAudioDecoder::~VEAudioDecoder()
{
    uninit();
}

int VEAudioDecoder::init(VEMediaInfo *info)
{
    if (info == nullptr) {
        return -1;
    }

    // 根据音频编码格式初始化解码器
    const AVCodec *codec = avcodec_find_decoder(info->mAudioCodecParams->codec_id);
    if (codec == nullptr) {
        return -1;
    }

    // 创建解码器上下文
    mAudioCtx = avcodec_alloc_context3(codec);
    if (mAudioCtx == nullptr) {
        return -1;
    }

    // 设置解码器参数
    avcodec_parameters_to_context(mAudioCtx, info->mAudioCodecParams);

    // 打开解码器
    if (avcodec_open2(mAudioCtx, codec, nullptr) != 0) {
        return -1;
    }

    return 0;
}

int VEAudioDecoder::flush()
{
    if (!mAudioCtx) {
        return -1; // 解码器上下文为空
    }

    // 刷新解码器状态
    avcodec_flush_buffers(mAudioCtx);

    return 0;
}

int VEAudioDecoder::sendPacket(VEPacket *pack)
{
       if (pack == nullptr) {
        return -1;
    }

    // 将音频包送入解码器
    return avcodec_send_packet(mAudioCtx, pack->frame);

}

int VEAudioDecoder::readFrame(VEFrame *frame)
{
    if (frame == nullptr) {
        return -1;
    }

    // 从解码器中读取音频帧
    int ret = avcodec_receive_frame(mAudioCtx, frame->frame);
    if (ret == AVERROR(EAGAIN)) {
        return 0;
    } else if (ret < 0) {
        return -1;
    }

    return 0;
}

int VEAudioDecoder::uninit()
{
    // 释放资源，包括解码器上下文等
    if (mAudioCtx) {
        avcodec_free_context(&mAudioCtx);
    }

    mMediaInfo = nullptr;
    return 0;
}
