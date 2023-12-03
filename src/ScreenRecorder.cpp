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

#include "ScreenRecorder.hpp"
#include <libavutil/dict.h>


static godot::String get_timestamp() {
	return godot::String(godot::OS::get_singleton()->get_unix_time());
}

// Workaround to prevent C++ from complaining about the temp array.
static godot::String get_avcodec_error_string(int err) {
	char errbuf[64] = {0};
	return godot::String(av_make_error_string(errbuf, 64, err));
}

static AVFrame *alloc_frame(AVPixelFormat pix_fmt, int width, int height) {
	AVFrame *frame;
	int ret;

	frame = av_frame_alloc();
	if (!frame)
		return nullptr;

	frame->format = pix_fmt;
	frame->width = width;
	frame->height = height;

	ret = av_frame_get_buffer(frame, 0);

	if (ret < 0) {
		PRINT_ERROR("Frame Data Allocation failed.");
		return nullptr;
	}

	return frame;
}

int ScreenRecorder::initialize() {

	int ret;

	// Deduce screen dimensions. We are assuming that these do not change.
	PRINT_MESSAGE("Making Window Unresizable");
	godot::OS::get_singleton()->set_window_resizable(false);
	get_viewport()->set_clear_mode(godot::Viewport::CLEAR_MODE_ALWAYS);
	godot::Ref<godot::Image> img = get_viewport()->get_texture()->get_data();

	video_width = img->get_width();
	video_height = img->get_height();

	// Deduce Format and Codec from given filename
	// Apparently attempting to free this will crash the program, so I'm
	// assuming this is managed by godot.
	char *c_file_name = file_name.alloc_c_string();

	avformat_alloc_output_context2(
		&fmtctx,
		nullptr, nullptr, c_file_name);
	
	if (!fmtctx) {
		PRINT_ERROR("Could not deduce output format from '" + file_name + "'. Attempting to use '" DEFAULT_OUTPUT_CODEC "'...");
		
		avformat_alloc_output_context2(&fmtctx, nullptr, DEFAULT_OUTPUT_CODEC, c_file_name);
		if (!fmtctx) {
			PRINT_ERROR("Could not load '" DEFAULT_OUTPUT_CODEC "' format. Init failed.");
			return FAILURE;
		}
	}

	fmt = fmtctx->oformat;

	if (fmt->video_codec == AV_CODEC_ID_NONE) {
		PRINT_ERROR("No video codec available for assigned format. Init failed.");
		return FAILURE;
	}
	
	// Read options dictionary and add the values

	godot::Array keys = options.keys();

	for (int i = 0; i < keys.size(); i++) {
		if (keys[i].get_type() != godot::Variant::STRING) {
			continue;
		}
		godot::String keystr = keys[i];

		if (options[keys[i]].get_type() != godot::Variant::STRING) {
			continue;
		}
		godot::String value = options[keys[i]]; 
		PRINT_MESSAGE("SETTING OPTION: " + keystr + " = " + value);
		av_dict_set(&opt, keystr.alloc_c_string(), value.alloc_c_string(), 0);
	}

	// Now load the codec

	codec = avcodec_find_encoder(fmt->video_codec);

	if (!codec) {
		PRINT_ERROR("Could not find encoder for '" + godot::String(avcodec_get_name(fmt->video_codec)) + "'. Init failed.");
		return FAILURE;
	}

	// Now start the output stream

	st = avformat_new_stream(fmtctx, codec); // Warn: NULL provided instead of codec in example.

	if (!st) {
		PRINT_ERROR("Could not allocate stream for required video format. Init failed.");
		return FAILURE;
	}

	st->id = fmtctx->nb_streams - 1;

	// Allocate Codec context

	codecctx = avcodec_alloc_context3(codec);

	if (!codecctx) {
		PRINT_ERROR("Could not allocate encoding context. Init failed.");
		return FAILURE;
	}

	codecctx->codec_id = fmt->video_codec;
	codecctx->bit_rate = bit_rate;
	codecctx->width = video_width;
	codecctx->height = video_height;

	st->time_base = (AVRational) { 1, 60 };
	codecctx->time_base = st->time_base;
	codecctx->gop_size = gop_size;

	std::cout << "ScreenRecorder Init" << std::endl
			  << "===================" << std::endl
			  << "file_name: " << c_file_name << std::endl
			  << "bit_rate: " << bit_rate << std::endl
			  << "video_width: " << video_width << std::endl
			  << "video_height: " << video_height << std::endl
			  << "frame_rate: " << frame_rate << std::endl
			  << "gop_size: " << gop_size << std::endl;

	// Set pixel format according to texture returned by viewport

	codecctx->pix_fmt = DEFAULT_OUTPUT_PIX_FMT;

	if (fmtctx->oformat->flags & AVFMT_GLOBALHEADER)
		codecctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	// Set options and open the codec needed to start decoding

	// avcodec_open2 populates opt with the options that are unused so it is
	// necessary that we pass a copy of it instead if we want to use it later.
	AVDictionary *opt_copy;

	av_dict_copy(&opt_copy, opt, 0);
	ret = avcodec_open2(codecctx, codec, &opt_copy);
	av_dict_free(&opt_copy);

	if (ret < 0) {
		PRINT_ERROR("Could not start video codec: " + get_avcodec_error_string(ret) + ". Init failed.");
		return FAILURE;
	}

	// Allocate frame

	tmp_frame = alloc_frame(AV_PIX_FMT_RGB24, video_width, video_height);
	frame = alloc_frame(codecctx->pix_fmt, video_width, video_height);

	// Copy stream parameters into muxer

	ret = avcodec_parameters_from_context(st->codecpar, codecctx);

	if (ret < 0) {
		PRINT_ERROR("Failed to copy stream parameters. Init failed.");
		return FAILURE;
	}

	// Dump format info to stdout

	av_dump_format(fmtctx, 0, c_file_name, 1);

	recorder_state = STATE_FINISHED;

	return SUCCESS;
}

