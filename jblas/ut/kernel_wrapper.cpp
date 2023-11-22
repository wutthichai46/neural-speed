#include "kernel_ut.h"
#include "../kernel_wrapper.h"
namespace jblas {
using namespace utils;
namespace ut {
namespace kernel_wrapper {
class UT_DecompressKBlockS4FP {
 public:
  UT_DecompressKBlockS4FP() {
    UT_START();
    CheckISA(AVX2);
    ut_avx2<S4_CLIP, 1, float, float>(410, 48, 48, 48, 0, 128, 48);
    CheckISA(AVX512F);
    ut<S4_CLIP, 2, float, float>(32, 128, 128, 128, 0, 32, 128);
    ut<S4_CLIP, 1, float, float>(32, 48, 48, 128, 0, 32, 128);
    ut<S4_CLIP, 1, float, utils::bf16>(32, 48, 48, 128, 0, 32, 128);
  }

  template <JBLAS_SIGN_INT_TYPE S4_T, int PACK_ROW, typename ST_T, typename DST_T>
  void ut(int row, int col, int ld_src, int ld_dst, int k_offset, int kblock, int NPad, bool asym = false) {
    printf("Test Case %s_%d_%d: %d %d %d %d %d %d %d %d\n", __FUNCTION__, int(S4_T), PACK_ROW, row, col, ld_src, ld_dst,
           k_offset, kblock, NPad, asym);
    std::vector<utils::int4x2> s4_wei(row * col / 2);
    std::vector<int8_t> s8_wei(col * row);
    std::vector<DST_T> bf16_wei(ld_dst * row);
    std::vector<DST_T> ref_wei(ld_dst * row);
    std::vector<ST_T> scales(col);
    std::vector<int8_t> zero_points(col);
    fill_buffer_randn(s8_wei.data(), s8_wei.size(), int8_t(-128), int8_t(127));
    fill_buffer_randn(scales.data(), scales.size(), ST_T(0.01f), ST_T(0.02f));
    fill_buffer_randn(zero_points.data(), zero_points.size(), (int8_t)(-5), (int8_t)(5));

    for (int i = 0; i < col * row; i += 2) {
      s4_wei[i / 2].x = utils::int4x2::convert(s8_wei[i]);
      s4_wei[i / 2].y = utils::int4x2::convert(s8_wei[i + 1]);
    }
    jblas::kernel::wrapper::DecompressKBlockS4FP<DST_T, PACK_ROW>::template forward<JblasAVX512F, ST_T, S4_T>(
        s4_wei.data(), bf16_wei.data(), row, col, ld_src, ld_dst, scales.data(), asym ? zero_points.data() : nullptr,
        k_offset, kblock, NPad);
    jblas::kernel::wrapper::DecompressKBlockS4FP<DST_T, PACK_ROW>::template forward<JblasNoSIMD, ST_T, S4_T>(
        s4_wei.data(), ref_wei.data(), row, col, ld_src, ld_dst, scales.data(), asym ? zero_points.data() : nullptr,
        k_offset, kblock, NPad);
    ut::buffer_error(ref_wei.data(), bf16_wei.data(), bf16_wei.size(), DST_T(0.01f));
  }

