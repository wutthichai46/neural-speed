//  Copyright (c) 2023 Intel Corporation
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
#pragma once
#include "bestla.h"
#include "bestla_utils.h"
#include "kernel_ref.h"
#if CompileAVX2()
#include <immintrin.h>
#endif
namespace bestla {
namespace kernel {
namespace avx2 {
#if CompileAVX2()
#ifdef __GNUC__
#pragma GCC push_options
#pragma GCC target("avx2", "fma")
#else
#endif

static uint8_t shuffle_map[] = {0x00, 0x01, 0x02, 0x03, 0xff, 0xff, 0xff, 0xff,
                                0x04, 0x05, 0x06, 0x07, 0xff, 0xff, 0xff, 0xff};

template <BTLA_DTYPE S4_T>
static inline __m128i unpack_4bits_sse(void* srcptr) {
  auto shuffle_v = _mm_loadu_si128(reinterpret_cast<__m128i*>(shuffle_map));
  auto raw_data = _mm_loadl_epi64(reinterpret_cast<__m128i*>(srcptr));
  auto xmm0 = _mm_shuffle_epi8(raw_data, shuffle_v);
  auto xmm1 = _mm_srli_epi32(xmm0, 0x04);
  auto and_helper = _mm_set1_epi8(0x0f);
  xmm0 = _mm_and_si128(xmm0, and_helper);
  xmm1 = _mm_and_si128(xmm1, and_helper);
  auto xmm2 = _mm_unpacklo_epi8(xmm0, xmm1);
  auto xmm3 = _mm_unpackhi_epi8(xmm0, xmm1);
  xmm2 = _mm_unpacklo_epi64(xmm2, xmm3);
  if constexpr (S4_T != BTLA_DTYPE::S4_FULLRANGE) xmm2 = _mm_slli_epi32(xmm2, 4);
  return xmm2;
}

inline __m256 ymm_cvt_bf16_fp32(__m128i vbf16) {
  auto vf32 = _mm256_cvtepu16_epi32(vbf16);
  return _mm256_castsi256_ps(_mm256_slli_epi32(vf32, 16));
}

inline __m128i ymm_cvtepi32_epi16(__m256i src) {
  __m128i tmp;
#ifdef __GNUC__
  for (size_t i = 0; i < 8; i++) {
    (reinterpret_cast<int16_t*>(&tmp))[i] = (reinterpret_cast<int32_t*>(&src))[i];
  }
#else
  for (size_t i = 0; i < 8; i++) {
    tmp.m128i_i16[i] = src.m256i_i32[i];
  }
#endif
  return tmp;
}

inline __m128i ymm_cvt_fp32_bf16(__m256 vfp32) {
  return ymm_cvtepi32_epi16(_mm256_bsrli_epi128(_mm256_castps_si256(vfp32), 2));
}

template <BTLA_DTYPE S4_T>
static inline void convert_s4_s8_16_sse(int8_t* dstptr, int8_t* srcptr) {
  auto dst0 = unpack_4bits_sse<S4_T>(srcptr);
  if constexpr (S4_T == BTLA_DTYPE::S4_FULLRANGE) {
    auto s8 = _mm_set1_epi8(8);
    dst0 = _mm_sub_epi8(dst0, s8);
  }
  _mm_storeu_si128(reinterpret_cast<__m128i*>(dstptr), dst0);
}

template <typename T>
static inline void convert_s8_fp_v8(T* dstptr, int8_t* srcptr) {
  auto xmm = _mm_loadl_epi64(reinterpret_cast<__m128i*>(srcptr));
  auto ymm = _mm256_cvtepi8_epi32(xmm);
  auto ymm1 = _mm256_cvtepi32_ps(ymm);
  if constexpr (std::is_same_v<T, utils::bf16>) {
    auto xmm = ymm_cvt_fp32_bf16(ymm1);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dstptr), xmm);
  } else {
    _mm256_storeu_ps(dstptr, ymm1);
  }
}

static inline void fp4_pad_4bit(int8_t* dstptr, int8_t* srcptr) {
  auto dst0 = unpack_4bits_sse<BTLA_DTYPE::S4_FULLRANGE>(srcptr);
  _mm_storeu_si128(reinterpret_cast<__m128i*>(dstptr), dst0);
}

template <int N, bool _IS_SYM>
static inline void dequant_s8_N_avx2(float* dstptr, int8_t* srcptr, __m256* vscales, __m256i* vzps = nullptr) {
  static_assert(N % 8 == 0);
  int constexpr VLoop = N / 8;
  for (int iv = 0; iv < VLoop; iv += 1) {
    auto src_s8 = _mm_loadl_epi64(reinterpret_cast<__m128i*>(srcptr + iv * 8));
    auto zmm = _mm256_cvtepi8_epi32(src_s8);
    if constexpr (!_IS_SYM) zmm = _mm256_sub_epi32(zmm, vzps[iv]);
    auto fzmm = _mm256_cvtepi32_ps(zmm);
    fzmm = _mm256_mul_ps(fzmm, vscales[iv]);
    _mm256_storeu_ps(dstptr + iv * 8, fzmm);
  }
}

inline BTLA_CODE dq8_get_fp_scale(uint8_t* src, float* dst, int row, int col, int scale_offset, int dq_blk,
                                  int dq_offset_idx, float* dq_scale, int src_stride, int dst_stride,
                                  bool zeropadding) {
  auto head_proc_num = utils::updiv(scale_offset, 8) * 8 - scale_offset;
  auto ymm_dq_offset = _mm256_set1_ps(dq_scale[dq_offset_idx]);

  auto get_fp_scale_ref = [&](int proc_src_num, int scale_offset, uint8_t* src, float* dst) {
    auto dq_s_idx = scale_offset / dq_blk;
    for (int j = 0; j < col; j++) dst[j] = dq8_bnb_LUT[src[j]] * dq_scale[dq_s_idx] + dq_scale[dq_offset_idx];
  };

  auto get_fp_scale_avx2 = [&](int scale_offset, uint8_t* src, float* dst) {
    auto dq_s_idx = scale_offset / dq_blk;
    auto ymm_dq_scale = _mm256_set1_ps(dq_scale[dq_s_idx]);
    float tmp[8];
    for (int i = 0; i < 8; i++) tmp[i] = dq8_bnb_LUT[src[i]];
    __m256 fp32_dq_ymm = _mm256_loadu_ps(tmp);
    auto fymm = _mm256_mul_ps(fp32_dq_ymm, ymm_dq_scale);
    fymm = _mm256_add_ps(fymm, ymm_dq_offset);
    _mm256_storeu_ps(dst, fymm);
  };

  for (int i = 0; i < row; i++) {
    if (head_proc_num > col) {
      get_fp_scale_ref(col, scale_offset, src + i * src_stride, dst + i * dst_stride);
    } else {
      get_fp_scale_ref(head_proc_num, scale_offset, src + i * src_stride, dst + i * dst_stride);
      auto scale_offset_iter = scale_offset + head_proc_num;
      uint8_t* src_iter_ptr = src + head_proc_num;
      float* dst_iter_ptr = dst + head_proc_num;
      auto body_loop = (col - head_proc_num) / 8;
      auto tail_proc_num = (col - head_proc_num) % 8;
      int ii = 0;
      for (; ii < body_loop; ii++) {
        get_fp_scale_avx2(scale_offset_iter + ii * 8, src_iter_ptr + i * src_stride + ii * 8,
                          dst_iter_ptr + i * dst_stride + ii * 8);
      }
      if (tail_proc_num > 0) {
        get_fp_scale_ref(tail_proc_num, scale_offset_iter + ii * 8, src_iter_ptr + i * src_stride + ii * 8,
                         dst_iter_ptr + i * dst_stride + ii * 8);
      }
    }
  }
  if (zeropadding) assert(0);
  return BTLA_CODE::Success;
}

static inline BTLA_CODE alphabeta_f32_f32(const float alpha, const float* srcptr, const int srcstep, const float beta,
                                          const float* src1ptr, const int src1step, float* dstptr, const int dststep,
                                          const int M, const int N) {
  int constexpr Vlen = 8;
  auto vN = utils::padto_le(N, Vlen);
  auto valpha = _mm256_set1_ps(alpha);
  auto vbeta = _mm256_set1_ps(beta);

  for (int i = 0; i < M; i++) {
    int j = 0;
    if (beta != 0.f) {
      for (; j < vN; j += Vlen) {
        auto vsrc = _mm256_loadu_ps(srcptr + i * srcstep + j);
        auto vsrc1 = _mm256_loadu_ps(src1ptr + i * src1step + j);
        auto vdst = _mm256_mul_ps(valpha, vsrc);
        vdst = _mm256_fmadd_ps(vbeta, vsrc1, vdst);
        _mm256_storeu_ps(dstptr + i * dststep + j, vdst);
      }
      for (; j < N; j += 1) {
        dstptr[i * dststep + j] = alpha * srcptr[i * srcstep + j] + beta * src1ptr[i * src1step + j];
      }
    } else {
      for (; j < vN; j += Vlen) {
        auto vsrc = _mm256_loadu_ps(srcptr + i * srcstep + j);
        auto vdst = _mm256_mul_ps(valpha, vsrc);
        _mm256_storeu_ps(dstptr + i * dststep + j, vdst);
      }
      for (; j < N; j += 1) {
        dstptr[i * dststep + j] = alpha * srcptr[i * srcstep + j];
      }
    }
  }
  return BTLA_CODE::Success;
}

template <int PACK_ROW, bool WITH_ZP, typename _DST_T>
BTLA_CODE dequant_kblock_s8_fp_fwd(int8_t* srcptr, _DST_T* dstptr, int row, int col, int ld_src, int ld_dst,
                                   float* scales, int8_t* zero_points, int k_offset, int kblock, int NPad) {
  const int Vlen = 8;
  size_t simd_process_num = utils::padto_le(col, Vlen);
  auto packrow4_permute_idx = _mm256_setr_epi32(0, 0, 0, 0, 1, 1, 1, 1);
  for (int i = 0; i < row; i++) {
    int kpos = (k_offset + i) / kblock;
    auto sptr = scales + kpos * NPad;
    int j = 0;
    for (; j < simd_process_num; j += Vlen) {
      auto s8_ymm_v = _mm_loadl_epi64(reinterpret_cast<__m128i*>(srcptr + i * ld_src + j));
      auto s32_ymm_v = _mm256_cvtepi8_epi32(s8_ymm_v);
      if constexpr (WITH_ZP) {
        auto zp_ymm =
            _mm256_cvtepi8_epi32(_mm_loadl_epi64(reinterpret_cast<__m128i*>(zero_points + kpos * NPad + j / PACK_ROW)));
        if constexpr (PACK_ROW == 4) zp_ymm = _mm256_permutevar8x32_epi32(zp_ymm, packrow4_permute_idx);
        s32_ymm_v = _mm256_sub_epi32(s32_ymm_v, zp_ymm);
      }
      auto f32_ymm_v = _mm256_cvtepi32_ps(s32_ymm_v);
      auto scale_ymm = _mm256_loadu_ps(sptr + j / PACK_ROW);
      if constexpr (PACK_ROW == 4) scale_ymm = _mm256_permutevar8x32_ps(scale_ymm, packrow4_permute_idx);
      f32_ymm_v = _mm256_mul_ps(f32_ymm_v, scale_ymm);
      if constexpr (std::is_same_v<_DST_T, float>) {
        _mm256_storeu_ps(dstptr + i * ld_dst + j, f32_ymm_v);
      } else if constexpr (std::is_same_v<_DST_T, utils::bf16>) {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dstptr + i * ld_dst), ymm_cvt_fp32_bf16(f32_ymm_v));
      } else {
        assert(0);
      }
    }
    for (; j < col; j++) {
      float tmp = (float)(srcptr[i * ld_src + j]);
      if constexpr (WITH_ZP) tmp -= (float)(zero_points[kpos * NPad + j / PACK_ROW]);
      dstptr[i * ld_dst + j] = tmp * sptr[j / PACK_ROW];
    }
  }
  return BTLA_CODE::Success;
}