int ScreenRecorder::start_recorder() {
	int ret;

	if (recorder_state != STATE_FINISHED) {
		PRINT_ERROR(godot::String(__func__) + " called when recorder is already started.");
		return int(godot::Error::ERR_ALREADY_IN_USE);
	}

	godot::String final_file_name;

	// if (append_timestamp) {
	// 	final_file_name = file_name + "_" + get_timestamp();
	// } else {
	// 	final_file_name = file_name;
	// }

	final_file_name = file_name;

	char *c_final_file_name = final_file_name.alloc_c_string();

	if (!(fmt->flags & AVFMT_NOFILE)) {
		ret = avio_open(&fmtctx->pb, c_final_file_name, AVIO_FLAG_WRITE);
		if (ret < 0) {
			PRINT_ERROR("Could not open " + final_file_name + ": " + get_avcodec_error_string(ret));
			return FAILURE;
		}
	}

	ret = avformat_write_header(fmtctx, &opt);
	
	if (ret < 0) {
		PRINT_ERROR("Could not write header: " + get_avcodec_error_string(ret));
		return FAILURE;
	}

#ifdef MULTITHREADED
	// We increment till the max buffer amount.
	for (int i = 0; i < max_buffer_size; i++) {
		full_sem->post();
	}

	thread = godot::Thread::_new();
	thread->reference();
	thread->start(this, "_thread_func");
#endif

	recorder_state = STATE_STARTED;
	return SUCCESS;
}

void ScreenRecorder::prepare_frame(AVFrame *f) {

#ifdef MULTITHREADED
	access->lock();
	godot::Variant v = frame_buffer.pop_back();
	godot::Ref<godot::Image> img = ((godot::Ref<godot::Image>) v);
	access->unlock();
#else
	godot::Ref<godot::Image> img = get_viewport()->get_texture()->get_data();
#endif

	if (img->get_format() != godot::Image::Format::FORMAT_RGB8) {
		img->convert(int64_t(godot::Image::Format::FORMAT_RGB8));
	}

	img->flip_y();

	godot::PoolByteArray data = img->get_data();

	memcpy(f->data[0], data.read().ptr(), data.size());
}

