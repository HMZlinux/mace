//
// Copyright (c) 2017 XiaoMi All rights reserved.
//

#include <arm_neon.h>
#include <limits>

#include "mace/core/common.h"

namespace mace {
namespace kernels {

void PoolingMaxNeonK3x3S2x2(const float *input,
                            const index_t *in_shape,
                            float *output,
                            const index_t *out_shape,
                            const int *paddings) {
  index_t batch = in_shape[0];
  index_t channels = in_shape[1];
  index_t in_height = in_shape[2];
  index_t in_width = in_shape[3];

  index_t out_height = out_shape[2];
  index_t out_width = out_shape[3];

  int padding_top = paddings[0] / 2;
  int padding_bottom = paddings[0] - padding_top;
  int padding_left = paddings[1] / 2;
  int padding_right = paddings[1] - padding_left;

  int in_image_size = in_height * in_width;
  int out_image_size = out_height * out_width;
  index_t input_offset = 0;
  index_t output_offset = 0;

#pragma omp parallel for collapse(2)
  for (int b = 0; b < batch; ++b) {
    for (int c = 0; c < channels; ++c) {
      float *outptr = output + output_offset;

      for (int h = 0; h < out_height; ++h) {
        int w = 0;
        int num_vectors = 0;
        const float *r0, *r1, *r2;
        if (!((h == 0 && padding_top > 0) ||
              (h == out_height - 1 && padding_bottom > 0))) {
          r0 = input + input_offset + (h * 2 - padding_top) * in_width;
          r1 = r0 + in_width;
          r2 = r1 + in_width;

          if (padding_left > 0) {
            if (padding_left == 1) {
              float max0 = std::max(r0[0], r0[1]);
              float max1 = std::max(r1[0], r1[1]);
              float max2 = std::max(r2[0], r2[1]);
              *outptr = std::max(std::max(max0, max1), max2);
              ++r0;
              ++r1;
            } else {  // padding_left == 2
              float max_tmp = std::max(r0[0], r1[0]);
              *outptr = std::max(max_tmp, r2[0]);
            }
            ++outptr;
            ++w;
          }
          if (padding_right > 0) {
            num_vectors = (out_width - w - 1) >> 2;
          } else {
            num_vectors = (out_width - w) >> 2;
          }
        }

        w += num_vectors << 2;
        float32x4x2_t row0 = vld2q_f32(r0);
        float32x4x2_t row1 = vld2q_f32(r1);
        float32x4x2_t row2 = vld2q_f32(r2);
        for (; num_vectors > 0; --num_vectors) {
          float32x4x2_t row0_next = vld2q_f32(r0 + 8);
          float32x4x2_t row1_next = vld2q_f32(r1 + 8);
          float32x4x2_t row2_next = vld2q_f32(r2 + 8);

          float32x4_t max0 = vmaxq_f32(row0.val[0], row0.val[1]);
          float32x4_t max1 = vmaxq_f32(row1.val[0], row1.val[1]);
          float32x4_t max2 = vmaxq_f32(row2.val[0], row2.val[1]);

          float32x4_t row02 = vextq_f32(row0.val[0], row0_next.val[0], 1);
          float32x4_t row12 = vextq_f32(row1.val[0], row1_next.val[0], 1);
          float32x4_t row22 = vextq_f32(row2.val[0], row2_next.val[0], 1);

          max0 = vmaxq_f32(max0, row02);
          max1 = vmaxq_f32(max1, row12);
          max2 = vmaxq_f32(max2, row22);

          float32x4_t max_result = vmaxq_f32(vmaxq_f32(max0, max1), max2);

          vst1q_f32(outptr, max_result);

          row0 = row0_next;
          row1 = row1_next;
          row2 = row2_next;

          r0 += 8;
          r1 += 8;
          r2 += 8;
          outptr += 4;
        }

        for (; w < out_width; ++w) {
          float max = std::numeric_limits<float>::lowest();
          for (int kh = 0; kh < 3; ++kh) {
            for (int kw = 0; kw < 3; ++kw) {
              int inh = h * 2 - padding_top + kh;
              int inw = w * 2 - padding_left + kw;
              if (inh >= 0 && inh < in_height && inw >= 0 && inw < in_width) {
                max = std::max(max, input[input_offset + inh * in_width + inw]);
              }
            }
          }

          *outptr = max;
          ++outptr;
        }
      }
      input_offset += in_image_size;
      output_offset += out_image_size;
    }
  }
}

// assume the input has already been padded
void PoolingMaxNeonK3x3S2x2Padded(const float *input,
                                  const index_t *in_shape,
                                  float *output,
                                  const index_t *out_shape) {
  index_t batch = in_shape[0];
  index_t channels = in_shape[1];
  index_t in_height = in_shape[2];
  index_t in_width = in_shape[3];

  index_t out_height = out_shape[2];
  index_t out_width = out_shape[3];

  int in_image_size = in_height * in_width;
  int out_image_size = out_height * out_width;
  index_t input_offset = 0;
  index_t output_offset = 0;

#pragma omp parallel for collapse(2)
  for (int b = 0; b < batch; ++b) {
    for (int c = 0; c < channels; ++c) {
      const float *img0 = input + input_offset;
      float *outptr = output + output_offset;

      const float *r0 = img0;
      const float *r1 = r0 + in_width;
      const float *r2 = r1 + in_width;

      for (int h = 0; h < out_height; h++) {
        int num_vectors = out_width >> 2;
        int remain = out_width - (num_vectors << 2);

        float32x4x2_t row0 = vld2q_f32(r0);
        float32x4x2_t row1 = vld2q_f32(r1);
        float32x4x2_t row2 = vld2q_f32(r2);
        for (; num_vectors > 0; num_vectors--) {
          float32x4x2_t row0_next = vld2q_f32(r0 + 8);
          float32x4x2_t row1_next = vld2q_f32(r1 + 8);
          float32x4x2_t row2_next = vld2q_f32(r2 + 8);

          float32x4_t max0 = vmaxq_f32(row0.val[0], row0.val[1]);
          float32x4_t max1 = vmaxq_f32(row1.val[0], row1.val[1]);
          float32x4_t max2 = vmaxq_f32(row2.val[0], row2.val[1]);

          float32x4_t row02 = vextq_f32(row0.val[0], row0_next.val[0], 1);
          float32x4_t row12 = vextq_f32(row1.val[0], row1_next.val[0], 1);
          float32x4_t row22 = vextq_f32(row2.val[0], row2_next.val[0], 1);

          max0 = vmaxq_f32(max0, row02);
          max1 = vmaxq_f32(max1, row12);
          max2 = vmaxq_f32(max2, row22);

          float32x4_t max_result = vmaxq_f32(vmaxq_f32(max0, max1), max2);

          vst1q_f32(outptr, max_result);

          row0 = row0_next;
          row1 = row1_next;
          row2 = row2_next;

          r0 += 8;
          r1 += 8;
          r2 += 8;
          outptr += 4;
        }

        for (; remain > 0; remain--) {
          float max0 = std::max(std::max(r0[0], r0[1]), r0[2]);
          float max1 = std::max(std::max(r1[0], r1[1]), r1[2]);
          float max2 = std::max(std::max(r2[0], r2[1]), r2[2]);

          *outptr = std::max(std::max(max0, max1), max2);

          r0 += 2;
          r1 += 2;
          r2 += 2;
          outptr++;
        }

        r0 += 1 + in_width;
        r1 += 1 + in_width;
        r2 += 1 + in_width;
      }
      input_offset += in_image_size;
      output_offset += out_image_size;
    }
  }
}

}  // namespace kernels
}  // namespace mace