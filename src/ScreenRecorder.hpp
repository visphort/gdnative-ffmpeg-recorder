/*
 * GDNative FFmpeg Screen Recorder
 *
 * Copyright (c) 2022 Visphort <ratelimitingradiators@gmail.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED
 * “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
 * LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 */

#ifndef SCREENRECORDER_H
#define SCREENRECORDER_H

#include <Godot.hpp>
#include <Input.hpp>
#include <Reference.hpp>
#include <Sprite.hpp>
#include <Image.hpp>
#include <String.hpp>
#include <Camera.hpp>
#include <File.hpp>
#include <Viewport.hpp>
#include <ViewportTexture.hpp>
#include <Image.hpp>
#include <OS.hpp>
#include <Thread.hpp>
#include <Mutex.hpp>
#include <Semaphore.hpp>
#include <Array.hpp>
#include <Variant.hpp>
#include <Object.hpp>

#include <queue>
#include <cstring>
#include <iostream>

extern "C" {

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libavutil/timestamp.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <unistd.h>

}

/*
 * Multithreading is currently non-operational in this, and does not result in
 * any noticeable increase in speed.
 */

// #define MULTITHREADED 1

#define DEFAULT_OUTPUT_PIX_FMT AV_PIX_FMT_YUV420P
#define DEFAULT_SCALE_FLAGS SWS_BICUBIC
#define DEFAULT_OUTPUT_CODEC "mpeg"

#define FAILURE (int(godot::Error::FAILED))
#define SUCCESS (int(godot::Error::OK))

#define PRINT_MESSAGE(msg) (godot::Godot::print("[recorder]: " msg))
#define PRINT_ERROR(desc) (godot::Godot::print_error((desc), __func__, __FILE__, __LINE__))

class ScreenRecorder : public godot::Node {
	GODOT_CLASS(ScreenRecorder, godot::Reference)

	godot::String data;

	godot::String format_name;
	godot::String codec_name;

	AVPixelFormat viewport_pix_fmt;

	godot::File output_file; // TODO Remove. May go unused.

	enum State {
		STATE_UNINITIALIZED = 0,
		STATE_FINISHED,
		STATE_STARTED,
		STATE_ERROR
	};

	State recorder_state = STATE_UNINITIALIZED;

	godot::String file_name = "godot_recording.webm"; // export
	godot::String get_file_name() { return file_name; };
	void set_file_name(godot::String v) { file_name = v; };

	godot::Dictionary options;
	godot::Dictionary get_options() { return options; };
	void set_options(godot::Dictionary d) { options = d; };

	int bit_rate = 400000; // export
	int get_bit_rate() { return bit_rate; };
	void set_bit_rate(int v) { bit_rate = v; };

	int video_width = 1024;
	int video_height = 600;

	int frame_rate = 60; // export
	int get_frame_rate() { return frame_rate; };
	void set_frame_rate(int v) { frame_rate = v; };

	int gop_size = 12; // export
	int get_gop_size() { return gop_size; };
	void set_gop_size(int v) { gop_size = v; };

	bool append_timestamp = true; // export
	bool get_append_timestamp() { return append_timestamp; };
	void set_append_timestamp(bool v) { append_timestamp = v; };


#ifdef MULTITHREADED
	godot::Array frame_buffer;
	godot::Semaphore *full_sem;
	godot::Semaphore *empty_sem;
	godot::Mutex *access;
	godot::Thread *thread;
#endif

	int max_buffer_size = 60;


	AVDictionary *opt        = nullptr;
	AVCodec *codec           = nullptr;
	AVFormatContext *fmtctx  = nullptr;
	AVOutputFormat *fmt      = nullptr;
	AVFrame *frame           = nullptr;
	AVFrame *tmp_frame       = nullptr;
	AVStream *st             = nullptr;
	AVCodecContext *codecctx = nullptr;
	SwsContext *swsctx       = nullptr;

	AVPacket packet          = { 0 };

	int64_t next_pts = 0;
	int64_t received_frame_count = 0;
	godot::Viewport viewport;

	int write_video_frame();
	int get_video_frame();
	void prepare_frame(AVFrame *f);

public:
	static void _register_methods();

	void _init();

	int initialize(); // Export
	int start_recorder(); // Export. Called on demand
	int stop_recorder(); // Export, put in _exit_tree maybe 
	int recorder_step(); // Put in _process
	bool is_started();
	int64_t get_received_frame_count();

#ifdef MULTITHREADED
	void push_frame();

	void _thread_func(godot::Variant v);
#endif
};

#endif // SCREENRECORDER_H
