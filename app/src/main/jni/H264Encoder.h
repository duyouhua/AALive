#pragma once
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
}
#ifndef __KW_H264_ENCODER_H
#define __KW_H264_ENCODER_H

class H264Encoder {
public:
	H264Encoder();
	~H264Encoder();
	void setSrcVideoSize(const int width, const int height) {
		mSrcWidth = width;
		mSrcHeight = height;
	}
	void setDstVideoSize(const int width, const int height) {
		mDstWidth = width;
		mDstHeight = height;
	}

	void setBitrate(const int bitrate) {
		mBitrate = bitrate;
	}
	void setSrcPixelFormat(AVPixelFormat pixelFormat) {
		mSrcPixelFormat = pixelFormat;
	}
	bool encode(AVPacket *avpkt, const AVFrame *srcFrame);
	void release();
private:
	bool init();
	int mSrcWidth;
	int mSrcHeight;
	int mDstWidth;
	int mDstHeight;
	int mBitrate;
	AVPixelFormat mSrcPixelFormat;
	AVCodecContext *mCodecContext;
	struct SwsContext *mSwsContext;
	AVFrame *mDstFrame;
};
#endif