template <int PACK_ROW, typename _DST_T>
static inline BTLA_CODE dequant_kblock_s8_fp(int8_t* srcptr, _DST_T* dstptr, int row, int col, int ld_src, int ld_dst,
                                             float* scales, int8_t* zero_points, int k_offset, int kblock, int NPad) {
  if (zero_points == nullptr)
    return dequant_kblock_s8_fp_fwd<PACK_ROW, false>(srcptr, dstptr, row, col, ld_src, ld_dst, scales, zero_points,
                                                     k_offset, kblock, NPad);
  else
    return dequant_kblock_s8_fp_fwd<PACK_ROW, true>(srcptr, dstptr, row, col, ld_src, ld_dst, scales, zero_points,
                                                    k_offset, kblock, NPad);
}

template <typename SCAB_T>
static inline BTLA_CODE dequant_s32_fp32(const int32_t* srcptr, const int srcstep, float* dstptr, const int dststep,
                                         const int row, const int col, const float* scaleA, const int ldsa,
                                         const SCAB_T* scaleB) {
  int col8 = utils::padto_le(col, 8);
  for (int irow = 0; irow < row; irow++) {
    auto scale = scaleA[irow * ldsa];
    auto valpha = _mm256_set1_ps(scale);
    int icol = 0;
    for (; icol < col8; icol += 8) {
      __m256 vwscale;
      if constexpr (std::is_same_v<SCAB_T, float>) {
        vwscale = _mm256_loadu_ps(scaleB + icol);
      } else if constexpr (std::is_same_v<SCAB_T, utils::bf16>) {
        auto tmp = _mm_loadu_si128(reinterpret_cast<__m128i*>(scaleB + icol));
        vwscale = ymm_cvt_bf16_fp32(tmp);
      }
      auto vscale = _mm256_mul_ps(valpha, vwscale);
      auto vsrcd = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(srcptr + irow * srcstep + icol));
      auto vsrc = _mm256_cvtepi32_ps(vsrcd);
      vsrc = _mm256_mul_ps(vsrc, vscale);
      _mm256_storeu_ps(dstptr + irow * dststep + icol, vsrc);
    }
    for (; icol < col; icol += 1) {
      dstptr[irow * dststep + icol] = scale * scaleB[icol] * srcptr[irow * srcstep + icol];
    }
  }
  return BTLA_CODE::Success;
}

static inline BTLA_CODE remove_act_zeropoint_bias(float* accptr, int ldacc, int row, int col, uint8_t* zps,
                                                  float* scales, int lds, const float* reduce) {
  int constexpr VLen = 8;
  auto col8 = utils::padto_le(col, VLen);
  for (int i = 0; i < row; i++) {
    auto zpf = static_cast<float>(zps[i * lds]) * scales[i * lds];
    int j = 0;
    auto vzp = _mm256_set1_ps(-zpf);
    for (; j < col8; j += VLen) {
      auto vreduce = _mm256_loadu_ps(reduce + j);
      auto vacc = _mm256_loadu_ps(&accptr[i * ldacc + j]);
      vacc = _mm256_fmadd_ps(vzp, vreduce, vacc);
      _mm256_storeu_ps(&accptr[i * ldacc + j], vacc);
    }
    if (j < col) {
      for (; j < col; j++) {
        accptr[i * ldacc + j] -= zpf * reduce[j];
      }
    }
  }
  return BTLA_CODE::Success;
}

