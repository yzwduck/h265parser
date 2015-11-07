#include "h265const.h"

const char* GetH265NalType(enum H265NalType val) {
  switch (val) {
    case H265_NAL_TYPE_TRAIL_N:
      return "H265_NAL_TYPE_TRAIL_N";
    case H265_NAL_TYPE_TRAIL_R:
      return "H265_NAL_TYPE_TRAIL_R";
    case H265_NAL_TYPE_TSA_N:
      return "H265_NAL_TYPE_TSA_N";
    case H265_NAL_TYPE_TSA_R:
      return "H265_NAL_TYPE_TSA_R";
    case H265_NAL_TYPE_STSA_N:
      return "H265_NAL_TYPE_STSA_N";
    case H265_NAL_TYPE_STSA_R:
      return "H265_NAL_TYPE_STSA_R";
    case H265_NAL_TYPE_RADL_N:
      return "H265_NAL_TYPE_RADL_N";
    case H265_NAL_TYPE_RADL_R:
      return "H265_NAL_TYPE_RADL_R";
    case H265_NAL_TYPE_RASL_N:
      return "H265_NAL_TYPE_RASL_N";
    case H265_NAL_TYPE_RASL_R:
      return "H265_NAL_TYPE_RASL_R";
    case H265_NAL_TYPE_BLA_W_LP:
      return "H265_NAL_TYPE_BLA_W_LP";
    case H265_NAL_TYPE_BLA_W_RADL:
      return "H265_NAL_TYPE_BLA_W_RADL";
    case H265_NAL_TYPE_BLA_N_LP:
      return "H265_NAL_TYPE_BLA_N_LP";
    case H265_NAL_TYPE_IDR_W_RADL:
      return "H265_NAL_TYPE_IDR_W_RADL";
    case H265_NAL_TYPE_IDR_N_LP:
      return "H265_NAL_TYPE_IDR_N_LP";
    case H265_NAL_TYPE_CRA_NUT:
      return "H265_NAL_TYPE_CRA_NUT";
    case H265_NAL_TYPE_VPS_NUT:
      return "H265_NAL_TYPE_VPS_NUT";
    case H265_NAL_TYPE_SPS_NUT:
      return "H265_NAL_TYPE_SPS_NUT";
    case H265_NAL_TYPE_PPS_NUT:
      return "H265_NAL_TYPE_PPS_NUT";
    case H265_NAL_TYPE_AUD_NUT:
      return "H265_NAL_TYPE_AUD_NUT";
    case H265_NAL_TYPE_EOS_NUT:
      return "H265_NAL_TYPE_EOS_NUT";
    case H265_NAL_TYPE_EOB_NUT:
      return "H265_NAL_TYPE_EOB_NUT";
    case H265_NAL_TYPE_FD_NUT:
      return "H265_NAL_TYPE_FD_NUT";
    case H265_NAL_TYPE_PREFIX_SEI_NUT:
      return "H265_NAL_TYPE_PREFIX_SEI_NUT";
    case H265_NAL_TYPE_SUFFIX_SEI_NUT:
      return "H265_NAL_TYPE_SUFFIX_SEI_NUT";
    default:
      return "UNKNOWN";
  }
}
