#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "bitstream.h"
#include "h265const.h"
#include "output-context.h"
#include "h265parser.h"

struct h265_decode_t {
  struct NalUnitHeader nal_unit_header;
  struct H265VideoParameterSet video_param_set;
  struct H265SeqParameterSet seq_param_set;
  struct H265PicParameterSet pic_param_set;
  struct H265SliceSegmentLayer slice_segment;
};

int CeilLog2(uint64_t value) {
  // http://stackoverflow.com/a/3391294
  if (!value) {
    return -1; // no bits set
  }
  int pos = 0;
  if (value & (value - 1ULL)) {
    pos = 1;
  }
  if (value & 0xFFFFFFFF00000000ULL) {
    pos += 32;
    value = value >> 32;
  }
  if (value & 0x00000000FFFF0000ULL) {
    pos += 16;
    value = value >> 16;
  }
  if (value & 0x000000000000FF00ULL) {
    pos += 8;
    value = value >> 8;
  }
  if (value & 0x00000000000000F0ULL) {
    pos += 4;
    value = value >> 4;
  }
  if (value & 0x000000000000000CULL) {
    pos += 2;
    value = value >> 2;
  }
  if (value & 0x0000000000000002ULL) {
    pos += 1;
    value = value >> 1;
  }
  return pos;
}

uint32_t h265_find_next_start_code(uint8_t *pBuf, uint32_t bufLen) {
  uint32_t val;
  uint32_t offset;

  // B.1.1 Byte stream NAL unit syntax
  offset = 0;
  if (pBuf[0] == 0 && pBuf[1] == 0 && pBuf[2] == 0 && pBuf[3] == 1) {
    pBuf += 4;
    offset = 4;
  } else if (pBuf[0] == 0 && pBuf[1] == 0 && pBuf[2] == 1) {
    pBuf += 3;
    offset = 3;
  }
  val = 0xffffffff;
  while (offset < bufLen - 3) {
    val <<= 8;
    val |= *pBuf++;
    offset++;
    if (val == H265_START_CODE) {
      return offset - 4;
    }
    if ((val & 0x00ffffff) == H265_START_CODE) {
      return offset - 3;
    }
  }
  return 0;
}

uint32_t remove_03(uint8_t *r_ptr, uint32_t len) {
  if (len < 3)
    return len;
  uint32_t removed = 0;
  uint8_t *w_ptr = r_ptr;
  uint8_t *ptr_end_padded = r_ptr + len - 2;
  uint8_t *ptr_end = r_ptr + len;
  while (r_ptr < ptr_end_padded) {
    if (r_ptr[0] == 0 && r_ptr[1] == 0 && r_ptr[2] == 3) {
      removed++;
      r_ptr += 3;
      w_ptr += 1;
      *w_ptr++ = 3;
    } else {
      *w_ptr++ = *r_ptr++;
    }
  }
  while (r_ptr != ptr_end) {
    *w_ptr++ = *r_ptr++;
  }
  return len - removed;
}

int h265_nal_unit_header(struct h265_decode_t *dec, struct BitStream *bs,
                         struct OutputContextDict *out) {
  struct NalUnitHeader *head = &dec->nal_unit_header;
  if (BsRemain(bs) < 16) {
    fprintf(stderr, "insufficient buffer");
    return -1;
  }
  if (BsGet(bs, 1) != 0) {
    fprintf(stderr, "nal_unit_header.forbidden_zero_bit != 0");
    return -2;
  }
  head->nal_unit_type = (enum H265NalType)BsGet(bs, 6);
  head->nuh_layer_id = BsGet(bs, 6);
  head->nuh_temporal_id_plus1 = BsGet(bs, 3);
  out->put_enum(out, "nal_unit_type", GetH265NalType(head->nal_unit_type),
                head->nal_unit_type);
  out->put_uint(out, "nuh_layer_id", head->nuh_layer_id);
  out->put_uint(out, "nuh_temporal_id_plus1", head->nuh_temporal_id_plus1);
  return 0;
}

int h265_profile_tier_level(uint8_t profilePresentFlag,
                            uint8_t maxNumSubLayersMinus1,
                            struct h265_decode_t *dec, struct BitStream *bs,
                            struct OutputContextDict *out) {
  // 7.3.3 Profile, tier and level syntax
  int i, j;
  uint8_t general_profile_idc;
  struct H265ProfileTierLevelSubLayer sub_layer[8];
  uint8_t general_profile_compatibility_flag[32];
  if (profilePresentFlag) {
    out->put_uint(out, "general_profile_space",
                  general_profile_idc = BsGet(bs, 2));
    out->put_uint(out, "general_tier_flag", BsGet(bs, 1));
    out->put_uint(out, "general_profile_idc", BsGet(bs, 5));
    for (j = 0; j < 32; j++) {
      general_profile_compatibility_flag[j] = BsGet(bs, 1);
    }
    out->put_uint(out, "general_progressive_source_flag", BsGet(bs, 1));
    out->put_uint(out, "general_interlaced_source_flag", BsGet(bs, 1));
    out->put_uint(out, "general_non_packed_constraint_flag", BsGet(bs, 1));
    out->put_uint(out, "general_frame_only_constraint_flag", BsGet(bs, 1));
    if (general_profile_idc == 4 || general_profile_compatibility_flag[4] ||
        general_profile_idc == 5 || general_profile_compatibility_flag[5] ||
        general_profile_idc == 6 || general_profile_compatibility_flag[6] ||
        general_profile_idc == 7 || general_profile_compatibility_flag[7]) {
      /* The number of bits in this syntax structure is not affected by this
       * condition */
      out->put_uint(out, "general_max_12bit_constraint_flag", BsGet(bs, 1));
      out->put_uint(out, "general_max_10bit_constraint_flag", BsGet(bs, 1));
      out->put_uint(out, "general_max_8bit_constraint_flag", BsGet(bs, 1));
      out->put_uint(out, "general_max_422chroma_constraint_flag", BsGet(bs, 1));
      out->put_uint(out, "general_max_420chroma_constraint_flag", BsGet(bs, 1));
      out->put_uint(out, "general_max_monochrome_constraint_flag",
                    BsGet(bs, 1));
      out->put_uint(out, "general_intra_constraint_flag", BsGet(bs, 1));
      out->put_uint(out, "general_one_picture_only_constraint_flag",
                    BsGet(bs, 1));
      out->put_uint(out, "general_lower_bit_rate_constraint_flag",
                    BsGet(bs, 1));
      // uint8_t general_reserved_zero_34bits = BsGet(bs,34);
      BsGet(bs, 2);
      BsGet(bs, 32);
    } else {
      // uint8_t general_reserved_zero_43bits = BsGet(bs,43);
      BsGet(bs, 11);
      BsGet(bs, 32);
    }
    if ((general_profile_idc >= 1 && general_profile_idc <= 5) ||
        general_profile_compatibility_flag[1] ||
        general_profile_compatibility_flag[2] ||
        general_profile_compatibility_flag[3] ||
        general_profile_compatibility_flag[4] ||
        general_profile_compatibility_flag[5]) {
      /* The number of bits in this syntax structure is not affected by this
       * condition */
      out->put_uint(out, "general_inbld_flag", BsGet(bs, 1));
    } else {
      uint8_t general_reserved_zero_bit = BsGet(bs, 1);
    }
  }
  uint8_t general_level_idc = BsGet(bs, 8);
  for (i = 0; i < maxNumSubLayersMinus1; i++) {
    // ============================
    sub_layer[i].profile_present_flag = BsGet(bs, 1);
    sub_layer[i].level_present_flag = BsGet(bs, 1);
  }
  if (maxNumSubLayersMinus1 > 0) {
    for (i = maxNumSubLayersMinus1; i < 8; i++) {
      uint8_t reserved_zero_2bits = BsGet(bs, 2);
    }
  }
  for (i = 0; i < maxNumSubLayersMinus1; i++) {
    if (sub_layer[i].profile_present_flag) {
      sub_layer[i].profile_space = BsGet(bs, 2);
      sub_layer[i].tier_flag = BsGet(bs, 1);
      sub_layer[i].profile_idc = BsGet(bs, 5);
      for (j = 0; j < 32; j++) {
        sub_layer[i].profile_compatibility_flag[j] = BsGet(bs, 1);
      }
      sub_layer[i].progressive_source_flag = BsGet(bs, 1);
      sub_layer[i].interlaced_source_flag = BsGet(bs, 1);
      sub_layer[i].non_packed_constraint_flag = BsGet(bs, 1);
      sub_layer[i].frame_only_constraint_flag = BsGet(bs, 1);
      if (sub_layer[i].profile_idc == 4 ||
          sub_layer[i].profile_compatibility_flag[4] ||
          sub_layer[i].profile_idc == 5 ||
          sub_layer[i].profile_compatibility_flag[5] ||
          sub_layer[i].profile_idc == 6 ||
          sub_layer[i].profile_compatibility_flag[6] ||
          sub_layer[i].profile_idc == 7 ||
          sub_layer[i].profile_compatibility_flag[7]) {
        /* The number of bits in this syntax structure is not affected by this
         * condition */
        sub_layer[i].max_12bit_constraint_flag = BsGet(bs, 1);
        sub_layer[i].max_10bit_constraint_flag = BsGet(bs, 1);
        sub_layer[i].max_8bit_constraint_flag = BsGet(bs, 1);
        sub_layer[i].max_422chroma_constraint_flag = BsGet(bs, 1);
        sub_layer[i].max_420chroma_constraint_flag = BsGet(bs, 1);
        sub_layer[i].max_monochrome_constraint_flag = BsGet(bs, 1);
        sub_layer[i].intra_constraint_flag = BsGet(bs, 1);
        sub_layer[i].one_picture_only_constraint_flag = BsGet(bs, 1);
        sub_layer[i].lower_bit_rate_constraint_flag = BsGet(bs, 1);
        // uint8_t general_reserved_zero_34bits = BsGet(bs,34);
        BsGet(bs, 2);
        BsGet(bs, 32);
      } else {
        // uint8_t general_reserved_zero_43bits = BsGet(bs,43);
        BsGet(bs, 11);
        BsGet(bs, 32);
      }
      if ((sub_layer[i].profile_idc >= 1 && sub_layer[i].profile_idc <= 5) ||
          sub_layer[i].profile_compatibility_flag ||
          sub_layer[i].profile_compatibility_flag ||
          sub_layer[i].profile_compatibility_flag ||
          sub_layer[i].profile_compatibility_flag ||
          sub_layer[i].profile_compatibility_flag) {
        /* The number of bits in this syntax structure is not affected by this
         * condition */
        sub_layer[i].inbld_flag = BsGet(bs, 1);
      } else {
        uint8_t reserved_zero_bit = BsGet(bs, 1);
      }
    }
    if (sub_layer[i].level_present_flag)
      sub_layer[i].level_idc = BsGet(bs, 8);
  }

  return 0;
}