static inline BTLA_CODE remove_wei_zeropoint_bias(float* accptr, int ldacc, int row, int col, int8_t* zps,
                                                  float* scales, int lds, const float* reduce) {
  int constexpr VLen = 8;
  auto col8 = utils::padto_le(col, VLen);
  const int32_t mask[] = {-1, -1, 0, 0};
  for (int i = 0; i < row; i++) {
    auto vreduce = _mm256_set1_ps(-reduce[i * lds]);
    int j = 0;
    for (; j < col8; j += VLen) {
      auto vzp_s32 = _mm256_cvtepi8_epi32(_mm_maskload_epi32(reinterpret_cast<const int*>(zps + j),
                                                             _mm_loadu_si128(reinterpret_cast<const __m128i*>(mask))));
      auto vzp_f32 = _mm256_cvtepi32_ps(vzp_s32);
      auto vzp = _mm256_mul_ps(vzp_f32, _mm256_loadu_ps(scales + j));
      auto vacc = _mm256_loadu_ps(&accptr[i * ldacc + j]);
      vacc = _mm256_fmadd_ps(vzp, vreduce, vacc);
      _mm256_storeu_ps(&accptr[i * ldacc + j], vacc);
    }
    if (j < col) {
      for (; j < col; j++) {
        accptr[i * ldacc + j] -= static_cast<float>(zps[j]) * scales[j] * reduce[i * lds];
      }
    }
  }
  return BTLA_CODE::Success;
}

static inline BTLA_CODE remove_zeropoint_bias(float* accptr, int ldacc, int row, int col, uint8_t* zpa, int8_t* zpb,
                                              float* scalea, float* scaleb, int lds, int k, const float* reducea,
                                              const float* reduceb) {
  int constexpr VLen = 8;
  auto col8 = utils::padto_le(col, VLen);
  auto vk = _mm256_set1_ps(static_cast<float>(k));
  const int32_t mask[] = {-1, -1, 0, 0};
  for (int i = 0; i < row; i++) {
    auto vreducea = _mm256_set1_ps(-reducea[i * lds]);
    auto zpaf = static_cast<float>(zpa[i * lds]) * scalea[i * lds];
    auto vzpa = _mm256_set1_ps(-zpaf);
    int j = 0;
    for (; j < col8; j += VLen) {
      auto vzp_s32 = _mm256_cvtepi8_epi32(_mm_maskload_epi32(reinterpret_cast<const int*>(zpb + j),
                                                             _mm_loadu_si128(reinterpret_cast<const __m128i*>(mask))));
      auto vzp_f32 = _mm256_cvtepi32_ps(vzp_s32);
      auto vzpb = _mm256_mul_ps(vzp_f32, _mm256_loadu_ps(scaleb + j));
      auto vreduceb = _mm256_loadu_ps(reduceb + j);
      auto vacc = _mm256_loadu_ps(&accptr[i * ldacc + j]);
      vacc = _mm256_fmadd_ps(vzpa, vreduceb, vacc);
      vacc = _mm256_fmadd_ps(vzpb, vreducea, vacc);
      vzpb = _mm256_mul_ps(vzpb, vk);
      vacc = _mm256_fmadd_ps(vzpa, vzpb, vacc);
      _mm256_storeu_ps(&accptr[i * ldacc + j], vacc);
    }
    if (j < col) {
      for (; j < col; j++) {
        accptr[i * ldacc + j] -= static_cast<float>(zpb[j]) * scaleb[j] * reducea[i * lds];
        accptr[i * ldacc + j] -= zpaf * reduceb[j];
        accptr[i * ldacc + j] -= zpaf * static_cast<float>(zpb[j]) * scaleb[j] * k;
      }
    }
  }
  return BTLA_CODE::Success;
}

template <BTLA_DTYPE S4_T>
static inline BTLA_CODE decompress_s4_s8(utils::int4x2* srcptr, int8_t* dstptr, int row, int col, int ld_src,
                                         int ld_dst) {
  uint32_t mask = 0xf0f0f0f0;
  auto vmask = _mm256_set1_epi32(*reinterpret_cast<int*>(&mask));
  if (col == ld_src) {
    size_t elesize = static_cast<size_t>(row) * col;
    size_t ele16 = utils::padto_le(elesize, 16);
    size_t i = 0;
    for (; i < ele16; i += 16) {
      convert_s4_s8_16_sse<S4_T>(dstptr + i, reinterpret_cast<int8_t*>(srcptr + i / 2));
    }
    for (; i < elesize; i += 2) {
      auto tmp = srcptr[i / 2];
      dstptr[i + 0] = kernel::ref::get_s8<S4_T>(tmp.x);
      dstptr[i + 1] = kernel::ref::get_s8<S4_T>(tmp.y);
    }
    return BTLA_CODE::Success;
  }
  return BTLA_CODE::NotSupport;
}

template <BTLA_DTYPE S4_T, typename _DST_T>
inline BTLA_CODE decompress_kblock_s4_s8fp(utils::int4x2* srcptr, _DST_T* dstptr, int row, int col, int ld_src,
                                           int ld_dst, int8_t* tmp, size_t tmpsize) {
  uint32_t mask = 0xf0f0f0f0;
  auto vmask = _mm256_set1_epi32(*reinterpret_cast<int*>(&mask));
  if (col == ld_src) {
    size_t elesize = static_cast<size_t>(row) * col;
    size_t ele16 = utils::padto_le(elesize, 16);
    size_t i = 0;
    assert(tmpsize >= 16);
    for (; i < ele16; i += 16) {
      convert_s4_s8_16_sse<S4_T>(tmp, reinterpret_cast<int8_t*>(srcptr + i / 2));
      convert_s8_fp_v8(dstptr + i, tmp);
      convert_s8_fp_v8(dstptr + i + 8, tmp + 8);
    }
    for (; i < elesize; i += 2) {
      auto tmp = srcptr[i / 2];
      dstptr[i + 0] = static_cast<_DST_T>(static_cast<float>(ref::get_s8<S4_T>(tmp.x)));
      dstptr[i + 1] = static_cast<_DST_T>(static_cast<float>(ref::get_s8<S4_T>(tmp.y)));
    }
    return BTLA_CODE::Success;
  }
  return BTLA_CODE::Success;
}