  template <JBLAS_SIGN_INT_TYPE S4_T, int PACK_ROW, typename ST_T, typename DST_T>
  void ut_avx2(int row, int col, int ld_src, int ld_dst, int k_offset, int kblock, int NPad, bool asym = false) {
    printf("Test Case %s_%d_%d: %d %d %d %d %d %d %d %d\n", __FUNCTION__, int(S4_T), PACK_ROW, row, col, ld_src, ld_dst,
           k_offset, kblock, NPad, asym);
    int nk_blk = updiv(row, kblock);
    std::vector<utils::int4x2> s4_wei(row * col / 2);
    std::vector<int8_t> s8_wei(col * row);
    std::vector<DST_T> bf16_wei(ld_dst * row);
    std::vector<DST_T> ref_wei(ld_dst * row);
    std::vector<ST_T> scales(NPad * nk_blk);
    std::vector<int8_t> zero_points(NPad * nk_blk);
    fill_buffer_randn(s8_wei.data(), s8_wei.size(), int8_t(-128), int8_t(127));
    fill_buffer_randn(scales.data(), scales.size(), ST_T(0.01f), ST_T(0.02f));
    fill_buffer_randn(zero_points.data(), zero_points.size(), (int8_t)(-5), (int8_t)(5));

    for (int i = 0; i < col * row; i += 2) {
      s4_wei[i / 2].x = utils::int4x2::convert(s8_wei[i]);
      s4_wei[i / 2].y = utils::int4x2::convert(s8_wei[i + 1]);
    }
    jblas::kernel::wrapper::DecompressKBlockS4FP<DST_T, PACK_ROW>::template forward<JblasNoSIMD, ST_T, S4_T>(
        s4_wei.data(), bf16_wei.data(), row, col, ld_src, ld_dst, scales.data(), asym ? zero_points.data() : nullptr,
        k_offset, kblock, NPad);
    jblas::kernel::wrapper::DecompressKBlockS4FP<DST_T, PACK_ROW>::template forward<JblasAVX512F, ST_T, S4_T>(
        s4_wei.data(), ref_wei.data(), row, col, ld_src, ld_dst, scales.data(), asym ? zero_points.data() : nullptr,
        k_offset, kblock, NPad);
    ut::buffer_error(ref_wei.data(), bf16_wei.data(), bf16_wei.size(), DST_T(0.01f));
  }
};
#ifdef JBLAS_UT_KERNEL_WRAPPER
static UT_DecompressKBlockS4FP sUT_DecompressKBlockS4FP;
#endif

class UT_DecompressKBlockF4FP {
 public:
  UT_DecompressKBlockF4FP() {
    UT_START();
    CheckISA(AVX2);
    ut<float, 1, FP4_BNB, JblasAVX2>(35, 48, 48, 48, 0, 12, 48);
    ut<float, 1, FP4_BNB, JblasAVX2>(11, 48, 48, 48, 0, 20, 48);
    CheckISA(AVX512F);
    ut<float, 1, FP4_BNB, JblasAVX512F>(35, 48, 48, 48, 0, 12, 48);
    ut<float, 1, FP4_BNB, JblasAVX512F>(11, 48, 48, 48, 0, 20, 48);
  }