int h265_scaling_list_data(struct h265_decode_t *dec, struct BitStream *bs,
                           struct OutputContextDict *out) {
  int sizeId;
  for (sizeId = 0; sizeId < 4; sizeId++) {
    int matrixId;
    for (matrixId = 0; matrixId < 6; matrixId += (sizeId == 3) ? 3 : 1) {
      uint8_t scaling_list_pred_mode_flag = BsGet(bs, 1);
      if (!scaling_list_pred_mode_flag) {
        uint32_t scaling_list_pred_matrix_id_delta = BsUe(bs);
      } else {
        int coefNum = 1 << (4 + (sizeId << 1));
        if (64 < coefNum)
          coefNum = 64;
        if (sizeId > 1) {
          int32_t scaling_list_dc_coef_minus8 = BsSe(bs);
        }
        int i;
        for (i = 0; i < coefNum; i++) {
          int32_t scaling_list_delta_coef = BsSe(bs);
        }
      }
    }
  }
  return 0;
};

int h265_ref_pic_set(uint32_t stRpsIdx, struct h265_decode_t *dec,
                     struct BitStream *bs, struct OutputContextDict *out) {
  uint8_t inter_ref_pic_set_prediction_flag = 0;
  uint32_t i, j;

  if (stRpsIdx != 0)
    inter_ref_pic_set_prediction_flag = BsGet(bs, 1);
  if (inter_ref_pic_set_prediction_flag) {
    if (stRpsIdx == dec->seq_param_set.num_short_term_ref_pic_sets) {
      uint32_t delta_idx_minus1 = BsUe(bs);
    }
    uint8_t delta_rps_sign = BsGet(bs, 1);
    uint32_t abs_delta_rps_minus1 = BsUe(bs);
    fprintf(stderr, "Unimplemented ref_pic_set decoding.\n");
    exit(-2);
    /*
    for (j = 0; j <= NumDeltaPocs[RefRpsIdx]; j++) {
      uint8_t used_by_curr_pic_flag = BsGet(bs, 1);
      if (!used_by_curr_pic_flag) {
        uint8_t use_delta_flag = BsGet(bs, 1);
      }
    }
    */
  } else {
    uint32_t num_negative_pics = BsUe(bs);
    uint32_t num_positive_pics = BsUe(bs);
    for (i = 0; i < num_negative_pics; i++) {
      uint32_t delta_poc_s0_minus1 = BsUe(bs);
      uint8_t used_by_curr_pic_s0_flag = BsGet(bs, 1);
    }
    for (i = 0; i < num_positive_pics; i++) {
      uint32_t delta_poc_s1_minus1 = BsUe(bs);
      uint8_t used_by_curr_pic_s1_flag = BsGet(bs, 1);
    }
  }

  return 0;
}

int h265_sub_layer_hrd_parameters(uint8_t subLayerId, uint32_t CpbCnt,
                                  struct h265_decode_t *dec,
                                  struct BitStream *bs,
                                  struct OutputContextDict *out) {
  uint32_t i;
  //  CpbCnt is set equal to cpb_cnt_minus1[ subLayerId ]
  for (i = 0; i <= CpbCnt; i++) {
    uint32_t bit_rate_value_minus1 = BsUe(bs);
    uint32_t cpb_size_value_minus1 = BsUe(bs);
    if (dec->seq_param_set.vui_param.hrd_parameters
            .sub_pic_hrd_params_present_flag) {
      uint32_t cpb_size_du_value_minus1 = BsUe(bs);
      uint32_t bit_rate_du_value_minus1 = BsUe(bs);
    }
    uint8_t cbr_flag = BsGet(bs, 1);
  }
  return 0;
}

