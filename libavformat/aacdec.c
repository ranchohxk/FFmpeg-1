/*
 * raw ADTS AAC demuxer
 * Copyright (c) 2008 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2009 Robert Swain ( rob opendot cl )
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"
#include "id3v1.h"
#include "apetag.h"

#define ADTS_HEADER_SIZE 7

static int adts_aac_probe(AVProbeData *p)
{
    int max_frames = 0, first_frames = 0;
    int fsize, frames;
    const uint8_t *buf0 = p->buf;
    const uint8_t *buf2;
    const uint8_t *buf;
    const uint8_t *end = buf0 + p->buf_size - 7;
    buf = buf0;

    for (; buf < end; buf = buf2 + 1) {
        buf2 = buf;

        for (frames = 0; buf2 < end; frames++) {
            uint32_t header = AV_RB16(buf2);
            if ((header & 0xFFF6) != 0xFFF0) {
                if (buf != buf0) {
                    // Found something that isn't an ADTS header, starting
                    // from a position other than the start of the buffer.
                    // Discard the count we've accumulated so far since it
                    // probably was a false positive.
                    frames = 0;
                }
                break;
            }
            fsize = (AV_RB32(buf2 + 3) >> 13) & 0x1FFF;
            if (fsize < 7)
                break;
            fsize = FFMIN(fsize, end - buf2);
            buf2 += fsize;
        }
        max_frames = FFMAX(max_frames, frames);
        if (buf == buf0)
            first_frames = frames;
    }
    if (first_frames >= 3)
        return AVPROBE_SCORE_EXTENSION + 1;
    else if (max_frames > 100)
        return AVPROBE_SCORE_EXTENSION;
    else if (max_frames >= 3)
        return AVPROBE_SCORE_EXTENSION / 2;
    else if (first_frames >= 1)
        return 1;
    else
        return 0;
}
//add by hxk
static int getAdtsFrameLength(AVFormatContext *s,int64_t offset,int* headerSize)
{
	int64_t filesize, position = avio_tell(s->pb);  
    filesize = avio_size(s->pb);
	//av_log(NULL, AV_LOG_WARNING, "hxk->getAdtsFrameLength.filesize:%d\n",filesize);
    const int kAdtsHeaderLengthNoCrc = 7;
    const int kAdtsHeaderLengthWithCrc = 9;
    int frameSize = 0;
    uint8_t syncword[2];
	avio_seek(s->pb, offset, SEEK_SET);
    if(avio_read(s->pb,&syncword, 2)!= 2){
		return 0;
	}
    if ((syncword[0] != 0xff) || ((syncword[1] & 0xf6) != 0xf0)) {
        return 0;
    }
	uint8_t protectionAbsent;
	avio_seek(s->pb, offset+1, SEEK_SET);
    if (avio_read(s->pb, &protectionAbsent, 1) < 1) {
        return 0;
    }
    protectionAbsent &= 0x1;

    uint8_t header[3];
	avio_seek(s->pb, offset+3, SEEK_SET);
    if (avio_read(s->pb, &header, 3) < 3) {
        return 0;
    }
    frameSize = (header[0] & 0x3) << 11 | header[1] << 3 | header[2] >> 5;
    // protectionAbsent is 0 if there is CRC
    int headSize = protectionAbsent ? kAdtsHeaderLengthNoCrc : kAdtsHeaderLengthWithCrc;
    if (headSize > frameSize) {
        return 0;
    }
    if (headerSize != NULL) {
        *headerSize = headSize;
    }
    return frameSize;
}

static uint32_t get_sample_rate(const uint8_t sf_index)
{
    static const uint32_t sample_rates[] =
    {
        96000, 88200, 64000, 48000, 44100, 32000,
        24000, 22050, 16000, 12000, 11025, 8000
    };

    if (sf_index < sizeof(sample_rates) / sizeof(sample_rates[0])) {
        return sample_rates[sf_index];
    }

    return 0;
}

//add end
static int adts_aac_read_header(AVFormatContext *s)
{
	//av_log(NULL, AV_LOG_WARNING, "hxk->adts_aac_read_header!\n");

    AVStream *st;
    uint16_t state;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id   = s->iformat->raw_codec_id;
    st->need_parsing         = AVSTREAM_PARSE_FULL_RAW;
    ff_id3v1_read(s);
    if ((s->pb->seekable & AVIO_SEEKABLE_NORMAL) &&
        !av_dict_get(s->metadata, "", NULL, AV_DICT_IGNORE_SUFFIX)) {
        int64_t cur = avio_tell(s->pb);
        ff_ape_parse_tag(s);
        avio_seek(s->pb, cur, SEEK_SET);
    }

    // skip data until the first ADTS frame is found
    state = avio_r8(s->pb);
    while (!avio_feof(s->pb) && avio_tell(s->pb) < s->probesize) {
        state = (state << 8) | avio_r8(s->pb);
        if ((state >> 4) != 0xFFF)
            continue;
        avio_seek(s->pb, -2, SEEK_CUR);
        break;
    }
    if ((state >> 4) != 0xFFF)
        return AVERROR_INVALIDDATA;

    // LCM of all possible ADTS sample rates
  //  avpriv_set_pts_info(st, 64, 1, 28224000);
//add by hxk
#if  1       
		uint8_t profile, sf_index, channel, header[2];
		avio_seek(s->pb, 2, SEEK_SET);
		if (avio_read(s->pb,&header, 2) < 2) {
			av_log(NULL, AV_LOG_ERROR, "avio_read header error!\n");
			return 0;
		}
		int64_t offset = 0;
		profile = (header[0] >> 6) & 0x3;
		st->codecpar->profile = profile;
		sf_index = (header[0] >> 2) & 0xf;
		uint32_t sr = get_sample_rate(sf_index);
		if (sr == 0) {
			av_log(NULL, AV_LOG_ERROR, "adts_aac_read_header read sampletare error!\n");
			return 0;
		}
		st->codecpar->sample_rate = sr;
		channel = (header[0] & 0x1) << 2 | (header[1] >> 6);
		if(channel == 0) {
			av_log(NULL, AV_LOG_ERROR, "adts_aac_read_header read channel error!\n");
			return 0;
		}
		st->codecpar->channels = channel;
		sf_index = (header[0] >> 2) & 0xf;
		int frameSize = 0;
		int64_t mFrameDurationUs = 0;
		int64_t duration = 0;
		st->codecpar->sample_rate = sr;
		int64_t streamSize, numFrames = 0;
	    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
		streamSize =  avio_size(s->pb);
	    //av_log(NULL, AV_LOG_WARNING, "hxk->streamSize:%d!\n",streamSize);
		if (streamSize > 0) {
			while (offset < streamSize) {
				if ((frameSize = getAdtsFrameLength(s, offset, NULL)) == 0) {
					  return 0;
				}
				offset += frameSize;
				numFrames ++;
			//av_log(NULL, AV_LOG_WARNING, "hxk->frameSize:%d!\n",frameSize);
			}
		    // av_log(NULL, AV_LOG_WARNING, "hxk->numFrames:%lld!\n",numFrames);
			// Round up and get the duration
			if(sr == 0) {
				 av_log(NULL, AV_LOG_ERROR, "sr can not be zero!\n");
				 return AVERROR_INVALIDDATA;
			}
			mFrameDurationUs = (1024 * 1000000ll + (sr - 1)) / sr;
			duration = numFrames * mFrameDurationUs;//us
			//av_log(NULL, AV_LOG_WARNING, "hxk->duration.us:%d!\n",duration);
			//时间基转换avstream的，us单位(AV_TIME_BASE_Q)转avstream的时间基
			duration = av_rescale_q(duration,AV_TIME_BASE_Q, st->time_base);
			st->duration = duration;
		    //av_log(NULL, AV_LOG_WARNING, "hxk->duration.st_timebase:%d!\n",duration);
		}
		avio_seek(s->pb, 0, SEEK_SET);
		
#endif
//add end

    return 0;
}

static int adts_aac_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, fsize;
    
    ret = av_get_packet(s->pb, pkt, ADTS_HEADER_SIZE);
    if (ret < 0)
        return ret;
    if (ret < ADTS_HEADER_SIZE) {
        av_packet_unref(pkt);
        return AVERROR(EIO);
    }

    if ((AV_RB16(pkt->data) >> 4) != 0xfff) {
        av_packet_unref(pkt);
        return AVERROR_INVALIDDATA;
    }

    fsize = (AV_RB32(pkt->data + 3) >> 13) & 0x1FFF;
    if (fsize < ADTS_HEADER_SIZE) {
        av_packet_unref(pkt);
        return AVERROR_INVALIDDATA;
    }
//	av_log(NULL, AV_LOG_WARNING, "hxk->adts_aac_read_packet->fsize%d\n",fsize);

    return av_append_packet(s->pb, pkt, fsize - ADTS_HEADER_SIZE);
}

AVInputFormat ff_aac_demuxer = {
    .name         = "aac",
    .long_name    = NULL_IF_CONFIG_SMALL("raw ADTS AAC (Advanced Audio Coding)"),
    .read_probe   = adts_aac_probe,
    .read_header  = adts_aac_read_header,
    .read_packet  = adts_aac_read_packet,
    .flags        = AVFMT_GENERIC_INDEX,
    .extensions   = "aac",
    .mime_type    = "audio/aac,audio/aacp,audio/x-aac",
    .raw_codec_id = AV_CODEC_ID_AAC,
};