  template <typename T, int PACK_ROW, JBLAS_F4_TYPE F4_T, JBLAS_ISA ISA_T>
  void ut(int row, int col, int ld_src, int ld_dst, int k_offset, int kblock, int NPad) {
    std::vector<utils::f4x2> f4_wei(row * col / 2);
    std::vector<int8_t> s8_wei(col * row);
    std::vector<T> wei(col * row);
    std::vector<T> ref_wei(col * row);
    std::vector<T> scales(col * updiv(row, kblock));
    fill_buffer_randn(s8_wei.data(), s8_wei.size(), int8_t(-127), int8_t(127));
    fill_buffer_randn(scales.data(), scales.size(), T(1.f), T(10.f));
    for (int i = 0; i < col * row; i += 2) {
      f4_wei[i / 2].x = utils::int4x2::convert(s8_wei[i]);
      f4_wei[i / 2].y = utils::int4x2::convert(s8_wei[i + 1]);
    }
    jblas::kernel::wrapper::DecompressKBlockF4Fp<T, PACK_ROW>::template forward<ISA_T, T, F4_T>(
        f4_wei.data(), wei.data(), row, col, ld_src, ld_dst, scales.data(), k_offset, kblock, NPad);
    jblas::kernel::wrapper::DecompressKBlockF4Fp<T, PACK_ROW>::template forward<JblasNoSIMD, T, F4_T>(
        f4_wei.data(), ref_wei.data(), row, col, ld_src, ld_dst, scales.data(), k_offset, kblock, NPad);
    ut::buffer_error(ref_wei.data(), wei.data(), wei.size(), T(0.01f));
  }
};
#ifdef JBLAS_UT_KERNEL_WRAPPER
static UT_DecompressKBlockF4FP sUT_DecompressKBlockF4FP;
#endif

class UT_PaddingInterleaveMN {
 public:
  UT_PaddingInterleaveMN() {
    UT_START();
    // ut<48, 2, bf16, bf16>(128, 128, 2);  // TO IMPLEMENT
    ut<32, 2, fp16, bf16>(128, 128, 2);
  }
  template <int NTile, int RowPack, typename T_SRC, typename T_DST>
  void ut(int row, int col, int row_tile) {
    printf("%s %d %d %d\n", __FUNCTION__, row, col, row_tile);
    int row_pad = padto(row, row_tile);
    int col_pad = padto(col, NTile);

    aligned_vector<T_SRC> src(row * col);
    aligned_vector<T_DST> dst(row_pad * col_pad), ref(row_pad * col_pad);
    for (size_t i = 0; i < src.size(); i++) src[i] = static_cast<T_SRC>(float(i));

    kernel::wrapper::PaddingInterleaveMN<NTile, RowPack>::template forward<JblasNoSIMD>(
        src.data(), ref.data(), row, col, row_pad, col_pad, row_pad, col);
    kernel::wrapper::PaddingInterleaveMN<NTile, RowPack>::template forward<JblasAVX512_FP16>(
        src.data(), dst.data(), row, col, row_pad, col_pad, col, row_pad);
    ut::buffer_error(dst.data(), ref.data(), dst.size());
  }
};
#ifdef JBLAS_UT_KERNEL_WRAPPER
static UT_PaddingInterleaveMN sUT_PaddingInterleaveMN;
#endif

class UT_PaddingTransInterleaveMN {
 public:
  UT_PaddingTransInterleaveMN() {
    UT_START();
    // ut<48, 2, bf16, bf16>(128, 128, 2);  // TO IMPLEMENT
    ut<32, 2, fp16, bf16>(128, 128, 2);
  }
  template <int MTile, int ColPack, typename T_SRC, typename T_DST>
  void ut(int row, int col, int col_tile) {
    printf("%s %d %d %d\n", __FUNCTION__, row, col, col_tile);
    int row_pad = padto(row, MTile);
    int col_pad = padto(col, col_tile);

    aligned_vector<T_SRC> src(row * col);
    aligned_vector<T_DST> dst(col_pad * row_pad), ref(col_pad * row_pad);
    for (size_t i = 0; i < src.size(); i++) src[i] = static_cast<T_SRC>(float(i));

    kernel::wrapper::PaddingTransInterleaveMN<MTile, ColPack>::template forward<JblasNoSIMD>(
        src.data(), ref.data(), row, col, row_pad, col_pad, row_pad, col);
    kernel::wrapper::PaddingTransInterleaveMN<MTile, ColPack>::template forward<JblasAVX512_FP16>(
        src.data(), dst.data(), row, col, row_pad, col_pad, col, row_pad);
    ut::buffer_error(dst.data(), ref.data(), dst.size());
  }
};
#ifdef JBLAS_UT_KERNEL_WRAPPER
static UT_PaddingTransInterleaveMN sUT_PaddingTransInterleaveMN;
#endif

class UT_RevertPaddingInterleaveMN {
 public:
  UT_RevertPaddingInterleaveMN() {
    UT_START();
    ut<48, 4, char>(128, 128, 4);   // vnni
    ut<48, 1, float>(128, 128, 1);  // 512f
    ut<48, 4, char>(128, 32, 64);   // amxint8
  }
  template <int NTile, int PackRow, typename T>
  void ut(int row, int col, int rowtile) {
    printf("%s %d %d %d\n", __FUNCTION__, row, col, rowtile);
    int rowpad = padto(row, rowtile);
    int colpad = padto(col, NTile);
    aligned_vector<T> src(row * col), packed(rowpad * colpad);
    for (size_t i = 0; i < src.size(); i++) {
      src[i] = static_cast<T>(i);
    }
    aligned_vector<T> reverted(row * col);
    kernel::wrapper::PaddingInterleaveMN<NTile, PackRow>::template forward<JblasNoSIMD>(
        src.data(), packed.data(), row, col, rowpad, colpad, col, rowpad);
    kernel::wrapper::RevertPaddingInterleaveMN<NTile, PackRow>::template forward<JblasNoSIMD>(
        packed.data(), reverted.data(), row, col, rowpad, colpad, rowpad, col);
    ut::buffer_error(src.data(), reverted.data(), reverted.size());
  }
};
#ifdef JBLAS_UT_KERNEL_WRAPPER
static UT_RevertPaddingInterleaveMN sUT_RevertPaddingInterleaveMN;
#endif
}  // namespace kernel_wrapper
}  // namespace ut
}  // namespace jblas