int h265_hrd_parameters(uint8_t commonInfPresentFlag,
                        uint8_t maxNumSubLayersMinus1,
                        struct H265HrdParameters *hrd,
                        struct h265_decode_t *dec, struct BitStream *bs,
                        struct OutputContextDict *out) {
  // E.2.2 HRD parameters syntax
  int i;
  if (commonInfPresentFlag) {
    out->put_uint(out, "nal_hrd_parameters_present_flag",
                  hrd->nal_hrd_parameters_present_flag = BsGet(bs, 1));
    out->put_uint(out, "vcl_hrd_parameters_present_flag",
                  hrd->vcl_hrd_parameters_present_flag = BsGet(bs, 1));
    if (hrd->nal_hrd_parameters_present_flag ||
        hrd->vcl_hrd_parameters_present_flag) {
      out->put_uint(out, "sub_pic_hrd_params_present_flag",
                    hrd->sub_pic_hrd_params_present_flag = BsGet(bs, 1));
      if (hrd->sub_pic_hrd_params_present_flag) {
        out->put_uint(out, "tick_divisor_minus2",
                      hrd->tick_divisor_minus2 = BsGet(bs, 8));
        out->put_uint(out, "du_cpb_removal_delay_increment_length_minus1",
                      hrd->du_cpb_removal_delay_increment_length_minus1 =
                          BsGet(bs, 5));
        out->put_uint(out, "sub_pic_cpb_params_in_pic_timing_sei_flag",
                      hrd->sub_pic_cpb_params_in_pic_timing_sei_flag =
                          BsGet(bs, 1));
        out->put_uint(out, "dpb_output_delay_du_length_minus1",
                      hrd->dpb_output_delay_du_length_minus1 = BsGet(bs, 5));
      }
      out->put_uint(out, "bit_rate_scale", hrd->bit_rate_scale = BsGet(bs, 4));
      out->put_uint(out, "cpb_size_scale", hrd->cpb_size_scale = BsGet(bs, 4));
      if (hrd->sub_pic_hrd_params_present_flag) {
        out->put_uint(out, "cpb_size_du_scale",
                      hrd->cpb_size_du_scale = BsGet(bs, 4));
      }
      out->put_uint(out, "initial_cpb_removal_delay_length_minus1",
                    hrd->initial_cpb_removal_delay_length_minus1 =
                        BsGet(bs, 5));
      out->put_uint(out, "au_cpb_removal_delay_length_minus1",
                    hrd->au_cpb_removal_delay_length_minus1 = BsGet(bs, 5));
      out->put_uint(out, "dpb_output_delay_length_minus1",
                    hrd->dpb_output_delay_length_minus1 = BsGet(bs, 5));
    }
  }
  for (i = 0; i <= maxNumSubLayersMinus1; i++) {
    out->put_uint(out, "fixed_pic_rate_general_flag",
                  hrd->fixed_pic_rate_general_flag = BsGet(bs, 1));
    if (!hrd->fixed_pic_rate_general_flag) {
      out->put_uint(out, "fixed_pic_rate_within_cvs_flag",
                    hrd->fixed_pic_rate_within_cvs_flag = BsGet(bs, 1));
    }
    if (hrd->fixed_pic_rate_within_cvs_flag) {
      out->put_uint(out, "elemental_duration_in_tc_minus1",
                    hrd->elemental_duration_in_tc_minus1 = BsUe(bs));
    } else {
      out->put_uint(out, "low_delay_hrd_flag",
                    hrd->low_delay_hrd_flag = BsGet(bs, 1));
    }
    if (!hrd->low_delay_hrd_flag) {
      out->put_uint(out, "cpb_cnt_minus1", hrd->cpb_cnt_minus1 = BsUe(bs));
    }
    if (hrd->nal_hrd_parameters_present_flag) {
      h265_sub_layer_hrd_parameters(i, hrd->cpb_cnt_minus1, dec, bs, out);
    }
    if (hrd->vcl_hrd_parameters_present_flag) {
      h265_sub_layer_hrd_parameters(i, hrd->cpb_cnt_minus1, dec, bs, out);
    }
  }
  return 0;
}

int h265_vui_parameters(struct h265_decode_t *dec, struct BitStream *bs,
                        struct OutputContextDict *out) {
  struct H265VuiParameters *vui = &dec->seq_param_set.vui_param;
  out->put_uint(out, "aspect_ratio_info_present_flag",
                vui->aspect_ratio_info_present_flag = BsGet(bs, 1));
  if (vui->aspect_ratio_info_present_flag) {
    out->put_uint(out, "aspect_ratio_idc",
                  vui->aspect_ratio_idc = BsGet(bs, 8));
    if (vui->aspect_ratio_idc == EXTENDED_SAR) {
      out->put_uint(out, "sar_width", vui->sar_width = BsGet(bs, 16));
      out->put_uint(out, "sar_height", vui->sar_height = BsGet(bs, 16));
    }
  }
  out->put_uint(out, "overscan_info_present_flag",
                vui->overscan_info_present_flag = BsGet(bs, 1));
  if (vui->overscan_info_present_flag) {
    out->put_uint(out, "overscan_appropriate_flag",
                  vui->overscan_appropriate_flag = BsGet(bs, 1));
  }
  out->put_uint(out, "video_signal_type_present_flag",
                vui->video_signal_type_present_flag = BsGet(bs, 1));
  if (vui->video_signal_type_present_flag) {
    out->put_uint(out, "video_format", vui->video_format = BsGet(bs, 3));
    out->put_uint(out, "video_full_range_flag",
                  vui->video_full_range_flag = BsGet(bs, 1));
    out->put_uint(out, "colour_description_present_flag",
                  vui->colour_description_present_flag = BsGet(bs, 1));
    if (vui->colour_description_present_flag) {
      out->put_uint(out, "colour_primaries",
                    vui->colour_primaries = BsGet(bs, 8));
      out->put_uint(out, "transfer_characteristics",
                    vui->transfer_characteristics = BsGet(bs, 8));
      out->put_uint(out, "matrix_coeffs", vui->matrix_coeffs = BsGet(bs, 8));
    }
  }
  out->put_uint(out, "chroma_loc_info_present_flag",
                vui->chroma_loc_info_present_flag = BsGet(bs, 1));
  if (vui->chroma_loc_info_present_flag) {
    out->put_uint(out, "chroma_sample_loc_type_top_field",
                  vui->chroma_sample_loc_type_top_field = BsUe(bs));
    out->put_uint(out, "chroma_sample_loc_type_bottom_field",
                  vui->chroma_sample_loc_type_bottom_field = BsUe(bs));
  }
  out->put_uint(out, "neutral_chroma_indication_flag",
                vui->neutral_chroma_indication_flag = BsGet(bs, 1));
  out->put_uint(out, "field_seq_flag", vui->field_seq_flag = BsGet(bs, 1));
  out->put_uint(out, "frame_field_info_present_flag",
                vui->frame_field_info_present_flag = BsGet(bs, 1));
  out->put_uint(out, "default_display_window_flag",
                vui->default_display_window_flag = BsGet(bs, 1));
  if (vui->default_display_window_flag) {
    out->put_uint(out, "def_disp_win_left_offset",
                  vui->def_disp_win_left_offset = BsUe(bs));
    out->put_uint(out, "def_disp_win_right_offset",
                  vui->def_disp_win_right_offset = BsUe(bs));
    out->put_uint(out, "def_disp_win_top_offset",
                  vui->def_disp_win_top_offset = BsUe(bs));
    out->put_uint(out, "def_disp_win_bottom_offset",
                  vui->def_disp_win_bottom_offset = BsUe(bs));
  }
  out->put_uint(out, "vui_timing_info_present_flag",
                vui->vui_timing_info_present_flag = BsGet(bs, 1));
  if (vui->vui_timing_info_present_flag) {
    out->put_uint(out, "vui_num_units_in_tick",
                  vui->vui_num_units_in_tick = BsGet(bs, 32));
    out->put_uint(out, "vui_time_scale", vui->vui_time_scale = BsGet(bs, 32));
    out->put_uint(out, "vui_poc_proportional_to_timing_flag",
                  vui->vui_poc_proportional_to_timing_flag = BsGet(bs, 1));
    if (vui->vui_poc_proportional_to_timing_flag) {
      out->put_uint(out, "vui_num_ticks_poc_diff_one_minus1",
                    vui->vui_num_ticks_poc_diff_one_minus1 = BsUe(bs));
    }

    out->put_uint(out, "vui_hrd_parameters_present_flag",
                  vui->vui_hrd_parameters_present_flag = BsGet(bs, 1));
    if (vui->vui_hrd_parameters_present_flag)
      h265_hrd_parameters(1, dec->seq_param_set.sps_max_sub_layers_minus1,
                          &dec->seq_param_set.vui_param.hrd_parameters, dec, bs,
                          out);
  }
  out->put_uint(out, "bitstream_restriction_flag",
                vui->bitstream_restriction_flag = BsGet(bs, 1));
  if (vui->bitstream_restriction_flag) {
    out->put_uint(out, "tiles_fixed_structure_flag",
                  vui->tiles_fixed_structure_flag = BsGet(bs, 1));
    out->put_uint(out, "motion_vectors_over_pic_boundaries_flag",
                  vui->motion_vectors_over_pic_boundaries_flag = BsGet(bs, 1));
    out->put_uint(out, "restricted_ref_pic_lists_flag",
                  vui->restricted_ref_pic_lists_flag = BsGet(bs, 1));
    out->put_uint(out, "min_spatial_segmentation_idc",
                  vui->min_spatial_segmentation_idc = BsUe(bs));
    out->put_uint(out, "max_bytes_per_pic_denom",
                  vui->max_bytes_per_pic_denom = BsUe(bs));
    out->put_uint(out, "max_bits_per_min_cu_denom",
                  vui->max_bits_per_min_cu_denom = BsUe(bs));
    out->put_uint(out, "log2_max_mv_length_horizontal",
                  vui->log2_max_mv_length_horizontal = BsUe(bs));
    out->put_uint(out, "log2_max_mv_length_vertical",
                  vui->log2_max_mv_length_vertical = BsUe(bs));
  }
  return 0;
}