int ScreenRecorder::get_video_frame() {

	// Unnecessary
	/* check if we want to generate more frames */
	// if (av_compare_ts(ost->next_pts, c->time_base,
	// 				  STREAM_DURATION, (AVRational){ 1, 1 }) >= 0)
	// 	return NULL;

	/* when we pass a frame to the encoder, it may keep a reference to it
	 * internally; make sure we do not overwrite it here */
	
	if (av_frame_make_writable(frame) < 0) {
		PRINT_ERROR("Could not make frame writable.");
		return -1;
	}

	/* we must convert it to the codec pixel format if needed */
	if (!swsctx) {
		swsctx = sws_getContext(codecctx->width, codecctx->height,
								AV_PIX_FMT_RGB24,
								codecctx->width, codecctx->height,
								codecctx->pix_fmt,
								DEFAULT_SCALE_FLAGS, nullptr, nullptr, nullptr);
		if (!swsctx) {
			PRINT_ERROR("Could not initialize the conversion context");
			return -1;
		}
	}

	// Now put image
	// TODO look into PoolByteArray implementation

	prepare_frame(tmp_frame);

	// failed attempt to directly use PoolByteArray in sws_scale.
	//tmp_frame->data[0] =  (uint8_t *) get_viewport()->get_texture()->get_data()->get_data().read().ptr();

	sws_scale(swsctx,
			(const uint8_t * const *) tmp_frame->data,
			tmp_frame->linesize,
			0, codecctx->height, 
			frame->data, frame->linesize
			);

	frame->pts = next_pts;
	next_pts += 1;

	return 0;
}

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt) {
	AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

	// Yes. Looks really bad. I know. Just wanted to get rid of the error
	// messages ASAP. Maybe wrapping this in extern "C" {} instead and declaring
	// this in another file would do the trick.

	char buf1[128] = {0};
	char buf2[128] = {0};
	char buf3[128] = {0};
	char buf4[128] = {0};
	char buf5[128] = {0};
	char buf6[128] = {0};


		printf("timebase: %d/%d pts:%s pts_time:%s dts:%s dts_time:%s duration:%s "
			   "duration_time:%s stream_index:%d\n",
			   time_base->num,
			   time_base->den,
			   av_ts_make_string(buf1, pkt->pts),
			   av_ts_make_time_string(buf2, pkt->pts, time_base),
			   av_ts_make_string(buf3, pkt->dts),
			   av_ts_make_time_string(buf4, pkt->dts, time_base),
			   av_ts_make_string(buf5, pkt->duration),
			   av_ts_make_time_string(buf6, pkt->duration, time_base),
			   pkt->stream_index);
}

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt) {
	av_packet_rescale_ts(pkt, *time_base, st->time_base);
	pkt->stream_index = st->index;

	log_packet(fmt_ctx, pkt);
	return av_interleaved_write_frame(fmt_ctx, pkt);
}

int ScreenRecorder::write_video_frame() {
	int ret = 0;
	AVPacket pkt = {0};
	av_init_packet(&pkt);

	// get_video_frame goes here
	ret = get_video_frame();

	if (ret < 0) {
		PRINT_ERROR("Could not get video frame");
		return -1;
	}

	if (frame) {
		std::cout << "Send frame  " << frame->pts << std::endl;
	}

	ret = avcodec_send_frame(codecctx, frame);
	
	if (ret < 0) {
		PRINT_ERROR("Error encoding video frame: " + get_avcodec_error_string(ret));
		return ret;
	}

	ret = 0;

	while (ret >= 0) {
		ret = avcodec_receive_packet(codecctx, &pkt);
		// packet.duration = st.time_base.den / 60;
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			return ret;
		} else if (ret < 0) {
			PRINT_ERROR("Error while retrieving encoded data packet: " + get_avcodec_error_string(ret));
			return ret;
		}


		std::cout << "Recv Packet " << pkt.pts << " " << pkt.size << std::endl;

		ret = write_frame(fmtctx, &codecctx->time_base, st, &pkt);

		received_frame_count++;

		if (ret < 0) {
			PRINT_ERROR("Error while writing encoded data packet: " + get_avcodec_error_string(ret));
			return ret;
		}

		av_packet_unref(&pkt);
	}

	return ret;
}

int ScreenRecorder::recorder_step() {
	int ret;
	if (recorder_state != STATE_STARTED) {
		PRINT_ERROR("recorder_step called when recording is already finished.");
		return int(godot::Error::ERR_UNAVAILABLE);
	}

	ret = write_video_frame();

	if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
		PRINT_ERROR("Stream Error Detected. Exiting.");
		recorder_state = STATE_ERROR;
		return FAILURE;
	}
	
	return SUCCESS;
}