template <bool WITH_SCALE, typename _DST_T, int _PACK_ROW, typename _S_T>
inline BTLA_CODE decompress_kblock_f8_fp(utils::f8* srcptr, _DST_T* dstptr, int row, int col, int ld_src, int ld_dst,
                                         _S_T* scales, int k_offset, int kblock, int NPad, BTLA_DTYPE src_f8_type) {
  int align_col = col / 16 * 16;
  int col_tail = col - align_col;
  auto ebits = utils::bestla_dtype_get_f8_ebits(src_f8_type);
  auto mantissabit = 7 - ebits;
  auto sign_revert_and_mask = _mm256_set1_epi32(0x80000000);
  auto e_revert_and_mask = _mm256_set1_epi32(0x0000007f);
  auto e_revert_shift = _mm256_set1_epi32(1);
  e_revert_shift = _mm256_slli_epi32(e_revert_shift, ebits - 1);
  e_revert_shift = _mm256_sub_epi32(e_revert_shift, _mm256_set1_epi32(128));
  auto mantissa_revert_and_mask = _mm256_set1_epi32(0x007fffff);
  auto packrow2_permute_idx = _mm256_setr_epi32(0, 0, 1, 1, 2, 2, 3, 3);
  for (int i = 0; i < row; i++) {
    int kpos = (k_offset + i) / kblock;
    auto sptr = scales + kpos * NPad;
    int j = 0;
    auto quant = [&]() {
      auto sign_revert = _mm256_cvtepi8_epi32(_mm_loadu_si128(reinterpret_cast<__m128i*>(srcptr + i * ld_src + j)));
      auto e_revert = sign_revert;
      auto mantissa_revert = sign_revert;
      sign_revert = _mm256_slli_epi32(sign_revert, 24);
      sign_revert = _mm256_and_si256(sign_revert, sign_revert_and_mask);
      e_revert = _mm256_and_si256(e_revert, e_revert_and_mask);
      e_revert = _mm256_srli_epi32(e_revert, mantissabit);
      if constexpr (WITH_SCALE && std::is_same_v<_S_T, utils::f8>) {
        auto scale = _mm256_cvtepi8_epi32(_mm_loadu_si128(reinterpret_cast<__m128i*>(sptr + j / _PACK_ROW)));
        if constexpr (_PACK_ROW == 2) scale = _mm256_permutexvar_epi32(packrow2_permute_idx, scale);
        e_revert = _mm256_add_epi32(e_revert, scale);
      }
      e_revert = _mm256_sub_epi32(e_revert, e_revert_shift);
      e_revert = _mm256_slli_epi32(e_revert, 23);
      mantissa_revert = _mm256_slli_epi32(mantissa_revert, 23 - mantissabit);
      mantissa_revert = _mm256_and_si256(mantissa_revert, mantissa_revert_and_mask);
      auto fp_v = _mm256_or_ps(_mm256_castsi256_ps(sign_revert), _mm256_castsi256_ps(e_revert));
      fp_v = _mm256_or_ps(fp_v, _mm256_castsi256_ps(mantissa_revert));
      if constexpr (WITH_SCALE && std::is_same_v<_S_T, float>) {
        auto scale = _mm256_loadu_ps(sptr + j / _PACK_ROW);
        if constexpr (_PACK_ROW == 2) scale = _mm256_permutexvar_ps(packrow2_permute_idx, scale);
        fp_v = _mm256_mul_ps(fp_v, scale);
      }
      if constexpr (std::is_same_v<_DST_T, float>) {
        _mm256_storeu_ps(dstptr + i * ld_dst + j, fp_v);
      } else {
        assert(0);
      }
    };
    for (; j < align_col; j += 8) quant();
    for (; j < col; j++) {
      auto fp_v = ref::f8_to_fp32(srcptr[i * ld_src + j], src_f8_type);
      if constexpr (WITH_SCALE) {
        if constexpr (std::is_same_v<_S_T, utils::f8>) {
          dstptr[i * ld_dst + j] = fp_v * std::pow(2, sptr[j / _PACK_ROW].x);
        } else if constexpr (std::is_same_v<_S_T, float>) {
          dstptr[i * ld_dst + j] = fp_v * sptr[j / _PACK_ROW];
        }
      } else {
        dstptr[i * ld_dst + j] = fp_v;
      }
    }
  }
  return BTLA_CODE::Success;
}

template <typename DST_T>
inline BTLA_CODE decompress_kblock_s8_s8fp(int8_t* srcptr, DST_T* dstptr, int row, int col, int ld_src, int ld_dst) {
  if (col == ld_src) {
    size_t elesize = (size_t)row * col;
    size_t ele64 = utils::padto_le(elesize, 64);
    size_t i = 0;
    if (i + 64 <= ele64) {
      for (; i < ele64; i += 64) {
        for (size_t j = 0; j < 64; j += 8) {
          convert_s8_fp_v8(dstptr + i + j, srcptr + i + j);
        }
      }
    }
    for (; i < elesize; i += 1) {
      auto tmp = srcptr[i];
      dstptr[i] = static_cast<DST_T>(static_cast<float>(tmp));
    }
    return BTLA_CODE::Success;
  }
  return BTLA_CODE::NotSupport;
}

template <typename SCA_T>
static inline BTLA_CODE accum_alphaN_f32_f32(const SCA_T* alpha, const float* srcptr, const int srcstep, float* dstptr,
                                             const int dststep, const int M, const int N) {
  int constexpr Vlen = 8;
  auto vN = utils::padto_le(N, Vlen);
  int j = 0;
  for (; j < vN; j += Vlen) {
    __m256 valpha;
    if constexpr (std::is_same_v<SCA_T, float>) {
      valpha = _mm256_loadu_ps(alpha + j);
    } else if constexpr (std::is_same_v<SCA_T, utils::bf16>) {
      auto tmp = _mm_loadu_si128(reinterpret_cast<const __m128i*>(alpha + j));
      valpha = ymm_cvt_bf16_fp32(tmp);
    } else if constexpr (std::is_same_v<SCA_T, utils::f8>) {
      auto ebit = _mm256_cvtepi8_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(alpha + j)));
      ebit = _mm256_add_epi32(_mm256_set1_epi32(127), ebit);
      valpha = _mm256_castsi256_ps(_mm256_slli_epi32(ebit, 23));
    }
    for (size_t i = 0; i < M; i++) {
      auto vsrc = _mm256_loadu_ps(srcptr + i * srcstep + j);
      auto vsrc1 = _mm256_loadu_ps(dstptr + i * dststep + j);
      auto vdst = _mm256_fmadd_ps(valpha, vsrc, vsrc1);
      _mm256_storeu_ps(dstptr + i * dststep + j, vdst);
    }
  }
  for (; j < N; j += 1) {
    for (size_t i = 0; i < M; i++) {
      if constexpr (!std::is_same_v<SCA_T, utils::f8>) {
        dstptr[i * dststep + j] += alpha[j] * srcptr[i * srcstep + j];
      } else {
        dstptr[i * dststep + j] += std::pow(2, alpha[j].x) * srcptr[i * srcstep + j];
      }
    }
  }
  return BTLA_CODE::Success;
}

template <int N, typename _DST_T, BTLA_DTYPE F4_T>
static inline void dequant_f4_N(_DST_T* dstptr, int8_t* srcptr, __m256* vscales, __m256i* vzps) {
  static_assert(N % 8 == 0);
  float* LUT;
  static_assert(F4_T == BTLA_DTYPE::F4_BNB || F4_T == BTLA_DTYPE::F4_NF4 || F4_T == BTLA_DTYPE::F4_E2M1,
                "Unsupported F4 type");
  if constexpr (F4_T == BTLA_DTYPE::F4_BNB) {
    LUT = fp4_bnb_dequant_fp32_LUT;
  } else if constexpr (F4_T == BTLA_DTYPE::F4_NF4) {
    LUT = nf4_dequant_fp32_LUT;
  } else if constexpr (F4_T == BTLA_DTYPE::F4_E2M1) {
    LUT = fp4_e2m1_dequant_fp32_LUT;
  }
  int constexpr VLoop = N / 8;
  for (int iv = 0; iv < VLoop; iv++) {
    auto idx = _mm_loadl_epi64(reinterpret_cast<__m128i*>(srcptr + iv * 8));
    auto pad_idx = _mm256_cvtepu8_epi32(idx);
    auto fp32_dq_v = _mm256_i32gather_ps(LUT, pad_idx, 4);
    fp32_dq_v = _mm256_mul_ps(fp32_dq_v, vscales[iv]);
    if constexpr (std::is_same_v<_DST_T, float>) {
      _mm256_storeu_ps(dstptr + iv * 8, fp32_dq_v);
    } else if constexpr (std::is_same_v<_DST_T, utils::bf16>) {
      auto bf16v = ymm_cvt_fp32_bf16(fp32_dq_v);
      _mm_storeu_si128(reinterpret_cast<__m128i*>(dstptr + iv * 8), bf16v);
    }
  }
}

