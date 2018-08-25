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
*av_match_ext()���ڱȽ��ļ��ĺ�׺���ú�������ͨ��������ҵķ�ʽ�ҵ������ļ����еġ�.�����Ϳ���ͨ����ȡ��.��������ַ������õ����ļ��ĺ�׺��Ȼ�����av_match_name()�����úͱȽϸ�ʽ���Ƶķ����Ƚ�������׺��
*/
int av_match_ext(const char *filename, const char *extensions)
{
    const char *ext;

    if (!filename)
        return 0;

    ext = strrchr(filename, '.');//����ĳ�ַ����ַ��������һ�γ��ֵ�λ��
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
*�ú����������еĽ⸴�������������ǵ�read_probe()��������ƥ��÷֣�
*����⸴�����������ļ���չ��������Ƚ���ƥ�����ݸ���չ����ƥ��÷֡�
*�������շ��ؼ����ҵ�����ƥ��Ľ⸴����������ƥ�����Ҳ���ء�
*�����������ݲ��Һ��ʵ�AVInputFormat�����������λ��AVProbeData��
**/
AVInputFormat *av_probe_input_format3(AVProbeData *pd, int is_opened,
                                      int *score_ret)
{
    AVProbeData lpd = *pd;//���������ݸ�ֵ��lpd
    AVInputFormat *fmt1 = NULL, *fmt;
    int score, score_max = 0;
    const static uint8_t zerobuffer[AVPROBE_PADDING_SIZE];
    enum nodat {
        NO_ID3,
        ID3_ALMOST_GREATER_PROBE,
        ID3_GREATER_PROBE,
        ID3_GREATER_MAX_PROBE,
    } nodat = NO_ID3;

    if (!lpd.buf)//���lpd��û�д洢̽��AVInputFormat��ý������
        lpd.buf = (unsigned char *) zerobuffer;
	//���buf_size����10����lpd��buf��ID3V2
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
	//��ѭ������av_iformat_next()����FFmpeg�����е�AVInputFormat
    while ((fmt1 = av_iformat_next(fmt1))) {
        if (!is_opened == !(fmt1->flags & AVFMT_NOFILE) && strcmp(fmt1->name, "image2"))
            continue;
        score = 0;
        if (fmt1->read_probe) {//���read_probeָ�벻Ϊ��
			//����demuxer�е�read_probe��̽���ļ�,��ȡƥ�����(��һ����������ƥ��Ļ���һ�����AVPROBE_SCORE_MAX�ķ�ֵ����100��)
            score = fmt1->read_probe(&lpd);//����demuxer��read_probe��̽���ȡ̽�����
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
        } else if (fmt1->extensions) {//���û��read_probe������̽���ļ���Ҳ����demuxer��û����������Ļ�
			//��ʹ��av_match_ext()�����Ƚ�����ý�����չ����AVInputFormat����չ���Ƿ�ƥ�䣬
			//���ƥ��Ļ����趨ƥ�����ΪAVPROBE_SCORE_EXTENSION��AVPROBE_SCORE_EXTENSIONȡֵΪ50����50�֣���
            if (av_match_ext(lpd.filename, fmt1->extensions))
                score = AVPROBE_SCORE_EXTENSION;
        }
		//ʹ��av_match_name()�Ƚ�����ý���mime_type��AVInputFormat��mime_type��
		//���ƥ��Ļ����趨ƥ�����ΪAVPROBE_SCORE_MIME��AVPROBE_SCORE_MIMEȡֵΪ75����75�֣���
        if (av_match_name(lpd.mime_type, fmt1->mime_type)) {
            if (AVPROBE_SCORE_MIME > score) {
                av_log(NULL, AV_LOG_DEBUG, "Probing %s score:%d increased to %d due to MIME type\n", fmt1->name, score, AVPROBE_SCORE_MIME);
                score = AVPROBE_SCORE_MIME;
            }
        }
		//�����AVInputFormat��ƥ��������ڴ�ǰ�����ƥ�������
		//���¼��ǰ��ƥ�����Ϊ���ƥ�������
		//���Ҽ�¼��ǰ��AVInputFormatΪ���ƥ���AVInputFormat��
        if (score > score_max) {
            score_max = score;
            fmt       = fmt1;
        } else if (score == score_max)
            fmt = NULL;
    }
    if (nodat == ID3_GREATER_PROBE)
        score_max = FFMIN(AVPROBE_SCORE_EXTENSION / 2 - 1, score_max);
    *score_ret = score_max;

    return fmt;//���ػ�ȡ������ֵ��AVInputFormat
}

/***
*���õ���ƥ�������Ҫ���ƥ��ֵ��Ƚϣ�
*���ƥ�����>ƥ��ֵ���ⷵ�صõ��Ľ⸴���������򷵻�NULL
*pd���洢����������Ϣ��AVProbeData�ṹ�塣
*is_opened���ļ��Ƿ�򿪡�
*score_max���о�AVInputFormat������ֵ��ֻ��ĳ��ʽ�о��������ڸ�����ֵ��ʱ�򣬺����Ż᷵�ظ÷�װ��ʽ�����򷵻�NULL��
**/
AVInputFormat *av_probe_input_format2(AVProbeData *pd, int is_opened, int *score_max)
{
    int score_ret;
    AVInputFormat *fmt = av_probe_input_format3(pd, is_opened, &score_ret);
	//��av_probe_input_format3()���صķ�������score_max��ʱ��
	//�Ż᷵��AVInputFormat�����򷵻�NULL��
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
*�ļ�̽��
*pb�����ڶ�ȡ���ݵ�AVIOContext��
*fmt������Ʋ������AVInputFormat��
*filename������ý���·����
*logctx����־��û���о�������
*offset����ʼ�Ʋ�AVInputFormat��ƫ������
*max_probe_size�������Ʋ��ʽ��ý�����ݵ����ֵ��
av_probe_input_buffer2()������Ҫȷ�������Ʋ��ʽ��ý�����ݵ����ֵmax_probe_size��
max_probe_sizeĬ��ΪPROBE_BUF_MAX��PROBE_BUF_MAXȡֵΪ1 << 20����1048576Byte����Լ1MB��

��ȷ����max_probe_size֮�󣬺����ͻ���뵽һ��ѭ���У�
����avio_read()��ȡ���ݲ���ʹ��av_probe_input_format2()���ú���ǰ���Ѿ���¼����
�Ʋ��ļ���ʽ��
�϶����˻��������ΪʲôҪʹ��һ��ѭ����
������ֻ����һ�Σ���ʵ���ѭ����һ������������ý���������Ĺ��̡�
av_probe_input_buffer2()������һ���Զ�ȡmax_probe_size�ֽڵ�ý�����ݣ�
�Ҹ��˸о���������Ϊ���������Ǻܾ��ã�
��Ϊ�Ʋ�󲿷�ý���ʽ�����ò���1MB��ô���ý�����ݡ�
��˺�����ʹ��һ��probe_size�洢��Ҫ��ȡ���ֽ�����
��������ѭ���������������������ֵ��
�������ȴ�PROBE_BUF_MIN��ȡֵΪ2048�����ֽڿ�ʼ��ȡ��
���ͨ����Щ�����Ѿ������Ʋ��AVInputFormat��
��ô�Ϳ���ֱ���˳�ѭ���ˣ��ο�forѭ�����ж�������!*fmt������
���û���Ʋ������������probe_size����Ϊ��ȥ��2�����ο�forѭ���ı��ʽ��probe_size << 1������
�����Ʋ�AVInputFormat�����һֱ��ȡ��max_probe_size�ֽڵ�������Ȼû��ȷ��AVInputFormat��
����˳�ѭ�����ҷ��ش�����Ϣ

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
	//̽���ļ����ֽ�����ȡ��AVFormatContext
    if (!max_probe_size)//���û��̽���ֽ���
        max_probe_size = PROBE_BUF_MAX;//�������ֵ��max_probe_size�У���Լ1M
    else if (max_probe_size < PROBE_BUF_MIN) {//�������̽���ֽ���С��2048����ֱ�ӷ���error
        av_log(logctx, AV_LOG_ERROR,
               "Specified probe size value %u cannot be < %u\n", max_probe_size, PROBE_BUF_MIN);
        return AVERROR(EINVAL);
    }

    if (offset >= max_probe_size)//���ƫ�ƴ������̽���ֽ������մ�������offset��0
        return AVERROR(EINVAL);

    if (pb->av_class) {
        uint8_t *mime_type_opt = NULL;
        char *semi;
        av_opt_get(pb, "mime_type", AV_OPT_SEARCH_CHILDREN, &mime_type_opt);
        pd.mime_type = (const char *)mime_type_opt;//��ֵmime_type��probedata��
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
//forѭ������̽������
//�������ȴ�PROBE_BUF_MIN��ȡֵΪ2048�����ֽڿ�ʼ��ȡ;probe_sizeС�����̽��ֵ����û��fmt
//ÿһ��forѭ��probe_size��ֵ����һ��
    for (probe_size = PROBE_BUF_MIN; probe_size <= max_probe_size && !*fmt;
         probe_size = FFMIN(probe_size << 1,
                            FFMAX(max_probe_size, probe_size + 1))) {//�Ƚ�probe_size+1��̽�����ֵ�����ֵ��Ȼ��ȡ̽��ֵ��2�������ֵ���Ƚ�
        //���̽��ֵС�����̽��ֵ��Լ1m)     ���25��
        score = probe_size < max_probe_size ? AVPROBE_SCORE_RETRY : 0;

        /* Ϊ̽������buf�����ڴ�ռ�Read probe data. */
        if ((ret = av_reallocp(&buf, probe_size + AVPROBE_PADDING_SIZE)) < 0)
            goto fail;
		//��pb(AVIOContext)�ж�ȡ̽������buf
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

        /* �������ȡ����pd(AVProbedata�����뵽av_probe_input_format2����
           ���ļ���ʽ��̽����ʵĽ⸴����Guess file format.*/
        *fmt = av_probe_input_format2(&pd, 1, &score);
        if (*fmt) {//��ȡfmt
            /* This can only be true in the last iteration. */
            if (score <= AVPROBE_SCORE_RETRY) {//�������С��25��
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

    if (!*fmt)//forѭ��û�л�ȡ��AVInputFormat,�������ݴ���
        ret = AVERROR_INVALIDDATA;

fail:
    /* ��̽�����ݷ��ظ�AVIOContext�Ļ���buffer��Rewind. Reuse probe buffer to avoid seeking. */
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