#ifdef MULTITHREADED

void ScreenRecorder::_thread_func(godot::Variant v) {
	while (recorder_state == STATE_STARTED) {
		empty_sem->wait();
		recorder_step();
		full_sem->post();
	}
	PRINT_MESSAGE("THREAD EXITING");
}

// Essentially the Producers - Consumers problem
void ScreenRecorder::push_frame() {
		get_viewport()->set_clear_mode(godot::Viewport::CLEAR_MODE_ONLY_NEXT_FRAME);
		full_sem->wait();
		godot::Ref<godot::Image> v = get_viewport()->get_texture()->get_data();
		v->reference();
		access->lock();
		frame_buffer.push_back(v);
		access->unlock();
		empty_sem->post();
}

#endif

int ScreenRecorder::stop_recorder() {
	int ret;

	if (recorder_state == STATE_FINISHED) {
		PRINT_ERROR("recorder_stop called when recording is already finished.");
		return int(godot::Error::ERR_UNAVAILABLE);
	}

	recorder_state = STATE_FINISHED;

#ifdef MULTITHREADED

	thread->wait_to_finish();
	thread->free();

#endif

	PRINT_MESSAGE("Writing Trailer.");
	ret = av_write_trailer(fmtctx);
	if (ret < 0) {
		PRINT_ERROR("Failed to write Trailer");
		return FAILURE;
	}

	PRINT_MESSAGE("Cleaning Up...");
	avcodec_free_context(&codecctx);
	av_frame_free(&frame);
	av_frame_free(&tmp_frame);
	sws_freeContext(swsctx);

	if (!(fmt->flags & AVFMT_NOFILE)) {
		avio_closep(&fmtctx->pb);
	}

	avformat_free_context(fmtctx);
	PRINT_MESSAGE("Finished.");

	return SUCCESS;
}

bool ScreenRecorder::is_started() {
	return recorder_state == STATE_STARTED;
}

int64_t ScreenRecorder::get_received_frame_count() {
	return received_frame_count;
}

void ScreenRecorder::_register_methods() {
	godot::register_method("initialize", &ScreenRecorder::initialize);
	godot::register_method("start_recorder", &ScreenRecorder::start_recorder);
	godot::register_method("stop_recorder", &ScreenRecorder::stop_recorder);
	godot::register_method("recorder_step", &ScreenRecorder::recorder_step);
	godot::register_method("is_started", &ScreenRecorder::is_started);
	godot::register_method("get_received_frame_count", &ScreenRecorder::get_received_frame_count);
#ifdef MULTITHREADED
	godot::register_method("push_frame", &ScreenRecorder::push_frame);
	godot::register_method("_thread_func", &ScreenRecorder::_thread_func);
#endif

	godot::register_property<ScreenRecorder, godot::String>(
		"file_name",
		&ScreenRecorder::set_file_name,
		&ScreenRecorder::get_file_name,
		"godot_recording.webm",
		GODOT_METHOD_RPC_MODE_DISABLED,
		GODOT_PROPERTY_USAGE_DEFAULT,
		GODOT_PROPERTY_HINT_GLOBAL_FILE);

	godot::register_property<ScreenRecorder, int>(
		"bit_rate",
		&ScreenRecorder::set_bit_rate,
		&ScreenRecorder::get_bit_rate,
		400000);

	godot::register_property<ScreenRecorder, godot::Dictionary>(
			"options",
			&ScreenRecorder::set_options,
			&ScreenRecorder::get_options,
			godot::Dictionary()
		);

	godot::register_property<ScreenRecorder, int>(
		"frame_rate",
		&ScreenRecorder::set_frame_rate,
		&ScreenRecorder::get_frame_rate,
		60);

	godot::register_property<ScreenRecorder, int>(
		"gop_size",
		&ScreenRecorder::set_gop_size,
		&ScreenRecorder::get_gop_size,
		12);

	godot::register_property<ScreenRecorder, bool>(
		"append_timestamp",
		&ScreenRecorder::set_append_timestamp,
		&ScreenRecorder::get_append_timestamp,
		true);
}

void ScreenRecorder::_init() {
	set_process(false);

#ifdef MULTITHREADED
	full_sem = godot::Semaphore::_new();
	empty_sem = godot::Semaphore::_new();
	access = godot::Mutex::_new();
#endif
}