template <int N, typename _DST_T, BTLA_DTYPE F4_T>
static inline void unpack_f4_N(_DST_T* dstptr, int8_t* srcptr) {
  static_assert(N % 8 == 0);
  float* LUT;
  static_assert(F4_T == BTLA_DTYPE::F4_BNB || F4_T == BTLA_DTYPE::F4_NF4 || F4_T == BTLA_DTYPE::F4_E2M1,
                "Unsupported F4 type");
  if constexpr (F4_T == BTLA_DTYPE::F4_BNB) {
    LUT = fp4_bnb_dequant_fp32_LUT;
  } else if constexpr (F4_T == BTLA_DTYPE::F4_NF4) {
    LUT = nf4_dequant_fp32_LUT;
  } else if constexpr (F4_T == BTLA_DTYPE::F4_E2M1) {
    LUT = fp4_e2m1_dequant_fp32_LUT;
  }
  int constexpr VLoop = N / 8;
  for (int iv = 0; iv < VLoop; iv++) {
    auto idx = _mm_loadl_epi64(reinterpret_cast<__m128i*>(srcptr + iv * 8));
    auto pad_idx = _mm256_cvtepu8_epi32(idx);
    auto fp32_dq_v = _mm256_i32gather_ps(LUT, pad_idx, 4);
    if constexpr (std::is_same_v<_DST_T, float>) {
      _mm256_storeu_ps(dstptr + iv * 8, fp32_dq_v);
    } else if constexpr (std::is_same_v<_DST_T, utils::bf16>) {
      auto bf16v = ymm_cvt_fp32_bf16(fp32_dq_v);
      _mm_storeu_si128(reinterpret_cast<__m128i*>(dstptr + iv * 8), bf16v);
    }
  }
}

template <BTLA_DTYPE F4_T, typename DST_T>
inline BTLA_CODE decompress_kblock_f4_fp_noscale(utils::f4x2* srcptr, DST_T* dstptr, int row, int col, int ld_src,
                                                 int ld_dst, int8_t* tmp, size_t tmpsize) {
  uint32_t mask = 0xf0f0f0f0;
  auto vmask = _mm256_set1_epi32(*reinterpret_cast<int*>(&mask));
  if (col == ld_src) {
    size_t elesize = static_cast<size_t>(row) * col;
    size_t ele16 = utils::padto_le(elesize, 16);
    size_t i = 0;
    assert(tmpsize >= 16);
    for (; i < ele16; i += 16) {
      fp4_pad_4bit(tmp, reinterpret_cast<int8_t*>(srcptr + i / 2));
      unpack_f4_N<16, DST_T, F4_T>(dstptr + i, tmp);
    }
    for (; i < elesize; i += 2) {
      auto tmp = srcptr[i / 2];
      dstptr[i + 0] = static_cast<DST_T>(ref::f4_unpack<F4_T>(tmp.x));
      dstptr[i + 1] = static_cast<DST_T>(ref::f4_unpack<F4_T>(tmp.y));
    }
    return BTLA_CODE::Success;
  }
  return BTLA_CODE::Success;
}

template <bool _IS_SYM, int _NCOL, typename _ST, typename _DST_T>
static inline BTLA_CODE decompress_kblock_bit4_packrow1(
    utils::bit4x2* srcptr, _DST_T* dstptr, int row, int col, int ld_src, int ld_dst, _ST* scales, int8_t* zero_points,
    int k_offset, int kblock, int NPad, void (*dequantize)(_DST_T*, int8_t*, __m256*, __m256i*),
    void (*pad_bit4_16)(int8_t*, int8_t*), void (*pad_bit4_8)(int8_t*, int8_t*), int8_t* tmpbuf, size_t tmpsize) {
  uint32_t mask = 0xf0f0f0f0;
  auto vmask = _mm256_set1_epi32(*reinterpret_cast<int*>(&mask));
  int constexpr NReg = _NCOL / 8;
  assert(col == _NCOL);
  assert(ld_src == _NCOL);
  assert(ld_dst == _NCOL);
  __m256 vscales[NReg];
  __m256i vzps[NReg];
  int constexpr UnrollRow = 4;
  assert(kblock % UnrollRow == 0);
  int constexpr Loop16 = _NCOL * UnrollRow / 16;
  assert(tmpsize >= (_NCOL * UnrollRow));
  int row0 = kblock - k_offset % kblock;
  row0 = row0 == kblock ? 0 : row0;
  row0 = row0 > row ? row : row0;
  int row1 = row - row0;
  int irow = 0;
  if (row0) {
    int rowpad4 = utils::padto_le(row0, UnrollRow);
    for (int iv = 0; iv < NReg; iv++) {
      vscales[iv] = _mm256_loadu_ps(scales + (k_offset + irow) / kblock * NPad + iv * 8);
      if constexpr (!_IS_SYM) {
        auto tmp =
            _mm_loadl_epi64(reinterpret_cast<__m128i*>(zero_points + (k_offset + irow) / kblock * NPad + iv * 8));
        vzps[iv] = _mm256_cvtepi8_epi32(tmp);
      }
    }
    for (; irow < rowpad4; irow += UnrollRow) {
      for (int iter16 = 0; iter16 < Loop16; iter16++)
        pad_bit4_16(tmpbuf + iter16 * 16, reinterpret_cast<int8_t*>(srcptr + irow * ld_src / 2 + 8 * iter16));
      for (int iterr = 0; iterr < UnrollRow; iterr++)
        dequantize(dstptr + (irow + iterr) * ld_dst, tmpbuf + iterr * _NCOL, vscales, vzps);
    }
    for (; irow < row0; irow++) {
      if constexpr (_NCOL == 24) {
        pad_bit4_16(tmpbuf, reinterpret_cast<int8_t*>(srcptr + irow * ld_src / 2));
        pad_bit4_8(tmpbuf + 16, reinterpret_cast<int8_t*>(srcptr + irow * ld_src / 2 + 8));
      } else {
        for (int iter16 = 0; iter16 < 3; iter16++)
          pad_bit4_16(tmpbuf + iter16 * 16, reinterpret_cast<int8_t*>(srcptr + irow * ld_src / 2 + 8 * iter16));
      }
      dequantize(dstptr + irow * ld_dst, tmpbuf, vscales, vzps);
    }
  }

  int row1_blk = utils::padto_le(row1, kblock) + row0;
  for (; irow < row1_blk; irow += kblock) {
    for (int iv = 0; iv < NReg; iv++) {
      vscales[iv] = _mm256_loadu_ps(scales + (k_offset + irow) / kblock * NPad + iv * 8);
      if constexpr (!_IS_SYM) {
        auto tmp =
            _mm_loadl_epi64(reinterpret_cast<__m128i*>(zero_points + (k_offset + irow) / kblock * NPad + iv * 8));
        vzps[iv] = _mm256_cvtepi8_epi32(tmp);
      }
    }
    for (int irr = 0; irr < kblock; irr += UnrollRow) {
      for (int iter16 = 0; iter16 < Loop16; iter16++)
        pad_bit4_16(tmpbuf + iter16 * 16, reinterpret_cast<int8_t*>(srcptr + (irow + irr) * ld_src / 2 + 8 * iter16));
      for (int iterr = 0; iterr < UnrollRow; iterr++)
        dequantize(dstptr + (irow + irr + iterr) * ld_src, tmpbuf + iterr * _NCOL, vscales, vzps);
    }
  }
  if (irow < row) {
    for (int iv = 0; iv < NReg; iv++) {
      vscales[iv] = _mm256_loadu_ps(scales + (k_offset + irow) / kblock * NPad + iv * 8);
      if constexpr (!_IS_SYM) {
        auto tmp =
            _mm_loadl_epi64(reinterpret_cast<__m128i*>(zero_points + (k_offset + irow) / kblock * NPad + iv * 8));
        vzps[iv] = _mm256_cvtepi8_epi32(tmp);
      }
    }
    auto rowre = row - irow;
    int rowpad4 = utils::padto_le(rowre, UnrollRow) + irow;
    for (; irow < rowpad4; irow += UnrollRow) {
      for (int iter16 = 0; iter16 < Loop16; iter16++)
        pad_bit4_16(tmpbuf + iter16 * 16, reinterpret_cast<int8_t*>(srcptr + irow * ld_src / 2 + 8 * iter16));
      for (int iterr = 0; iterr < UnrollRow; iterr++)
        dequantize(dstptr + (irow + iterr) * ld_dst, tmpbuf + iterr * _NCOL, vscales, vzps);
    }
    for (; irow < row; irow++) {
      if constexpr (_NCOL == 24) {
        pad_bit4_16(tmpbuf, reinterpret_cast<int8_t*>(srcptr + irow * ld_src / 2));
        pad_bit4_8(tmpbuf + 16, reinterpret_cast<int8_t*>(srcptr + irow * ld_src / 2 + 8));
      } else {
        for (int iter16 = 0; iter16 < 3; iter16++)
          pad_bit4_16(tmpbuf + iter16 * 16, reinterpret_cast<int8_t*>(srcptr + irow * ld_src / 2 + 8 * iter16));
      }
      dequantize(dstptr + irow * ld_dst, tmpbuf, vscales, vzps);
    }
  }
  return BTLA_CODE::Success;
}

