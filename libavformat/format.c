/*
 * Format register and lookup
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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

#include "libavutil/atomic.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/opt.h"

#include "avio_internal.h"
#include "avformat.h"
#include "id3v2.h"
#include "internal.h"


/**
 * @file
 * Format register and lookup
 */
/** head of registered input format linked list */
static AVInputFormat *first_iformat = NULL;
/** head of registered output format linked list */
static AVOutputFormat *first_oformat = NULL;

static AVInputFormat **last_iformat = &first_iformat;
static AVOutputFormat **last_oformat = &first_oformat;

AVInputFormat *av_iformat_next(const AVInputFormat *f)
{
    if (f)
        return f->next;
    else
        return first_iformat;
}

AVOutputFormat *av_oformat_next(const AVOutputFormat *f)
{
    if (f)
        return f->next;
    else
        return first_oformat;
}

void av_register_input_format(AVInputFormat *format)
{
    AVInputFormat **p = last_iformat;

    // Note, format could be added after the first 2 checks but that implies that *p is no longer NULL
    while(p != &format->next && !format->next && avpriv_atomic_ptr_cas((void * volatile *)p, NULL, format))
        p = &(*p)->next;

    if (!format->next)
        last_iformat = &format->next;
}

void av_register_output_format(AVOutputFormat *format)
{
    AVOutputFormat **p = last_oformat;

    // Note, format could be added after the first 2 checks but that implies that *p is no longer NULL
    while(p != &format->next && !format->next && avpriv_atomic_ptr_cas((void * volatile *)p, NULL, format))
        p = &(*p)->next;

    if (!format->next)
        last_oformat = &format->next;
}
/*
*av_match_ext()用于比较文件的后缀。该函数首先通过反向查找的方式找到输入文件名中的“.”，就可以通过获取“.”后面的字符串来得到该文件的后缀。然后调用av_match_name()，采用和比较格式名称的方法比较两个后缀。
*/
int av_match_ext(const char *filename, const char *extensions)
{
    const char *ext;

    if (!filename)
        return 0;

    ext = strrchr(filename, '.');//查找某字符在字符串中最后一次出现的位置
    if (ext)
        return av_match_name(ext + 1, extensions);
    return 0;
}

AVOutputFormat *av_guess_format(const char *short_name, const char *filename,
                                const char *mime_type)
{
    AVOutputFormat *fmt = NULL, *fmt_found;
    int score_max, score;

    /* specific test for image sequences */
#if CONFIG_IMAGE2_MUXER
    if (!short_name && filename &&
        av_filename_number_test(filename) &&
        ff_guess_image2_codec(filename) != AV_CODEC_ID_NONE) {
        return av_guess_format("image2", NULL, NULL);
    }
#endif
    /* Find the proper file type. */
    fmt_found = NULL;
    score_max = 0;
    while ((fmt = av_oformat_next(fmt))) {
        score = 0;
        if (fmt->name && short_name && av_match_name(short_name, fmt->name))
            score += 100;
        if (fmt->mime_type && mime_type && !strcmp(fmt->mime_type, mime_type))
            score += 10;
        if (filename && fmt->extensions &&
            av_match_ext(filename, fmt->extensions)) {
            score += 5;
        }
        if (score > score_max) {
            score_max = score;
            fmt_found = fmt;
        }
    }
    return fmt_found;
}

enum AVCodecID av_guess_codec(AVOutputFormat *fmt, const char *short_name,
                              const char *filename, const char *mime_type,
                              enum AVMediaType type)
{
    if (av_match_name("segment", fmt->name) || av_match_name("ssegment", fmt->name)) {
        AVOutputFormat *fmt2 = av_guess_format(NULL, filename, NULL);
        if (fmt2)
            fmt = fmt2;
    }

    if (type == AVMEDIA_TYPE_VIDEO) {
        enum AVCodecID codec_id = AV_CODEC_ID_NONE;

#if CONFIG_IMAGE2_MUXER
        if (!strcmp(fmt->name, "image2") || !strcmp(fmt->name, "image2pipe")) {
            codec_id = ff_guess_image2_codec(filename);
        }
#endif
        if (codec_id == AV_CODEC_ID_NONE)
            codec_id = fmt->video_codec;
        return codec_id;
    } else if (type == AVMEDIA_TYPE_AUDIO)
        return fmt->audio_codec;
    else if (type == AVMEDIA_TYPE_SUBTITLE)
        return fmt->subtitle_codec;
    else if (type == AVMEDIA_TYPE_DATA)
        return fmt->data_codec;
    else
        return AV_CODEC_ID_NONE;
}