int h265_seq_parameter_set(struct h265_decode_t *dec, struct BitStream *bs,
                           struct OutputContextDict *out) {
  uint32_t i;
  struct H265SeqParameterSet *sps = &dec->seq_param_set;
  struct OutputContextDict subdict[1];
  out->put_uint(out, "sps_video_parameter_set_id",
                sps->sps_video_parameter_set_id = BsGet(bs, 4));
  out->put_uint(out, "sps_max_sub_layers_minus1",
                sps->sps_max_sub_layers_minus1 = BsGet(bs, 3));
  out->put_uint(out, "sps_temporal_id_nesting_flag",
                sps->sps_temporal_id_nesting_flag = BsGet(bs, 1));

  out->put_dict(out, "profile_tier_level", subdict);
  h265_profile_tier_level(1, sps->sps_max_sub_layers_minus1, dec, bs, subdict);
  subdict->end(subdict);

  out->put_uint(out, "sps_seq_parameter_set_id",
                sps->sps_seq_parameter_set_id = BsUe(bs));
  out->put_uint(out, "chroma_format_idc", sps->chroma_format_idc = BsUe(bs));
  if (sps->chroma_format_idc == 3) {
    out->put_uint(out, "separate_colour_plane_flag",
                  sps->separate_colour_plane_flag = BsGet(bs, 1));
  }
  out->put_uint(out, "ChromaArrayType",
                sps->ChromaArrayType = (sps->separate_colour_plane_flag == 0)
                                           ? sps->chroma_format_idc
                                           : 0);
  out->put_uint(out, "pic_width_in_luma_samples",
                sps->pic_width_in_luma_samples = BsUe(bs));
  out->put_uint(out, "pic_height_in_luma_samples",
                sps->pic_height_in_luma_samples = BsUe(bs));
  out->put_uint(out, "conformance_window_flag",
                sps->conformance_window_flag = BsGet(bs, 1));
  if (sps->conformance_window_flag) {
    out->put_uint(out, "conf_win_left_offset",
                  sps->conf_win_left_offset = BsUe(bs));
    out->put_uint(out, "conf_win_right_offset",
                  sps->conf_win_right_offset = BsUe(bs));
    out->put_uint(out, "conf_win_top_offset",
                  sps->conf_win_top_offset = BsUe(bs));
    out->put_uint(out, "conf_win_bottom_offset",
                  sps->conf_win_bottom_offset = BsUe(bs));
  }
  out->put_uint(out, "bit_depth_luma_minus8",
                sps->bit_depth_luma_minus8 = BsUe(bs));
  out->put_uint(out, "bit_depth_chroma_minus8",
                sps->bit_depth_chroma_minus8 = BsUe(bs));
  out->put_uint(out, "log2_max_pic_order_cnt_lsb_minus4",
                sps->log2_max_pic_order_cnt_lsb_minus4 = BsUe(bs));
  out->put_uint(out, "sps_sub_layer_ordering_info_present_flag",
                sps->sps_sub_layer_ordering_info_present_flag = BsGet(bs, 1));
  for (i = (sps->sps_sub_layer_ordering_info_present_flag
                ? 0
                : dec->seq_param_set.sps_max_sub_layers_minus1);
       i <= dec->seq_param_set.sps_max_sub_layers_minus1; i++) {
    uint32_t sps_max_dec_pic_buffering_minus1 = BsUe(bs);
    uint32_t sps_max_num_reorder_pics = BsUe(bs);
    uint32_t sps_max_latency_increase_plus1 = BsUe(bs);
  }
  out->put_uint(out, "log2_min_luma_coding_block_size_minus3",
                sps->log2_min_luma_coding_block_size_minus3 = BsUe(bs));
  out->put_uint(out, "log2_diff_max_min_luma_coding_block_size",
                sps->log2_diff_max_min_luma_coding_block_size = BsUe(bs));
  out->put_uint(out, "log2_min_luma_transform_block_size_minus2",
                sps->log2_min_luma_transform_block_size_minus2 = BsUe(bs));
  out->put_uint(out, "log2_diff_max_min_luma_transform_block_size",
                sps->log2_diff_max_min_luma_transform_block_size = BsUe(bs));
  out->put_uint(out, "max_transform_hierarchy_depth_inter",
                sps->max_transform_hierarchy_depth_inter = BsUe(bs));
  out->put_uint(out, "max_transform_hierarchy_depth_intra",
                sps->max_transform_hierarchy_depth_intra = BsUe(bs));
  out->put_uint(out, "scaling_list_enabled_flag",
                sps->scaling_list_enabled_flag = BsGet(bs, 1));
  if (sps->scaling_list_enabled_flag) {
    out->put_uint(out, "sps_scaling_list_data_present_flag",
                  sps->sps_scaling_list_data_present_flag = BsGet(bs, 1));
    if (sps->sps_scaling_list_data_present_flag) {
      h265_scaling_list_data(dec, bs, out);
    }
  }
  out->put_uint(out, "amp_enabled_flag", sps->amp_enabled_flag = BsGet(bs, 1));
  out->put_uint(out, "sample_adaptive_offset_enabled_flag",
                sps->sample_adaptive_offset_enabled_flag = BsGet(bs, 1));
  out->put_uint(out, "pcm_enabled_flag", sps->pcm_enabled_flag = BsGet(bs, 1));
  if (sps->pcm_enabled_flag) {
    out->put_uint(out, "pcm_sample_bit_depth_luma_minus1",
                  sps->pcm_sample_bit_depth_luma_minus1 = BsGet(bs, 4));

    // ===========================

    out->put_uint(out, "pcm_sample_bit_depth_chroma_minus1",
                  sps->pcm_sample_bit_depth_chroma_minus1 = BsGet(bs, 4));
    out->put_uint(out, "log2_min_pcm_luma_coding_block_size_minus3",
                  sps->log2_min_pcm_luma_coding_block_size_minus3 = BsUe(bs));
    out->put_uint(out, "log2_diff_max_min_pcm_luma_coding_block_size",
                  sps->log2_diff_max_min_pcm_luma_coding_block_size = BsUe(bs));
    out->put_uint(out, "pcm_loop_filter_disabled_flag",
                  sps->pcm_loop_filter_disabled_flag = BsGet(bs, 1));
  }
  sps->num_short_term_ref_pic_sets = BsUe(bs);
  out->put_uint(out, "num_short_term_ref_pic_sets",
                sps->num_short_term_ref_pic_sets);
  for (i = 0; i < sps->num_short_term_ref_pic_sets; i++) {
    // TODO: phrase st_ref_pic_set
    // st_ref_pic_set(i);
  }
  out->put_uint(out, "long_term_ref_pics_present_flag",
                sps->long_term_ref_pics_present_flag = BsGet(bs, 1));
  if (sps->long_term_ref_pics_present_flag) {
    out->put_uint(out, "num_long_term_ref_pics_sps",
                  sps->num_long_term_ref_pics_sps = BsUe(bs));
    for (i = 0; i < sps->num_long_term_ref_pics_sps; i++) {
      uint32_t lt_ref_pic_poc_lsb_sps =
          BsGet(bs, sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
      uint8_t used_by_curr_pic_lt_sps_flag = BsGet(bs, 1);
    }
  }
  out->put_uint(out, "sps_temporal_mvp_enabled_flag",
                sps->sps_temporal_mvp_enabled_flag = BsGet(bs, 1));
  out->put_uint(out, "strong_intra_smoothing_enabled_flag",
                sps->strong_intra_smoothing_enabled_flag = BsGet(bs, 1));
  out->put_uint(out, "vui_parameters_present_flag",
                sps->vui_parameters_present_flag = BsGet(bs, 1));
  if (sps->vui_parameters_present_flag) {
    out->put_dict(out, "vui_parameters", subdict);
    h265_vui_parameters(dec, bs, subdict);
    subdict->end(subdict);
  }
  out->put_uint(out, "sps_extension_present_flag",
                sps->sps_extension_present_flag = BsGet(bs, 1));
  if (sps->sps_extension_present_flag) {
    uint8_t sps_range_extension_flag = BsGet(bs, 1);
    uint8_t sps_multilayer_extension_flag = BsGet(bs, 1);
    uint8_t sps_extension_6bits = BsGet(bs, 6);
  }
  /*
  if (sps_range_extension_flag)
    sps_range_extension()
    if (sps_multilayer_extension_flag)
      sps_multilayer_extension() // specified in Annex F
      if (sps_extension_6bits)
        while (more_rbsp_data())
          uint8_t sps_extension_data_flag = BsGet(bs, 1);
  rbsp_trailing_bits()
  */

  // =======================
  // P20, Table 6-1
  if (sps->separate_colour_plane_flag == 0) {
    switch (sps->chroma_format_idc) {
    case 0:
      sps->SubWidthC = 1;
      sps->SubHeightC = 1;
      break;
    case 1:
      sps->SubWidthC = 2;
      sps->SubHeightC = 2;
      break;
    case 2:
      sps->SubWidthC = 2;
      sps->SubHeightC = 1;
      break;
    case 3:
      sps->SubWidthC = 1;
      sps->SubHeightC = 1;
      break;
    }
  } else if (sps->separate_colour_plane_flag == 1 &&
             sps->chroma_format_idc == 3) {
    sps->SubWidthC = 1;
    sps->SubHeightC = 1;
  }
  out->put_uint(out, "SubWidthC", sps->SubWidthC);
  out->put_uint(out, "SubHeightC", sps->SubHeightC);

  // P74, (7-10) ~ (7-22)
  sps->MinCbLog2SizeY = sps->log2_min_luma_coding_block_size_minus3 + 3;
  sps->CtbLog2SizeY =
      sps->MinCbLog2SizeY + sps->log2_diff_max_min_luma_coding_block_size;
  sps->MinCbSizeY = 1 << sps->MinCbLog2SizeY;
  sps->CtbSizeY = 1 << sps->CtbLog2SizeY;
  sps->PicWidthInMinCbsY = sps->pic_width_in_luma_samples / sps->MinCbSizeY;
  sps->PicWidthInCtbsY =
      (uint32_t)ceil(1.0 * sps->pic_width_in_luma_samples / sps->CtbSizeY);
  sps->PicHeightInMinCbsY = sps->pic_height_in_luma_samples / sps->MinCbSizeY;
  sps->PicHeightInCtbsY =
      (uint32_t)ceil(1.0 * sps->pic_height_in_luma_samples / sps->CtbSizeY);
  sps->PicSizeInMinCbsY = sps->PicWidthInMinCbsY * sps->PicHeightInMinCbsY;
  sps->PicSizeInCtbsY = sps->PicWidthInCtbsY * sps->PicHeightInCtbsY;
  sps->PicSizeInSamplesY =
      sps->pic_width_in_luma_samples * sps->pic_height_in_luma_samples;
  sps->PicWidthInSamplesC = sps->pic_width_in_luma_samples / sps->SubWidthC;
  sps->PicHeightInSamplesC = sps->pic_height_in_luma_samples / sps->SubHeightC;
  out->put_uint(out, "MinCbLog2SizeY", sps->MinCbLog2SizeY);
  out->put_uint(out, "CtbLog2SizeY", sps->CtbLog2SizeY);
  out->put_uint(out, "MinCbSizeY", sps->MinCbSizeY);
  out->put_uint(out, "CtbSizeY", sps->CtbSizeY);
  out->put_uint(out, "PicWidthInMinCbsY", sps->PicWidthInMinCbsY);
  out->put_uint(out, "PicWidthInCtbsY", sps->PicWidthInCtbsY);
  out->put_uint(out, "PicHeightInMinCbsY", sps->PicHeightInMinCbsY);
  out->put_uint(out, "PicHeightInCtbsY", sps->PicHeightInCtbsY);
  out->put_uint(out, "PicSizeInMinCbsY", sps->PicSizeInMinCbsY);
  out->put_uint(out, "PicSizeInCtbsY", sps->PicSizeInCtbsY);
  out->put_uint(out, "PicSizeInSamplesY", sps->PicSizeInSamplesY);
  out->put_uint(out, "PicWidthInSamplesC", sps->PicWidthInSamplesC);
  out->put_uint(out, "PicHeightInSamplesC", sps->PicHeightInSamplesC);
  return 0;
}

int h265_video_parameter_set(struct h265_decode_t *dec, struct BitStream *bs,
                             struct OutputContextDict *out) {
  uint32_t i, j;
  struct H265VideoParameterSet *vps = &dec->video_param_set;
  out->put_uint(out, "vps_video_parameter_set_id",
                vps->vps_video_parameter_set_id = BsGet(bs, 4));
  out->put_uint(out, "vps_base_layer_internal_flag",
                vps->vps_base_layer_internal_flag = BsGet(bs, 1));
  out->put_uint(out, "vps_base_layer_available_flag",
                vps->vps_base_layer_available_flag = BsGet(bs, 1));
  out->put_uint(out, "vps_max_layers_minus1",
                vps->vps_max_layers_minus1 = BsGet(bs, 6));
  out->put_uint(out, "vps_max_sub_layers_minus1",
                vps->vps_max_sub_layers_minus1 = BsGet(bs, 3));
  out->put_uint(out, "vps_temporal_id_nesting_flag",
                vps->vps_temporal_id_nesting_flag = BsGet(bs, 1));
  out->put_hex(out, "vps_reserved_0xffff_16bits",
               vps->vps_reserved_0xffff_16bits = BsGet(bs, 16));
  h265_profile_tier_level(1, vps->vps_max_sub_layers_minus1, dec, bs, out);
  out->put_uint(out, "vps_sub_layer_ordering_info_present_flag",
                vps->vps_sub_layer_ordering_info_present_flag = BsGet(bs, 1));

  for (i = (vps->vps_sub_layer_ordering_info_present_flag
                ? 0
                : vps->vps_max_sub_layers_minus1);
       i <= vps->vps_max_sub_layers_minus1; i++) {
    uint32_t vps_max_dec_pic_buffering_minus1 = BsUe(bs);
    uint32_t vps_max_num_reorder_pics = BsUe(bs);
    uint32_t vps_max_latency_increase_plus1 = BsUe(bs);
  }
  out->put_uint(out, "vps_max_layer_id", vps->vps_max_layer_id = BsGet(bs, 6));
  out->put_uint(out, "vps_num_layer_sets_minus1",
                vps->vps_num_layer_sets_minus1 = BsUe(bs));
  for (i = 1; i <= vps->vps_num_layer_sets_minus1; i++) {
    for (j = 0; j <= vps->vps_max_layer_id; j++) {
      uint8_t layer_id_included_flag = BsGet(bs, 1);
    }
  }
  out->put_uint(out, "vps_timing_info_present_flag",
                vps->vps_timing_info_present_flag = BsGet(bs, 1));
  if (vps->vps_timing_info_present_flag) {
    out->put_uint(out, "vps_num_units_in_tick",
                  vps->vps_num_units_in_tick = BsGet(bs, 32));
    out->put_uint(out, "vps_time_scale", vps->vps_time_scale = BsGet(bs, 32));
    out->put_uint(out, "vps_poc_proportional_to_timing_flag",
                  vps->vps_poc_proportional_to_timing_flag = BsGet(bs, 1));
    if (vps->vps_poc_proportional_to_timing_flag) {
      out->put_uint(out, "vps_num_ticks_poc_diff_one_minus1",
                    vps->vps_num_ticks_poc_diff_one_minus1 = BsUe(bs));
    }
    out->put_uint(out, "vps_num_hrd_parameters",
                  vps->vps_num_hrd_parameters = BsUe(bs));

    struct OutputContextList list[1];
    out->put_list(out, "hrd_parameters", list);
    for (i = 0; i < vps->vps_num_hrd_parameters; i++) {
      uint8_t hrd_layer_set_idx = BsUe(bs);
      uint8_t cprms_present_flag = 0;
      if (i > 0) {
        cprms_present_flag = BsGet(bs, 1);
      }
      struct H265HrdParameters hrd;
      struct OutputContextDict subdict[1];
      list->put_dict(list, subdict);
      h265_hrd_parameters(cprms_present_flag, vps->vps_max_sub_layers_minus1,
                          &hrd, dec, bs, subdict);
      subdict->end(subdict);
    }
    list->end(list);
  }
  out->put_uint(out, "vps_extension_flag",
                vps->vps_extension_flag = BsGet(bs, 1));
  return 0;
}

int h265_pps_range_extension(struct h265_decode_t *dec, struct BitStream *bs,
                             struct OutputContextDict *out) {
  uint32_t i;
  struct H265PicParameterSet *pps = &dec->pic_param_set;
  if (pps->transform_skip_enabled_flag) {
    out->put_uint(out, "log2_max_transform_skip_block_size_minus2",
                  pps->log2_max_transform_skip_block_size_minus2 = BsUe(bs));
  }
  out->put_uint(out, "cross_component_prediction_enabled_flag",
                pps->cross_component_prediction_enabled_flag = BsGet(bs, 1));
  out->put_uint(out, "chroma_qp_offset_list_enabled_flag",
                pps->chroma_qp_offset_list_enabled_flag = BsGet(bs, 1));
  if (pps->chroma_qp_offset_list_enabled_flag) {
    out->put_uint(out, "diff_cu_chroma_qp_offset_depth",
                  pps->diff_cu_chroma_qp_offset_depth = BsUe(bs));
    out->put_uint(out, "chroma_qp_offset_list_len_minus1",
                  pps->chroma_qp_offset_list_len_minus1 = BsUe(bs));
    for (i = 0; i <= pps->chroma_qp_offset_list_len_minus1; i++) {
      int32_t cb_qp_offset_list = BsSe(bs);
      int32_t cr_qp_offset_list = BsSe(bs);
    }
  }
  out->put_uint(out, "log2_sao_offset_scale_luma",
                pps->log2_sao_offset_scale_luma = BsUe(bs));
  out->put_uint(out, "log2_sao_offset_scale_chroma",
                pps->log2_sao_offset_scale_chroma = BsUe(bs));
  return 0;
}

int h265_pic_parameter_set(struct h265_decode_t *dec, struct BitStream *bs,
                           struct OutputContextDict *out) {
  uint32_t i;
  struct H265PicParameterSet *pps = &dec->pic_param_set;
  out->put_uint(out, "pps_pic_parameter_set_id",
                pps->pps_pic_parameter_set_id = BsUe(bs));
  out->put_uint(out, "pps_seq_parameter_set_id",
                pps->pps_seq_parameter_set_id = BsUe(bs));
  out->put_uint(out, "dependent_slice_segments_enabled_flag",
                pps->dependent_slice_segments_enabled_flag = BsGet(bs, 1));
  out->put_uint(out, "output_flag_present_flag",
                pps->output_flag_present_flag = BsGet(bs, 1));
  out->put_uint(out, "num_extra_slice_header_bits",
                pps->num_extra_slice_header_bits = BsGet(bs, 3));
  out->put_uint(out, "sign_data_hiding_enabled_flag",
                pps->sign_data_hiding_enabled_flag = BsGet(bs, 1));
  out->put_uint(out, "cabac_init_present_flag",
                pps->cabac_init_present_flag = BsGet(bs, 1));
  out->put_uint(out, "num_ref_idx_l0_default_active_minus1",
                pps->num_ref_idx_l0_default_active_minus1 = BsUe(bs));
  out->put_uint(out, "num_ref_idx_l1_default_active_minus1",
                pps->num_ref_idx_l1_default_active_minus1 = BsUe(bs));
  out->put_int(out, "init_qp_minus26", pps->init_qp_minus26 = BsSe(bs));
  out->put_uint(out, "constrained_intra_pred_flag",
                pps->constrained_intra_pred_flag = BsGet(bs, 1));
  out->put_uint(out, "transform_skip_enabled_flag",
                pps->transform_skip_enabled_flag = BsGet(bs, 1));
  out->put_uint(out, "cu_qp_delta_enabled_flag",
                pps->cu_qp_delta_enabled_flag = BsGet(bs, 1));
  if (pps->cu_qp_delta_enabled_flag) {
    out->put_uint(out, "diff_cu_qp_delta_depth",
                  pps->diff_cu_qp_delta_depth = BsUe(bs));
  }
  out->put_int(out, "pps_cb_qp_offset", pps->pps_cb_qp_offset = BsSe(bs));
  out->put_int(out, "pps_cr_qp_offset", pps->pps_cr_qp_offset = BsSe(bs));
  out->put_uint(out, "pps_slice_chroma_qp_offsets_present_flag",
                pps->pps_slice_chroma_qp_offsets_present_flag = BsGet(bs, 1));
  out->put_uint(out, "weighted_pred_flag",
                pps->weighted_pred_flag = BsGet(bs, 1));
  out->put_uint(out, "weighted_bipred_flag",
                pps->weighted_bipred_flag = BsGet(bs, 1));
  out->put_uint(out, "transquant_bypass_enabled_flag",
                pps->transquant_bypass_enabled_flag = BsGet(bs, 1));
  out->put_uint(out, "tiles_enabled_flag",
                pps->tiles_enabled_flag = BsGet(bs, 1));
  out->put_uint(out, "entropy_coding_sync_enabled_flag",
                pps->entropy_coding_sync_enabled_flag = BsGet(bs, 1));
  if (pps->tiles_enabled_flag) {
    out->put_uint(out, "num_tile_columns_minus1",
                  pps->num_tile_columns_minus1 = BsUe(bs));
    out->put_uint(out, "num_tile_rows_minus1",
                  pps->num_tile_rows_minus1 = BsUe(bs));
    out->put_uint(out, "uniform_spacing_flag",
                  pps->uniform_spacing_flag = BsGet(bs, 1));
    if (!pps->uniform_spacing_flag) {
      for (i = 0; i < pps->num_tile_columns_minus1; i++) {
        uint32_t column_width_minus1 = BsUe(bs);
      }
      for (i = 0; i < pps->num_tile_rows_minus1; i++) {
        uint32_t row_height_minus1 = BsUe(bs);
      }
    }
    out->put_uint(out, "loop_filter_across_tiles_enabled_flag",
                  pps->loop_filter_across_tiles_enabled_flag = BsGet(bs, 1));
  }
  out->put_uint(out, "pps_loop_filter_across_slices_enabled_flag",
                pps->pps_loop_filter_across_slices_enabled_flag = BsGet(bs, 1));
  out->put_uint(out, "deblocking_filter_control_present_flag",
                pps->deblocking_filter_control_present_flag = BsGet(bs, 1));
  if (pps->deblocking_filter_control_present_flag) {
    out->put_uint(out, "deblocking_filter_override_enabled_flag",
                  pps->deblocking_filter_override_enabled_flag = BsGet(bs, 1));
    out->put_uint(out, "pps_deblocking_filter_disabled_flag",
                  pps->pps_deblocking_filter_disabled_flag = BsGet(bs, 1));
    if (!pps->pps_deblocking_filter_disabled_flag) {
      out->put_int(out, "pps_beta_offset_div2",
                   pps->pps_beta_offset_div2 = BsSe(bs));
      out->put_int(out, "pps_tc_offset_div2",
                   pps->pps_tc_offset_div2 = BsSe(bs));
    }
  }
  out->put_uint(out, "pps_scaling_list_data_present_flag",
                pps->pps_scaling_list_data_present_flag = BsGet(bs, 1));
  if (pps->pps_scaling_list_data_present_flag) {
    h265_scaling_list_data(dec, bs, out);
  }
  out->put_uint(out, "lists_modification_present_flag",
                pps->lists_modification_present_flag = BsGet(bs, 1));
  out->put_uint(out, "log2_parallel_merge_level_minus2",
                pps->log2_parallel_merge_level_minus2 = BsUe(bs));
  out->put_uint(out, "slice_segment_header_extension_present_flag",
                pps->slice_segment_header_extension_present_flag =
                    BsGet(bs, 1));
  out->put_uint(out, "pps_extension_present_flag",
                pps->pps_extension_present_flag = BsGet(bs, 1));
  if (pps->pps_extension_present_flag) {
    out->put_uint(out, "pps_range_extension_flag",
                  pps->pps_range_extension_flag = BsGet(bs, 1));
    out->put_uint(out, "pps_multilayer_extension_flag",
                  pps->pps_multilayer_extension_flag = BsGet(bs, 1));
    out->put_uint(out, "pps_extension_6bits",
                  pps->pps_extension_6bits = BsGet(bs, 6));
  }
  if (pps->pps_range_extension_flag)
    h265_pps_range_extension(dec, bs, out);
  /*
  if( pps_multilayer_extension_flag )
  pps_multilayer_extension();  // specified in Annex F
  if( pps_extension_6bits )
  while( more_rbsp_data( ) )
  out->put_uint(out, "pps_extension_data_flag", pps->pps_extension_data_flag
  =BsGet(bs, 1));
  */
  return 0;
}

int h265_slice_segment_header(struct h265_decode_t *dec, struct BitStream *bs,
                              struct OutputContextDict *out) {
  uint32_t i;
  struct H265SliceSegmentHeader *ssh = &dec->slice_segment.header;
  struct H265SeqParameterSet *sps = &dec->seq_param_set;
  struct H265PicParameterSet *pps = &dec->pic_param_set;

  uint8_t nal_unit_type = dec->nal_unit_header.nal_unit_type;

  out->put_uint(out, "first_slice_segment_in_pic_flag",
                ssh->first_slice_segment_in_pic_flag = BsGet(bs, 1));
  if (nal_unit_type >= H265_NAL_TYPE_BLA_W_LP &&
      nal_unit_type <= H265_NAL_TYPE_RSV_IRAP_VCL23) {
    out->put_uint(out, "no_output_of_prior_pics_flag",
                  ssh->no_output_of_prior_pics_flag = BsGet(bs, 1));
  }
  out->put_uint(out, "slice_pic_parameter_set_id",
                ssh->slice_pic_parameter_set_id = BsUe(bs));
  if (!ssh->first_slice_segment_in_pic_flag) {
    if (dec->pic_param_set.dependent_slice_segments_enabled_flag) {
      out->put_uint(out, "dependent_slice_segment_flag",
                    ssh->dependent_slice_segment_flag = BsGet(bs, 1));
    }
    out->put_uint(out, "slice_segment_address",
                  ssh->slice_segment_address =
                      BsGet(bs, CeilLog2(sps->PicSizeInCtbsY)));
  }
  if (!ssh->dependent_slice_segment_flag) {
    for (i = 0; i < pps->num_extra_slice_header_bits; i++) {
      uint8_t slice_reserved_flag = BsGet(bs, 1);
    }
    out->put_uint(out, "slice_type", ssh->slice_type = BsUe(bs));
    if (pps->output_flag_present_flag) {
      out->put_uint(out, "pic_output_flag",
                    ssh->pic_output_flag = BsGet(bs, 1));
    }
    if (sps->separate_colour_plane_flag == 1) {
      out->put_uint(out, "colour_plane_id",
                    ssh->colour_plane_id = BsGet(bs, 2));
    }
    if (nal_unit_type != H265_NAL_TYPE_IDR_W_RADL &&
        nal_unit_type != H265_NAL_TYPE_IDR_N_LP) {
      out->put_uint(out, "slice_pic_order_cnt_lsb",
                    ssh->slice_pic_order_cnt_lsb =
                        BsGet(bs, sps->log2_max_pic_order_cnt_lsb_minus4 + 4));
      out->put_uint(out, "short_term_ref_pic_set_sps_flag",
                    ssh->short_term_ref_pic_set_sps_flag = BsGet(bs, 1));
      if (!ssh->short_term_ref_pic_set_sps_flag) {
        fprintf(stderr, "Unimplement st_ref_pic_set\n");
        return -2;
        // st_ref_pic_set(sps->num_short_term_ref_pic_sets);
      } else if (sps->num_short_term_ref_pic_sets > 1) {
        out->put_uint(out, "short_term_ref_pic_set_idx",
                      ssh->short_term_ref_pic_set_idx = BsGet(
                          bs, CeilLog2(sps->num_short_term_ref_pic_sets)));
      }
      if (sps->long_term_ref_pics_present_flag) {
        if (sps->num_long_term_ref_pics_sps > 0) {
          out->put_uint(out, "num_long_term_sps",
                        ssh->num_long_term_sps = BsUe(bs));
        }
        out->put_uint(out, "num_long_term_pics",
                      ssh->num_long_term_pics = BsUe(bs));
        for (i = 0; i < ssh->num_long_term_sps + ssh->num_long_term_pics; i++) {
          if (i < ssh->num_long_term_sps) {
            if (sps->num_long_term_ref_pics_sps > 1) {
              uint8_t lt_idx_sps =
                  BsGet(bs, CeilLog2(sps->num_long_term_ref_pics_sps));
            }
          } else {
            uint8_t poc_lsb_lt =
                BsGet(bs, sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
            uint8_t used_by_curr_pic_lt_flag = BsGet(bs, 1);
          }
          uint8_t delta_poc_msb_present_flag = BsGet(bs, 1);
          if (delta_poc_msb_present_flag) {
            uint32_t delta_poc_msb_cycle_lt = BsUe(bs);
          }
        }
      }
      if (sps->sps_temporal_mvp_enabled_flag) {
        out->put_uint(out, "slice_temporal_mvp_enabled_flag",
                      ssh->slice_temporal_mvp_enabled_flag = BsGet(bs, 1));
      }

      // ===================
    }
    if (sps->sample_adaptive_offset_enabled_flag) {
      out->put_uint(out, "slice_sao_luma_flag",
                    ssh->slice_sao_luma_flag = BsGet(bs, 1));
      if (sps->ChromaArrayType != 0) {
        out->put_uint(out, "slice_sao_chroma_flag",
                      ssh->slice_sao_chroma_flag = BsGet(bs, 1));
      }
    }
    if (ssh->slice_type == H265_SLICE_TYPE_P ||
        ssh->slice_type == H265_SLICE_TYPE_B) {
      out->put_uint(out, "num_ref_idx_active_override_flag",
                    ssh->num_ref_idx_active_override_flag = BsGet(bs, 1));
      if (ssh->num_ref_idx_active_override_flag) {
        out->put_uint(out, "num_ref_idx_l0_active_minus1",
                      ssh->num_ref_idx_l0_active_minus1 = BsUe(bs));
        if (ssh->slice_type == H265_SLICE_TYPE_B) {
          out->put_uint(out, "num_ref_idx_l1_active_minus1",
                        ssh->num_ref_idx_l1_active_minus1 = BsUe(bs));
        }
      }
      fprintf(stderr, "Unimplemented ref_pic_lists_modification()\n");
      return -2;
      /*
      if (pps->lists_modification_present_flag && NumPicTotalCurr > 1) {
        ref_pic_lists_modification();
      }
      */
      if (ssh->slice_type == H265_SLICE_TYPE_B) {
        out->put_uint(out, "mvd_l1_zero_flag",
                      ssh->mvd_l1_zero_flag = BsGet(bs, 1));
      }
      if (pps->cabac_init_present_flag) {
        out->put_uint(out, "cabac_init_flag",
                      ssh->cabac_init_flag = BsGet(bs, 1));
      }
      if (ssh->slice_temporal_mvp_enabled_flag) {
        if (ssh->slice_type == H265_SLICE_TYPE_B) {
          out->put_uint(out, "collocated_from_l0_flag",
                        ssh->collocated_from_l0_flag = BsGet(bs, 1));
        }
        if ((ssh->collocated_from_l0_flag &&
             ssh->num_ref_idx_l0_active_minus1 > 0) ||
            (!ssh->collocated_from_l0_flag &&
             ssh->num_ref_idx_l1_active_minus1 > 0)) {
          out->put_uint(out, "collocated_ref_idx",
                        ssh->collocated_ref_idx = BsUe(bs));
        }
      }
      if ((pps->weighted_pred_flag && ssh->slice_type == H265_SLICE_TYPE_P) ||
          (pps->weighted_bipred_flag && ssh->slice_type == H265_SLICE_TYPE_B)) {
        fprintf(stderr, "Unimplemented pred_weight_table()\n");
        return -2;
        // pred_weight_table();
      }
      out->put_uint(out, "five_minus_max_num_merge_cand",
                    ssh->five_minus_max_num_merge_cand = BsUe(bs));
    }
    out->put_int(out, "slice_qp_delta", ssh->slice_qp_delta = BsSe(bs));
    if (pps->pps_slice_chroma_qp_offsets_present_flag) {
      out->put_int(out, "slice_cb_qp_offset",
                   ssh->slice_cb_qp_offset = BsSe(bs));
      out->put_int(out, "slice_cr_qp_offset",
                   ssh->slice_cr_qp_offset = BsSe(bs));
    }
    if (pps->chroma_qp_offset_list_enabled_flag) {
      out->put_uint(out, "cu_chroma_qp_offset_enabled_flag",
                    ssh->cu_chroma_qp_offset_enabled_flag = BsGet(bs, 1));
    }
    if (pps->deblocking_filter_override_enabled_flag) {
      out->put_uint(out, "deblocking_filter_override_flag",
                    ssh->deblocking_filter_override_flag = BsGet(bs, 1));
    }
    if (ssh->deblocking_filter_override_flag) {
      out->put_uint(out, "slice_deblocking_filter_disabled_flag",
                    ssh->slice_deblocking_filter_disabled_flag = BsGet(bs, 1));
      if (!ssh->slice_deblocking_filter_disabled_flag) {
        out->put_int(out, "slice_beta_offset_div2",
                     ssh->slice_beta_offset_div2 = BsSe(bs));
        out->put_int(out, "slice_tc_offset_div2",
                     ssh->slice_tc_offset_div2 = BsSe(bs));
      }
    }

    // =======================

    if (pps->pps_loop_filter_across_slices_enabled_flag &&
        (ssh->slice_sao_luma_flag || ssh->slice_sao_chroma_flag ||
         !ssh->slice_deblocking_filter_disabled_flag)) {
      out->put_uint(out, "slice_loop_filter_across_slices_enabled_flag",
                    ssh->slice_loop_filter_across_slices_enabled_flag =
                        BsGet(bs, 1));
    }
  }
  if (pps->tiles_enabled_flag || pps->entropy_coding_sync_enabled_flag) {
    out->put_uint(out, "num_entry_point_offsets",
                  ssh->num_entry_point_offsets = BsUe(bs));
    if (ssh->num_entry_point_offsets > 0) {
      out->put_uint(out, "offset_len_minus1",
                    ssh->offset_len_minus1 = BsUe(bs));
      for (i = 0; i < ssh->num_entry_point_offsets; i++) {
        uint8_t entry_point_offset_minus1 =
            BsGet(bs, ssh->offset_len_minus1 + 1);
      }
    }
  }
  if (pps->slice_segment_header_extension_present_flag) {
    out->put_uint(out, "slice_segment_header_extension_length",
                  ssh->slice_segment_header_extension_length = BsUe(bs));
    for (i = 0; i < ssh->slice_segment_header_extension_length; i++) {
      uint8_t slice_segment_header_extension_data_byte = BsGet(bs, 8);
    }
  }
  // byte_alignment();
  return 0;
}

int h265_parse_nal(struct h265_decode_t *dec, struct BitStream *bs,
                   struct OutputContextDict *out) {
  int err = 0;
  struct OutputContextDict subdict[1];
  if (BsGet(bs, 24) == 0)
    BsGet(bs, 8);
  out->put_dict(out, "nal_unit_header", subdict);
  err = h265_nal_unit_header(dec, bs, subdict);
  subdict->end(subdict);
  if (err == 0) {
    switch (dec->nal_unit_header.nal_unit_type) {
    case H265_NAL_TYPE_VPS_NUT:
      h265_video_parameter_set(dec, bs, out);
      break;
    case H265_NAL_TYPE_SPS_NUT:
      h265_seq_parameter_set(dec, bs, out);
      break;
    case H265_NAL_TYPE_PPS_NUT:
      h265_pic_parameter_set(dec, bs, out);
      break;
    case H265_NAL_TYPE_TRAIL_N:
    case H265_NAL_TYPE_TRAIL_R:
    case H265_NAL_TYPE_TSA_N:
    case H265_NAL_TYPE_TSA_R:
    case H265_NAL_TYPE_STSA_N:
    case H265_NAL_TYPE_STSA_R:
    case H265_NAL_TYPE_RADL_N:
    case H265_NAL_TYPE_RADL_R:
    case H265_NAL_TYPE_RASL_N:
    case H265_NAL_TYPE_RASL_R:
    case H265_NAL_TYPE_BLA_W_LP:
    case H265_NAL_TYPE_BLA_W_RADL:
    case H265_NAL_TYPE_BLA_N_LP:
    case H265_NAL_TYPE_IDR_W_RADL:
    case H265_NAL_TYPE_IDR_N_LP:
    case H265_NAL_TYPE_CRA_NUT:
      h265_slice_segment_header(dec, bs, out);
      break;
    }
  }
  return err;
}

#define MAX_BUFFER (1024 * 512)
uint8_t buffer[MAX_BUFFER];

int main(int argc, char *argv[]) {
  uint32_t buffer_on, buffer_size;
  uint64_t bytes = 0;

  const char *fn1 = "E:\\Data\\MediaSample\\sample_k.hvc";
  FILE *fi = fopen(fn1, "rb");
  FILE *fp = stdout;
  struct h265_decode_t dec, prevdec;
  memset(&dec, 0, sizeof(dec));
  memset(&prevdec, 0, sizeof(prevdec));

  buffer_on = buffer_size = 0;

  struct OutputContextList out_list[1];
  struct OutputConfig out_cfg;
  out_cfg.print_hex = 1;
  out_cfg.explain_enum = 1;
  OutputContextInitList(out_list, fp, 1, &out_cfg);
  while (!feof(fi)) {
    bytes += buffer_on;
    if (buffer_on != 0) {
      buffer_on = buffer_size - buffer_on;
      memmove(buffer, &buffer[buffer_size - buffer_on], buffer_on);
    }
    buffer_size = fread(buffer + buffer_on, 1, sizeof(buffer) - buffer_on, fi);
    buffer_size += buffer_on;
    buffer_on = 0;

    int done = 0;

    struct BitStream bs;
    do {
      uint32_t ret;
      struct OutputContextDict out_dict[1];
      ret = h265_find_next_start_code(buffer + buffer_on,
                                      buffer_size - buffer_on);
      if (ret == 0) {
        done = 1;
        if (buffer_on == 0) {
          fprintf(stderr, "couldn't find start code in buffer\n");
          exit(-1);
        }
        break;
      }
      if (ret > 3) {
        uint32_t nal_len;
        nal_len = remove_03(buffer + buffer_on, ret);
        out_list->put_dict(out_list, out_dict);
        out_dict->put_uint(out_dict, "nal_length", nal_len);
        out_dict->put_uint(out_dict, "start_code_bytes",
                           buffer[buffer_on + 2] == 1 ? 3 : 4);
        out_dict->put_hex(out_dict, "offset", bytes + buffer_on);
        BsInit(&bs, buffer + buffer_on, nal_len);
        h265_parse_nal(&dec, &bs, out_dict);
        out_dict->end(out_dict);
      }
      buffer_on += ret;
    } while (!done);
  }
  out_list->end(out_list);

  fclose(fi);
  return 0;
}