template <bool _IS_SYM, typename _ST, typename _DST_T>
static inline BTLA_CODE decompress_kblock_bit4_packrow2(utils::bit4x2* srcptr, _DST_T* dstptr, int row, int col,
                                                        int ld_src, int ld_dst, _ST* scales, int8_t* zero_points,
                                                        int k_offset, int kblock, int NPad,
                                                        void (*dequantize)(_DST_T*, int8_t*, __m256*, __m256i*),
                                                        void (*pad_bit4)(int8_t*, int8_t*), int8_t* tmp,
                                                        size_t tmpsize) {
  return BTLA_CODE::NotSupport;
}

template <BTLA_DTYPE _F4_T, typename _DST_T, int _PACK_ROW, typename _ST>
static inline BTLA_CODE decompress_kblock_f4_fp(utils::f4x2* srcptr, _DST_T* dstptr, int row, int col, int ld_src,
                                                int ld_dst, _ST* scales, int k_offset, int kblock, int NPad,
                                                int8_t* tmp, size_t tmpsize) {
  if constexpr (_PACK_ROW == 1) {
    if (col == 24) {
      return decompress_kblock_bit4_packrow1<true, 24, _ST, _DST_T>(
          srcptr, dstptr, row, col, ld_src, ld_dst, scales, nullptr, k_offset, kblock, NPad,
          &dequant_f4_N<24, _DST_T, _F4_T>, fp4_pad_4bit, &ref::convert_s4_s8_8<_F4_T>, tmp, tmpsize);
    }
    if (col == 48) {
      return decompress_kblock_bit4_packrow1<true, 48, _ST, _DST_T>(
          srcptr, dstptr, row, col, ld_src, ld_dst, scales, nullptr, k_offset, kblock, NPad,
          &dequant_f4_N<48, _DST_T, _F4_T>, fp4_pad_4bit, &ref::convert_s4_s8_8<_F4_T>, tmp, tmpsize);
    }
  } else if constexpr (_PACK_ROW == 2) {
    return decompress_kblock_bit4_packrow2<true, _ST, _DST_T>(srcptr, dstptr, row, col, ld_src, ld_dst, scales, nullptr,
                                                              k_offset, kblock, NPad, &dequant_f4_N<64, _DST_T, _F4_T>,
                                                              fp4_pad_4bit, tmp, tmpsize);
  }
  return BTLA_CODE::NotSupport;
}

enum class AVX2_REDUCE_TYPE { MAX, MIN, ADD };
#define AVX2_REDUCE_OP                                                  \
  if constexpr (TYPE == AVX2_REDUCE_TYPE::MAX) x = _mm256_max_ps(x, y); \
  if constexpr (TYPE == AVX2_REDUCE_TYPE::MIN) x = _mm256_min_ps(x, y); \
  if constexpr (TYPE == AVX2_REDUCE_TYPE::ADD) x = _mm256_add_ps(x, y);

template <AVX2_REDUCE_TYPE TYPE>
inline float avx2_reduce_ps(__m256 x) {
  __m256 y = _mm256_permute2f128_ps(x, x, 1);
  AVX2_REDUCE_OP
  y = _mm256_permute_ps(x, 0b01001110);
  AVX2_REDUCE_OP
  y = _mm256_permute_ps(x, 0b10110001);
  AVX2_REDUCE_OP
  return _mm256_cvtss_f32(x);
}

#define AVX2_REDUCE_OP_EPI32(dst, src)                                           \
  if constexpr (TYPE == AVX2_REDUCE_TYPE::MAX) dst = _mm256_max_epi32(dst, src); \
  if constexpr (TYPE == AVX2_REDUCE_TYPE::MIN) dst = _mm256_min_epi32(dst, src); \
  if constexpr (TYPE == AVX2_REDUCE_TYPE::ADD) dst = _mm256_add_epi32(dst, src);

#ifndef _mm256_cvtsi256_si32
#define _mm256_cvtsi256_si32(a) (_mm_cvtsi128_si32(_mm256_castsi256_si128(a)))
#endif

template <AVX2_REDUCE_TYPE TYPE>
inline int avx2_reduce_epi32(__m256i xd) {
  auto x = _mm256_castsi256_ps(xd);
  __m256 y = _mm256_permute2f128_ps(x, x, 1);
  auto yd = _mm256_castps_si256(y);
  AVX2_REDUCE_OP_EPI32(xd, yd);
  x = _mm256_castsi256_ps(xd);
  y = _mm256_permute_ps(x, 0b01001110);
  yd = _mm256_castps_si256(y);
  AVX2_REDUCE_OP_EPI32(xd, yd);
  x = _mm256_castsi256_ps(xd);
  y = _mm256_permute_ps(x, 0b10110001);
  yd = _mm256_castps_si256(y);
  AVX2_REDUCE_OP_EPI32(xd, yd);
  return _mm256_cvtsi256_si32(xd);
}

inline __m128i avx2_cvtepi32_epu8(__m256i x) {
  auto out_v = _mm_packus_epi32(_mm256_castsi256_si128(x), _mm256_extractf128_si256(x, 1));
  out_v = _mm_packus_epi16(out_v, out_v);
  return out_v;
}