AVInputFormat *av_find_input_format(const char *short_name)
{
    AVInputFormat *fmt = NULL;
    while ((fmt = av_iformat_next(fmt)))
        if (av_match_name(short_name, fmt->name))
            return fmt;
    return NULL;
}
/**
*该函数遍历所有的解复用器，调用它们的read_probe()函数计算匹配得分，
*如果解复用器定义了文件扩展名，还会比较输匹配数据跟扩展名的匹配得分。
*函数最终返回计算找到的最匹配的解复用器，并将匹配分数也返回。
*根据输入数据查找合适的AVInputFormat。输入的数据位于AVProbeData中
**/
AVInputFormat *av_probe_input_format3(AVProbeData *pd, int is_opened,
                                      int *score_ret)
{
    AVProbeData lpd = *pd;//把输入数据赋值给lpd
    AVInputFormat *fmt1 = NULL, *fmt;
    int score, score_max = 0;
    const static uint8_t zerobuffer[AVPROBE_PADDING_SIZE];
    enum nodat {
        NO_ID3,
        ID3_ALMOST_GREATER_PROBE,
        ID3_GREATER_PROBE,
        ID3_GREATER_MAX_PROBE,
    } nodat = NO_ID3;

    if (!lpd.buf)//如果lpd中没有存储探测AVInputFormat的媒体数据
        lpd.buf = (unsigned char *) zerobuffer;
	//如果buf_size大于10，且lpd的buf是ID3V2
    if (lpd.buf_size > 10 && ff_id3v2_match(lpd.buf, ID3v2_DEFAULT_MAGIC)) {
        int id3len = ff_id3v2_tag_len(lpd.buf);
        if (lpd.buf_size > id3len + 16) {
            if (lpd.buf_size < 2LL*id3len + 16)
                nodat = ID3_ALMOST_GREATER_PROBE;
            lpd.buf      += id3len;
            lpd.buf_size -= id3len;
        } else if (id3len >= PROBE_BUF_MAX) {
            nodat = ID3_GREATER_MAX_PROBE;
        } else
            nodat = ID3_GREATER_PROBE;
    }

    fmt = NULL;
	//该循环调用av_iformat_next()遍历FFmpeg中所有的AVInputFormat
    while ((fmt1 = av_iformat_next(fmt1))) {
        if (!is_opened == !(fmt1->flags & AVFMT_NOFILE) && strcmp(fmt1->name, "image2"))
            continue;
        score = 0;
        if (fmt1->read_probe) {//如果read_probe指针不为空
			//调用demuxer中的read_probe来探测文件,获取匹配分数(这一方法如果结果匹配的话，一般会获得AVPROBE_SCORE_MAX的分值，即100分)
            score = fmt1->read_probe(&lpd);//调用demuxer的read_probe来探测获取探测分数
            if (score)
                av_log(NULL, AV_LOG_TRACE, "Probing %s score:%d size:%d\n", fmt1->name, score, lpd.buf_size);
            if (fmt1->extensions && av_match_ext(lpd.filename, fmt1->extensions)) {
                switch (nodat) {
                case NO_ID3:
                    score = FFMAX(score, 1);
                    break;
                case ID3_GREATER_PROBE:
                case ID3_ALMOST_GREATER_PROBE:
                    score = FFMAX(score, AVPROBE_SCORE_EXTENSION / 2 - 1);
                    break;
                case ID3_GREATER_MAX_PROBE:
                    score = FFMAX(score, AVPROBE_SCORE_EXTENSION);
                    break;
                }
            }
        } else if (fmt1->extensions) {//如果没有read_probe函数来探测文件，也就是demuxer中没有这个函数的话
			//就使用av_match_ext()函数比较输入媒体的扩展名和AVInputFormat的扩展名是否匹配，
			//如果匹配的话，设定匹配分数为AVPROBE_SCORE_EXTENSION（AVPROBE_SCORE_EXTENSION取值为50，即50分）。
            if (av_match_ext(lpd.filename, fmt1->extensions))
                score = AVPROBE_SCORE_EXTENSION;
        }
		//使用av_match_name()比较输入媒体的mime_type和AVInputFormat的mime_type，
		//如果匹配的话，设定匹配分数为AVPROBE_SCORE_MIME（AVPROBE_SCORE_MIME取值为75，即75分）。
        if (av_match_name(lpd.mime_type, fmt1->mime_type)) {
            if (AVPROBE_SCORE_MIME > score) {
                av_log(NULL, AV_LOG_DEBUG, "Probing %s score:%d increased to %d due to MIME type\n", fmt1->name, score, AVPROBE_SCORE_MIME);
                score = AVPROBE_SCORE_MIME;
            }
        }
		//如果该AVInputFormat的匹配分数大于此前的最大匹配分数，
		//则记录当前的匹配分数为最大匹配分数，
		//并且记录当前的AVInputFormat为最佳匹配的AVInputFormat。
        if (score > score_max) {
            score_max = score;
            fmt       = fmt1;
        } else if (score == score_max)
            fmt = NULL;
    }
    if (nodat == ID3_GREATER_PROBE)
        score_max = FFMIN(AVPROBE_SCORE_EXTENSION / 2 - 1, score_max);
    *score_ret = score_max;

    return fmt;//返回获取最大分数值的AVInputFormat
}

