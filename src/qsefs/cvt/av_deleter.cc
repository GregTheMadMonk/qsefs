module;

#include "av_headers.hh"

module qsefs.cvt;

namespace qsefs::cvt {

void AVDeleter::operator()(AVIOContext* io) const { av_free(io);  }
void AVDeleter::operator()(AVFormatContext* fmt) const
{ avformat_close_input(&fmt); }
void AVDeleter::operator()(AVCodecContext* cdx) const
{ avcodec_free_context(&cdx); }
void AVDeleter::operator()(AVPacket* pk) const
{ av_packet_free(&pk); }
void AVDeleter::operator()(AVFrame* frm) const
{ av_frame_free(&frm); }
void AVDeleter::operator()(void* buf) const { av_free(buf); }

} // <-- namespace qsefs::cvt