template <typename SRC_T>
static inline BTLA_CODE quantize_fp_u8_colblock(int row, int col, const SRC_T* srcptr, int ld_src, uint8_t* dstptr,
                                                int ld_dst, float* scales, int ld_scale, uint8_t* zps, int blocksize,
                                                float* blkreduce) {
  int constexpr VLen = 8;
  auto vff = _mm256_set1_epi32(255);
  auto v0 = _mm256_set1_epi32(0);
  int vblocksize = utils::padto_le(blocksize, VLen);
  int colblk = utils::padto_le(col, blocksize);
  for (int i = 0; i < row; i++) {
    size_t j = 0;
    for (; j < colblk; j += blocksize) {
      __m256 vmaxval = _mm256_set1_ps(0.f);
      __m256 vminval = _mm256_set1_ps(0.f);
      size_t ij = 0;
      for (; ij < vblocksize; ij += VLen) {
        __m256 vsrc;
        if constexpr (std::is_same_v<SRC_T, float>) vsrc = _mm256_loadu_ps(&srcptr[(j + ij) + i * ld_src]);
        if constexpr (std::is_same_v<SRC_T, utils::bf16>) {
          auto vtmp =
              _mm_loadu_si128(reinterpret_cast<__m128i*>(const_cast<utils::bf16*>(&srcptr[(j + ij) + i * ld_src])));
          vsrc = ymm_cvt_bf16_fp32(vtmp);
        }
        vmaxval = _mm256_max_ps(vmaxval, vsrc);
        vminval = _mm256_min_ps(vminval, vsrc);
      }
      auto maxval = avx2_reduce_ps<AVX2_REDUCE_TYPE::MAX>(vmaxval);
      auto minval = avx2_reduce_ps<AVX2_REDUCE_TYPE::MIN>(vminval);
      if (ij < blocksize) {
        for (; ij < blocksize; ij++) {
          auto srcval = (float)srcptr[(j + ij) + i * ld_src];
          maxval = std::max(maxval, srcval);
          minval = std::min(minval, srcval);
        }
      }
      float scale = (maxval - minval) / 255;
      uint8_t zp = utils::cast<float, uint8_t>((0 - minval) / scale);
      scales[j / blocksize + i * ld_scale] = scale;
      zps[j / blocksize + i * ld_scale] = zp;
      int sum = 0;
      float rscale = 1.f / scale;
      auto vrscale = _mm256_set1_ps(rscale);
      auto vdzp = _mm256_set1_epi32(zp);
      ij = 0;
      if (blkreduce) {
        for (; ij < vblocksize; ij += VLen) {
          __m256 vsrc;
          if constexpr (std::is_same_v<SRC_T, float>) vsrc = _mm256_loadu_ps(&srcptr[(j + ij) + i * ld_src]);
          if constexpr (std::is_same_v<SRC_T, utils::bf16>) {
            auto vtmp =
                _mm_loadu_si128(reinterpret_cast<__m128i*>(const_cast<utils::bf16*>(&srcptr[(j + ij) + i * ld_src])));
            vsrc = ymm_cvt_bf16_fp32(vtmp);
          }
          vsrc = _mm256_mul_ps(vsrc, vrscale);
          auto vdsrc = _mm256_cvtps_epi32(vsrc);
          sum += avx2_reduce_epi32<AVX2_REDUCE_TYPE::ADD>(vdsrc);
          vdsrc = _mm256_add_epi32(vdsrc, vdzp);
          vdsrc = _mm256_min_epi32(vdsrc, vff);
          vdsrc = _mm256_max_epi32(vdsrc, v0);
          auto vbsrc = avx2_cvtepi32_epu8(vdsrc);
          _mm_storel_epi64(reinterpret_cast<__m128i*>(&dstptr[(j + ij) + i * ld_dst]), vbsrc);
        }
      } else {
        for (; ij < vblocksize; ij += VLen) {
          __m256 vsrc;
          if constexpr (std::is_same_v<SRC_T, float>) vsrc = _mm256_loadu_ps(&srcptr[(j + ij) + i * ld_src]);
          if constexpr (std::is_same_v<SRC_T, utils::bf16>) {
            auto vtmp =
                _mm_loadu_si128(reinterpret_cast<__m128i*>(const_cast<utils::bf16*>(&srcptr[(j + ij) + i * ld_src])));
            vsrc = ymm_cvt_bf16_fp32(vtmp);
          }
          vsrc = _mm256_mul_ps(vsrc, vrscale);
          auto vdsrc = _mm256_cvtps_epi32(vsrc);
          vdsrc = _mm256_add_epi32(vdsrc, vdzp);
          vdsrc = _mm256_min_epi32(vdsrc, vff);
          vdsrc = _mm256_max_epi32(vdsrc, v0);
          auto vbsrc = avx2_cvtepi32_epu8(vdsrc);
          _mm_storel_epi64(reinterpret_cast<__m128i*>(&dstptr[(j + ij) + i * ld_dst]), vbsrc);
        }
      }
      for (; ij < blocksize; ij++) {
        auto srcval = (float)srcptr[(j + ij) + i * ld_src];
        srcval = srcval * rscale;
        auto srcint = int(roundf(srcval));
        sum += srcint;
        srcint += zp;
        srcint = std::min(srcint, 0xff);
        srcint = std::max(srcint, 0);
        dstptr[(j + ij) + i * ld_dst] = static_cast<uint8_t>(srcint);
      }
      if (blkreduce) {
        blkreduce[j / blocksize + i * ld_scale] = sum * scale;
      }
    }
    if (j < col) {
      float maxval = 0.f;
      float minval = 0.f;
      for (size_t ij = j; ij < col; ij++) {
        maxval = std::max((float)srcptr[ij + i * ld_src], maxval);
        minval = std::min((float)srcptr[ij + i * ld_src], minval);
      }
      float scale = (maxval - minval) / 255;
      uint8_t zp = utils::cast<float, uint8_t>((0 - minval) / scale);
      float rscale = 1.f / scale;
      scales[j / blocksize + i * ld_scale] = scale;
      zps[j / blocksize + i * ld_scale] = zp;
      int sum = 0;
      for (size_t ij = j; ij < col; ij++) {
        auto srcint = utils::cast<float, int>(srcptr[ij + i * ld_src] * rscale);
        sum += srcint;
        srcint += zp;
        srcint = srcint <= 255 ? srcint : 255;
        srcint = srcint >= 0 ? srcint : 0;
        dstptr[ij + i * ld_dst] = utils::cast<int, uint8_t>(srcint);
      }
      if (blkreduce) {
        blkreduce[j / blocksize + i * ld_scale] = sum * scale;
      }
    }
  }
  return BTLA_CODE::Success;
}

template <typename SRC_T>
static inline BTLA_CODE col_block_reduce_sum(const SRC_T* srcptr, int ldsrc, int row, int col, int blocksize,
                                             float* reduce, int ldr) {
  int constexpr VLen = 8;
  auto vblock2_ = utils::padto_le(blocksize, VLen * 2);
  auto vblock_ = utils::padto_le(blocksize, VLen);
  for (int i = 0; i < row; i++) {
    for (int j = 0; j < col; j += blocksize) {
      auto tmp = 0.f;
      auto vsum = _mm256_set1_ps(0.f);
      int jj = 0;
      auto vblock2 = j + vblock2_ <= col ? vblock2_ : 0;
      auto vblock = j + vblock_ <= col ? vblock_ : 0;
      for (; jj < vblock2; jj += VLen * 2) {
        auto vtmp = _mm256_loadu_ps(srcptr + i * ldsrc + j + jj);
        auto vtmp1 = _mm256_loadu_ps(srcptr + i * ldsrc + j + jj + VLen);
        auto s0 = avx2_reduce_ps<AVX2_REDUCE_TYPE::ADD>(vtmp);
        auto s1 = avx2_reduce_ps<AVX2_REDUCE_TYPE::ADD>(vtmp1);
        tmp += s0;
        tmp += s1;
      }
      if (jj + VLen <= vblock) {
        for (; jj < vblock; jj += VLen) {
          auto vtmp = _mm256_loadu_ps(srcptr + i * ldsrc + j + jj);
          auto s0 = avx2_reduce_ps<AVX2_REDUCE_TYPE::ADD>(vtmp);
          tmp += s0;
        }
      }
      for (; jj < blocksize; jj++) {
        tmp += *(srcptr + i * ldsrc + j + jj);
      }
      reduce[i * ldr + j / blocksize] = tmp;
    }
  }
  return BTLA_CODE::Success;
}

static inline BTLA_CODE bf16_cvt_fp32_2D_write_back(const utils::bf16* src_ptr, float* dst_ptr, int row, int col,
                                                    int src_step, int dst_step, bool zeropadding) {
  const int npadding = (dst_step - col) * sizeof(float);
  constexpr int simd_proc_elt = 8;
  auto col_body = col / simd_proc_elt * simd_proc_elt;
  for (int i = 0; i < row; i++) {
    auto src = const_cast<utils::bf16*>(src_ptr + i * src_step);
    auto dst = dst_ptr + i * dst_step;
    int j = 0;
    for (; j < col_body; j += simd_proc_elt) {
      auto bf16_v = _mm_loadu_si128(reinterpret_cast<__m128i*>(src + j));
      auto fp32_v = _mm256_castsi256_ps(_mm256_bslli_epi128(_mm256_cvtepu16_epi32(bf16_v), 2));
      _mm256_storeu_ps(dst + j, fp32_v);
    }
    for (; j < col; j++) {
      *(dst + j) = (src + j)->tofloat();
    }
    if (zeropadding && npadding) std::memset(dst + col, 0, npadding);
  }
  return BTLA_CODE::Success;
}