/***
*将得到的匹配分数与要求的匹配值相比较，
*如果匹配分数>匹配值，这返回得到的解复用器，否则返回NULL
*pd：存储输入数据信息的AVProbeData结构体。
*is_opened：文件是否打开。
*score_max：判决AVInputFormat的门限值。只有某格式判决分数大于该门限值的时候，函数才会返回该封装格式，否则返回NULL。
**/
AVInputFormat *av_probe_input_format2(AVProbeData *pd, int is_opened, int *score_max)
{
    int score_ret;
    AVInputFormat *fmt = av_probe_input_format3(pd, is_opened, &score_ret);
	//当av_probe_input_format3()返回的分数大于score_max的时候，
	//才会返回AVInputFormat，否则返回NULL。
    if (score_ret > *score_max) {
        *score_max = score_ret;
        return fmt;
    } else
        return NULL;
}

AVInputFormat *av_probe_input_format(AVProbeData *pd, int is_opened)
{
    int score = 0;
    return av_probe_input_format2(pd, is_opened, &score);
}
/**
*	
*文件探测
*pb：用于读取数据的AVIOContext。
*fmt：输出推测出来的AVInputFormat。
*filename：输入媒体的路径。
*logctx：日志（没有研究过）。
*offset：开始推测AVInputFormat的偏移量。
*max_probe_size：用于推测格式的媒体数据的最大值。
av_probe_input_buffer2()首先需要确定用于推测格式的媒体数据的最大值max_probe_size。
max_probe_size默认为PROBE_BUF_MAX（PROBE_BUF_MAX取值为1 << 20，即1048576Byte，大约1MB）

在确定了max_probe_size之后，函数就会进入到一个循环中，
调用avio_read()读取数据并且使用av_probe_input_format2()（该函数前文已经记录过）
推测文件格式。
肯定有人会奇怪这里为什么要使用一个循环，
而不是只运行一次？其实这个循环是一个逐渐增加输入媒体数据量的过程。
av_probe_input_buffer2()并不是一次性读取max_probe_size字节的媒体数据，
我个人感觉可能是因为这样做不是很经济，
因为推测大部分媒体格式根本用不到1MB这么多的媒体数据。
因此函数中使用一个probe_size存储需要读取的字节数，
并且随着循环次数的增加逐渐增加这个值。
函数首先从PROBE_BUF_MIN（取值为2048）个字节开始读取，
如果通过这些数据已经可以推测出AVInputFormat，
那么就可以直接退出循环了（参考for循环的判断条件“!*fmt”）；
如果没有推测出来，就增加probe_size的量为过去的2倍（参考for循环的表达式“probe_size << 1”），
继续推测AVInputFormat；如果一直读取到max_probe_size字节的数据依然没能确定AVInputFormat，
则会退出循环并且返回错误信息

**/
int av_probe_input_buffer2(AVIOContext *pb, AVInputFormat **fmt,
                          const char *filename, void *logctx,
                          unsigned int offset, unsigned int max_probe_size)
{
    AVProbeData pd = { filename ? filename : "" };
    uint8_t *buf = NULL;
    int ret = 0, probe_size, buf_offset = 0;
    int score = 0;
    int ret2;
	//探测文件的字节数，取自AVFormatContext
    if (!max_probe_size)//如果没有探测字节数
        max_probe_size = PROBE_BUF_MAX;//设置最大值到max_probe_size中，大约1M
    else if (max_probe_size < PROBE_BUF_MIN) {//如果最大的探测字节数小于2048个，直接返回error
        av_log(logctx, AV_LOG_ERROR,
               "Specified probe size value %u cannot be < %u\n", max_probe_size, PROBE_BUF_MIN);
        return AVERROR(EINVAL);
    }

    if (offset >= max_probe_size)//如果偏移大于最大探测字节数，刚传进来的offset是0
        return AVERROR(EINVAL);

    if (pb->av_class) {
        uint8_t *mime_type_opt = NULL;
        char *semi;
        av_opt_get(pb, "mime_type", AV_OPT_SEARCH_CHILDREN, &mime_type_opt);
        pd.mime_type = (const char *)mime_type_opt;//赋值mime_type到probedata中
        av_log(NULL, AV_LOG_ERROR,"hxk>>>>mime_type:%s\n",pd.mime_type);
        semi = pd.mime_type ? strchr(pd.mime_type, ';') : NULL;
        if (semi) {
            *semi = '\0';
        }
    }
#if 0
    if (!*fmt && pb->av_class && av_opt_get(pb, "mime_type", AV_OPT_SEARCH_CHILDREN, &mime_type) >= 0 && mime_type) {
        if (!av_strcasecmp(mime_type, "audio/aacp")) {
            *fmt = av_find_input_format("aac");
        }
        av_freep(&mime_type);
    }
#endif
//for循环处理探测数据
//函数首先从PROBE_BUF_MIN（取值为2048）个字节开始读取;probe_size小于最大探测值，且没有fmt
//每一次for循环probe_size的值增加一倍
    for (probe_size = PROBE_BUF_MIN; probe_size <= max_probe_size && !*fmt;
         probe_size = FFMIN(probe_size << 1,
                            FFMAX(max_probe_size, probe_size + 1))) {//比较probe_size+1与探测最大值的最大值，然后取探测值的2倍与最大值做比较
        //如果探测值小于最大探测值（约1m)     打分25分
        score = probe_size < max_probe_size ? AVPROBE_SCORE_RETRY : 0;

        /* 为探测数据buf申请内存空间Read probe data. */
        if ((ret = av_reallocp(&buf, probe_size + AVPROBE_PADDING_SIZE)) < 0)
            goto fail;
		//从pb(AVIOContext)中读取探测数据buf
        if ((ret = avio_read(pb, buf + buf_offset,
                             probe_size - buf_offset)) < 0) {
            /* Fail if error was not end of file, otherwise, lower score. */
            if (ret != AVERROR_EOF)
                goto fail;

            score = 0;
            ret   = 0;          /* error was end of file, nothing read */
        }
        buf_offset += ret;
        if (buf_offset < offset)
            continue;
        pd.buf_size = buf_offset - offset;
        pd.buf = &buf[offset];

        memset(pd.buf + pd.buf_size, 0, AVPROBE_PADDING_SIZE);

        /* 将上面读取到的pd(AVProbedata）传入到av_probe_input_format2函数
           猜文件格式，探测合适的解复用器Guess file format.*/
        *fmt = av_probe_input_format2(&pd, 1, &score);
        if (*fmt) {//获取fmt
            /* This can only be true in the last iteration. */
            if (score <= AVPROBE_SCORE_RETRY) {//如果分数小于25分
                av_log(logctx, AV_LOG_WARNING,
                       "Format %s detected only with low score of %d, "
                       "misdetection possible!\n", (*fmt)->name, score);
            } else
                av_log(logctx, AV_LOG_DEBUG,
                       "Format %s probed with size=%d and score=%d\n",
                       (*fmt)->name, probe_size, score);
#if 0
            FILE *f = fopen("probestat.tmp", "ab");
            fprintf(f, "probe_size:%d format:%s score:%d filename:%s\n", probe_size, (*fmt)->name, score, filename);
            fclose(f);
#endif
        }
    }

    if (!*fmt)//for循环没有获取到AVInputFormat,返回数据错误
        ret = AVERROR_INVALIDDATA;

fail:
    /* 将探测数据返回给AVIOContext的缓冲buffer，Rewind. Reuse probe buffer to avoid seeking. */
    ret2 = ffio_rewind_with_probe_data(pb, &buf, buf_offset);
    if (ret >= 0)
        ret = ret2;

    av_freep(&pd.mime_type);
    return ret < 0 ? ret : score;
}

int av_probe_input_buffer(AVIOContext *pb, AVInputFormat **fmt,
                          const char *filename, void *logctx,
                          unsigned int offset, unsigned int max_probe_size)
{
    int ret = av_probe_input_buffer2(pb, fmt, filename, logctx, offset, max_probe_size);
    return ret < 0 ? ret : 0;
}
