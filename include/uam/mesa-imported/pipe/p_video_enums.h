/**************************************************************************
 *
 * Copyright 2009 Younes Manton.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef PIPE_VIDEO_ENUMS_H
#define PIPE_VIDEO_ENUMS_H

#ifdef __cplusplus
extern "C" {
#endif

enum pipe_video_format
{
   PIPE_VIDEO_FORMAT_UNKNOWN = 0,
   PIPE_VIDEO_FORMAT_MPEG12,   /**< MPEG1, MPEG2 */
   PIPE_VIDEO_FORMAT_MPEG4,    /**< DIVX, XVID */
   PIPE_VIDEO_FORMAT_VC1,      /**< WMV */
   PIPE_VIDEO_FORMAT_MPEG4_AVC,/**< H.264 */
   PIPE_VIDEO_FORMAT_HEVC,     /**< H.265 */
   PIPE_VIDEO_FORMAT_JPEG,     /**< JPEG */
   PIPE_VIDEO_FORMAT_VP9       /**< VP9 */
};

enum pipe_video_profile
{
   PIPE_VIDEO_PROFILE_UNKNOWN,
   PIPE_VIDEO_PROFILE_MPEG1,
   PIPE_VIDEO_PROFILE_MPEG2_SIMPLE,
   PIPE_VIDEO_PROFILE_MPEG2_MAIN,
   PIPE_VIDEO_PROFILE_MPEG4_SIMPLE,
   PIPE_VIDEO_PROFILE_MPEG4_ADVANCED_SIMPLE,
   PIPE_VIDEO_PROFILE_VC1_SIMPLE,
   PIPE_VIDEO_PROFILE_VC1_MAIN,
   PIPE_VIDEO_PROFILE_VC1_ADVANCED,
   PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE,
   PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE,
   PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN,
   PIPE_VIDEO_PROFILE_MPEG4_AVC_EXTENDED,
   PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH,
   PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10,
   PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH422,
   PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH444,
   PIPE_VIDEO_PROFILE_HEVC_MAIN,
   PIPE_VIDEO_PROFILE_HEVC_MAIN_10,
   PIPE_VIDEO_PROFILE_HEVC_MAIN_STILL,
   PIPE_VIDEO_PROFILE_HEVC_MAIN_12,
   PIPE_VIDEO_PROFILE_HEVC_MAIN_444,
   PIPE_VIDEO_PROFILE_JPEG_BASELINE,
   PIPE_VIDEO_PROFILE_VP9_PROFILE0,
   PIPE_VIDEO_PROFILE_VP9_PROFILE2,
   PIPE_VIDEO_PROFILE_MAX
};

/* Video caps, can be different for each codec/profile */
enum pipe_video_cap
{
   PIPE_VIDEO_CAP_SUPPORTED = 0,
   PIPE_VIDEO_CAP_NPOT_TEXTURES = 1,
   PIPE_VIDEO_CAP_MAX_WIDTH = 2,
   PIPE_VIDEO_CAP_MAX_HEIGHT = 3,
   PIPE_VIDEO_CAP_PREFERED_FORMAT = 4,
   PIPE_VIDEO_CAP_PREFERS_INTERLACED = 5,
   PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE = 6,
   PIPE_VIDEO_CAP_SUPPORTS_INTERLACED = 7,
   PIPE_VIDEO_CAP_MAX_LEVEL = 8,
   PIPE_VIDEO_CAP_STACKED_FRAMES = 9
};

enum pipe_video_entrypoint
{
   PIPE_VIDEO_ENTRYPOINT_UNKNOWN,
   PIPE_VIDEO_ENTRYPOINT_BITSTREAM,
   PIPE_VIDEO_ENTRYPOINT_IDCT,
   PIPE_VIDEO_ENTRYPOINT_MC,
   PIPE_VIDEO_ENTRYPOINT_ENCODE
};

#if defined(__cplusplus)
}
#endif

#endif /* PIPE_VIDEO_ENUMS_H */
