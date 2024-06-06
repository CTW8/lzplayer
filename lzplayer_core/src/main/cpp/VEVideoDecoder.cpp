#include"VEVideoDecoder.h"

VEVideoDecoder::VEVideoDecoder()
{
    mVideoCtx = nullptr;
    mMediaInfo = nullptr;
}

VEVideoDecoder::~VEVideoDecoder()
{
    uninit();
}

int VEVideoDecoder::init(VEMediaInfo *info)
{
    if (info == nullptr) {
        return -1;
    }

    // 根据音频编码格式初始化解码器
    const AVCodec *codec = avcodec_find_decoder(info->mVideoCodecParams->codec_id);
    if (codec == nullptr) {
        return -1;
    }

    // 创建解码器上下文
    mVideoCtx = avcodec_alloc_context3(codec);
    if (mVideoCtx == nullptr) {
        return -1;
    }

    // 设置解码器参数
    avcodec_parameters_to_context(mVideoCtx, info->mVideoCodecParams);

    // 打开解码器
    if (avcodec_open2(mVideoCtx, codec, nullptr) != 0) {
        return -1;
    }

    return 0;
}

int VEVideoDecoder::flush()
{
    if (!mVideoCtx) {
        return -1; // 解码器上下文为空
    }

    // 刷新解码器状态
    avcodec_flush_buffers(mVideoCtx);

    return 0;
}

int VEVideoDecoder::sendPacket(VEPacket *pack)
{
    if (pack == nullptr) {
        return -1;
    }

    // 将音频包送入解码器
    return avcodec_send_packet(mVideoCtx, pack->frame);

    return 0;
}

int VEVideoDecoder::readFrame(VEFrame *frame)
{
    if (frame == nullptr) {
        return -1;
    }

    // 从解码器中读取音频帧
    int ret = avcodec_receive_frame(mVideoCtx, frame->frame);
    if (ret == AVERROR(EAGAIN)) {
        return 0;
    } else if (ret < 0) {
        return -1;
    }
    return 0;
}

int VEVideoDecoder::uninit()
{
    // 释放资源，包括解码器上下文等
    if (mVideoCtx) {
        avcodec_free_context(&mVideoCtx);
    }

    mMediaInfo = nullptr;
    return 0;
}
