/*
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

/**
 * @file
 * H.264 common definitions
 */

#ifndef AVCODEC_H264_H
#define AVCODEC_H264_H

#define QP_MAX_NUM (51 + 6*6)           // The maximum supported qp

/* NAL unit types */
enum {
    H264_NAL_SLICE           = 1,//	一个非IDR图像的编码条带 
    H264_NAL_DPA             = 2,//slice A
    H264_NAL_DPB             = 3,//slice B
    H264_NAL_DPC             = 4,//slice C
    H264_NAL_IDR_SLICE       = 5,//IDR图像的slice
    H264_NAL_SEI             = 6,//6表示sei，辅助增强信息
    H264_NAL_SPS             = 7,//7表示sps
    H264_NAL_PPS             = 8,//8表示pps
    H264_NAL_AUD             = 9,//访问单元分隔符
    H264_NAL_END_SEQUENCE    = 10,//序列结束
    H264_NAL_END_STREAM      = 11,//码流结尾 
    H264_NAL_FILLER_DATA     = 12,
    H264_NAL_SPS_EXT         = 13,
    H264_NAL_AUXILIARY_SLICE = 19,
};

#endif /* AVCODEC_H264_H */