static const uint8_t avx2_bf16_convert_maigc_num[32] = {
    0x02, 0x03, 0x06, 0x07, 0x0a, 0x0b, 0x0e, 0x0f, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x02, 0x03, 0x06, 0x07, 0x0a, 0x0b, 0x0e, 0x0f, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};

static inline __m128i cvt_fp32_to_bf16(const __m256 src, __m256i* and_helper, __m256i* add_helper) {
  auto shuffle_v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(avx2_bf16_convert_maigc_num));
  auto round_bias = _mm256_castps_si256(src);
  round_bias = _mm256_and_si256(*and_helper, _mm256_srli_si256(round_bias, 2));
  round_bias = _mm256_add_epi32(round_bias, *add_helper);
  auto round_fp32_v = _mm256_add_epi32(_mm256_castps_si256(src), round_bias);
  __m256i trunc_elements = _mm256_shuffle_epi8(round_fp32_v, shuffle_v);
  __m256i ordered = _mm256_permute4x64_epi64(trunc_elements, 0x58);
  return _mm256_castsi256_si128(ordered);
}

static inline BTLA_CODE fp32_cvt_bf16_2D_write_back(const void* raw_srcptr, void* raw_dstptr, int row, int col,
                                                    int srcstride, int dststride, bool zeropadding) {
  auto srcptr = reinterpret_cast<const char*>(raw_srcptr);
  auto dstptr = reinterpret_cast<char*>(raw_dstptr);
  constexpr int simd_proc_elt = 8;
  auto bf16_and_helper = _mm256_set1_epi32(0X00000001);
  auto bf16_add_helper = _mm256_set1_epi32(0x00007FFF);
  auto col_body_loop = col / simd_proc_elt * simd_proc_elt;
  int npadding = dststride - col * sizeof(utils::bf16);
  for (int i = 0; i < row; i++) {
    auto src = srcptr + i * srcstride;
    auto dst = dstptr + i * dststride;
    int j = 0;
    for (; j < col_body_loop; j += simd_proc_elt) {
      auto pack_bf16_value = cvt_fp32_to_bf16(_mm256_loadu_ps(reinterpret_cast<const float*>(src) + j),
                                              &bf16_and_helper, &bf16_add_helper);
      _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + j * sizeof(utils::bf16)), pack_bf16_value);
    }
    for (; j < col; j++) {
      (reinterpret_cast<utils::bf16*>(dst) + j)->fromfloat(*(reinterpret_cast<const float*>(src) + j));
    }
    if (zeropadding && npadding) {
      std::memset(dst + col * sizeof(utils::bf16), 0, npadding);
    }
  }
  return BTLA_CODE::Success;
}

static inline BTLA_CODE layernorm(const float* srcptr, const float* scaleptr, const float* biasptr, float epsilon,
                                  int norm_size, float* dstptr, float* mean_out, float* mean_square_out,
                                  bool simplified) {
  int constexpr VLen = 8;
  int norm_size8 = utils::padto_le(norm_size, VLen);
  int h = 0;
  __m256 vmean = _mm256_setzero_ps(), vmeansq = _mm256_setzero_ps();
  for (; h < norm_size8; h += VLen) {
    auto tmp = _mm256_loadu_ps(srcptr + h);
    vmean = _mm256_add_ps(vmean, tmp);
    tmp = _mm256_mul_ps(tmp, tmp);
    vmeansq = _mm256_add_ps(vmeansq, tmp);
  }
  float mean = avx2_reduce_ps<AVX2_REDUCE_TYPE::ADD>(vmean);
  float mean_square = avx2_reduce_ps<AVX2_REDUCE_TYPE::ADD>(vmeansq);
  for (; h < norm_size; h++) {
    mean += srcptr[h];
    mean_square += srcptr[h] * srcptr[h];
  }
  mean = mean / norm_size;
  if (simplified) {
    mean_square = std::sqrt(mean_square / norm_size + epsilon);
  } else {
    mean_square = std::sqrt(mean_square / norm_size - mean * mean + epsilon);
  }
  auto vm = _mm256_set1_ps(mean);
  float inv_meansq = 1.f / mean_square;
  auto vms = _mm256_set1_ps(inv_meansq);
  h = 0;
  if (simplified) {
    if (scaleptr) {
      for (; h < norm_size8; h += VLen) {
        auto inp = _mm256_loadu_ps(srcptr + h);
        auto scale = _mm256_loadu_ps(scaleptr + h);
        inp = _mm256_mul_ps(inp, scale);
        inp = _mm256_mul_ps(inp, vms);
        _mm256_storeu_ps(dstptr + h, inp);
      }
      for (; h < norm_size; h++) {
        dstptr[h] = srcptr[h] * inv_meansq * scaleptr[h];
      }
    } else {
      for (; h < norm_size8; h += VLen) {
        auto inp = _mm256_loadu_ps(srcptr + h);
        inp = _mm256_mul_ps(inp, vms);
        _mm256_storeu_ps(dstptr + h, inp);
      }
      for (; h < norm_size; h++) {
        dstptr[h] = srcptr[h] * inv_meansq;
      }
    }

  } else {
    if (scaleptr) {
      if (biasptr == nullptr) {
        for (; h < norm_size8; h += VLen) {
          auto inp = _mm256_loadu_ps(srcptr + h);
          auto scale = _mm256_loadu_ps(scaleptr + h);
          inp = _mm256_sub_ps(inp, vm);
          inp = _mm256_mul_ps(inp, scale);
          inp = _mm256_mul_ps(inp, vms);
          _mm256_storeu_ps(dstptr + h, inp);
        }
        for (; h < norm_size; h++) {
          dstptr[h] = (srcptr[h] - mean) * inv_meansq * scaleptr[h];
        }
      } else {
        for (; h < norm_size8; h += VLen) {
          auto inp = _mm256_loadu_ps(srcptr + h);
          auto scale = _mm256_loadu_ps(scaleptr + h);
          inp = _mm256_sub_ps(inp, vm);
          inp = _mm256_mul_ps(inp, vms);
          inp = _mm256_mul_ps(inp, scale);
          auto bias = _mm256_loadu_ps(biasptr + h);
          inp = _mm256_add_ps(inp, bias);
          _mm256_storeu_ps(dstptr + h, inp);
        }
        for (; h < norm_size; h++) {
          dstptr[h] = (srcptr[h] - mean) * inv_meansq * scaleptr[h] + biasptr[h];
        }
      }
    } else {
      for (; h < norm_size8; h += VLen) {
        auto inp = _mm256_loadu_ps(srcptr + h);
        inp = _mm256_sub_ps(inp, vm);
        inp = _mm256_mul_ps(inp, vms);
        _mm256_storeu_ps(dstptr + h, inp);
      }
      for (; h < norm_size; h++) {
        dstptr[h] = (srcptr[h] - mean) * inv_meansq;
      }
    }
  }

  if (mean_out) {
    *mean_out = mean;
  }
  if (mean_square_out) {
    *mean_square_out = mean_square;
  }
  return BTLA_CODE::Success;
}

#ifdef __GNUC__
#pragma GCC pop_options
#else
#endif
#endif
}  // namespace avx2
}  // namespace kernel
}  // namespace bestla
