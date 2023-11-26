
/*
 * Copyright (C) 2020-2022  GreenWaves Technologies, ETH Zurich, University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* 
 * Authors: Arpan Suravi Prasad, ETH Zurich (prasadar@iis.ee.ethz.ch)
 */
#ifndef CONTROL_H
#define CONTROL_H
#include "datatype.hpp"
#include "params.hpp"

template <typename HwpeType>
class Control 
{
  private:
    std::array<bool,NeurekaTotalPECountXY> pe_enable_;
    std::array<std::array<bool, NeurekaInFeatScalarBufferCount>, NeurekaTotalPECountXY> col_enable_;
    std::array<std::array<std::array<bool, NeurekaComputeRowCount>,NeurekaInFeatScalarBufferCount>, NeurekaTotalPECountXY> binconv_enable_;

    HwpeType* hwpe_instance_;
    HwParams hw_param_;
    RegConfig ctrl_config_;

  public:
    TilingParams<int> current_tile_size, total_size;
    TilingStatus tiles, prev_tiles;
    LoadStoreStatus load_store_status;

    Control() {}
    Control(HwpeType* hwpe_instance){
      hwpe_instance_ = hwpe_instance;
      tiles = {};
    }

  void ResetIndexes(){
    tiles = {};
    prev_tiles={};
    load_store_status={};
    current_tile_size={};
    total_size={};
  }
  
  void ComputeDimensions() {
    if(ctrl_config_.config0.filter_mode != Pointwise) {
      load_store_status.infeat.count.win = tiles.index.wout < tiles.count.wout-1 ? NeurekaInFeatBufferSizeX : (ctrl_config_.win_tile_rem ? ctrl_config_.win_tile_rem : NeurekaInFeatBufferSizeX);
      load_store_status.infeat.count.hin = tiles.index.hout < tiles.count.hout-1 ? NeurekaInFeatBufferSizeY : (ctrl_config_.hin_tile_rem ? ctrl_config_.hin_tile_rem : NeurekaInFeatBufferSizeY);  
    } else {
      load_store_status.infeat.count.win = tiles.index.wout < tiles.count.wout-1 ? NeurekaPECountX : (ctrl_config_.win_tile_rem ? ctrl_config_.win_tile_rem : NeurekaPECountX);
      load_store_status.infeat.count.hin = tiles.index.hout < tiles.count.hout-1 ? NeurekaPECountY : (ctrl_config_.hin_tile_rem ? ctrl_config_.hin_tile_rem : NeurekaPECountY);
    }
    load_store_status.outfeat.count.wout = tiles.index.wout < tiles.count.wout-1 ? NeurekaPECountX : (ctrl_config_.wout_tile_rem ? ctrl_config_.wout_tile_rem : NeurekaPECountX);
    load_store_status.outfeat.count.hout = tiles.index.hout < tiles.count.hout-1 ? NeurekaPECountY : (ctrl_config_.hout_tile_rem ? ctrl_config_.hout_tile_rem : NeurekaPECountY);
    load_store_status.streamin.count.wout = load_store_status.outfeat.count.wout;
    load_store_status.streamin.count.hout = load_store_status.outfeat.count.hout;
    current_tile_size.kin  = tiles.index.kin  < tiles.count.kin-1  ? NeurekaInFeatScalarBufferCount : (ctrl_config_.kin_tile_rem ? ctrl_config_.kin_tile_rem : NeurekaInFeatScalarBufferCount) ;
    current_tile_size.kout = tiles.index.kout  < tiles.count.kout-1  ? NeurekaAccumulatorPerPECount : (ctrl_config_.kout_tile_rem? ctrl_config_.kout_tile_rem : NeurekaAccumulatorPerPECount);
    
    int current_tile_sizekout_rem = current_tile_size.kout%8 ? current_tile_size.kout%8  : 8;
    int current_tile_sizekout_quo = current_tile_size.kout%8 ? 1 + (current_tile_size.kout/8) : (current_tile_size.kout/8);
    load_store_status.outfeat.count.word = ctrl_config_.config0.quantization_bit_count == 8 ? 1 : current_tile_sizekout_quo;
    load_store_status.streamin.count.word = ctrl_config_.config0.streamin_bit_count == 8 ? 1 : current_tile_sizekout_quo;

    total_size.kin  = (tiles.count.kin-1)*NeurekaInFeatScalarBufferCount + ctrl_config_.kin_tile_rem;
    total_size.kout = (tiles.count.kout-1)*NeurekaAccumulatorPerPECount + ctrl_config_.kout_tile_rem;
    total_size.hout = (tiles.count.hout-1)*NeurekaPECountY  + ctrl_config_.hout_tile_rem;
    total_size.wout = (tiles.count.wout-1)*NeurekaPECountX  + ctrl_config_.wout_tile_rem;
    total_size.hin  = (tiles.count.hout-1)*NeurekaPECountY  + ctrl_config_.hin_tile_rem;
    total_size.win  = (tiles.count.wout-1)*NeurekaPECountX  + ctrl_config_.win_tile_rem;
  }



std::array<bool, NeurekaTotalPECountXY> ComputePEEnable(){
  ComputeDimensions();
  std::fill(pe_enable_.begin(), pe_enable_.end(), 0);

  for(int y=0; y<NeurekaPECountY; y++) {
    for(int x=0; x<NeurekaPECountX; x++) {
      if(y<load_store_status.outfeat.count.hout && x<load_store_status.outfeat.count.wout)
        pe_enable_[y*NeurekaPECountX+x] = true;
    }
  }
  return pe_enable_;
}

std::array<std::array<bool, NeurekaInFeatScalarBufferCount>, NeurekaTotalPECountXY>  ComputeColumnEnable(bool woffs) {
  auto temp = ComputePEEnable();
  col_enable_ = {};
  for(int pe=0; pe<NeurekaTotalPECountXY; pe++) {
    if(ctrl_config_.config0.filter_mode == Pointwise){
      for(int wgt_bit=0; wgt_bit<ctrl_config_.config0.weight_bit_count; wgt_bit++) {
        for(int i=0; i<NeurekaChannelwise1x1; i++) {
          int index = wgt_bit*NeurekaChannelwise1x1 + i;
          if(woffs && wgt_bit>0)
            col_enable_[pe][index] = false; 
          else
            col_enable_[pe][index] = true & pe_enable_[pe];
        }
      }
    } else {
    for(int col=0; col<NeurekaInFeatScalarBufferCount; col++)
      if(ctrl_config_.config0.filter_mode != Depthwise){
        if(col<current_tile_size.kin)
          col_enable_[pe][col] = (true & pe_enable_[pe]);
        else
          col_enable_[pe][col] = false;
      } else {
        if(col<current_tile_size.kout)
          col_enable_[pe][col] = (true & pe_enable_[pe]);
      }
    }
  }
  return col_enable_;
}

std::array<std::array<std::array<bool, NeurekaComputeRowCount>,NeurekaInFeatScalarBufferCount>, NeurekaTotalPECountXY> ComputeBinconvEnable(bool woffs){
  auto temp = ComputeColumnEnable(woffs);
  std::array<std::array<std::array<bool, NeurekaComputeRowCount>,NeurekaInFeatScalarBufferCount>, NeurekaTotalPECountXY> binconv_enable_ = {0};
  for(int pe=0; pe<NeurekaTotalPECountXY; pe++){
    if(ctrl_config_.config0.filter_mode == Pointwise) {
      for(int wgt_bit=0; wgt_bit<ctrl_config_.config0.weight_bit_count; wgt_bit++) { // repeated 8 times max 
        for(int i=0; i<NeurekaChannelwise1x1; i++) { // 4 columns
          int col_index = wgt_bit*NeurekaChannelwise1x1 + i;  
          for(int row=0; row<NeurekaComputeRowCount-1; row++) {
            int row_index =  i*NeurekaRepeated1x1 + row; 
            if(row_index<current_tile_size.kin)
              binconv_enable_[pe][col_index][row] = true & col_enable_[pe][col_index] & (~ctrl_config_.filter_mask_bit[row]);
            else 
              binconv_enable_[pe][col_index][row] = false; 
          }
        }
      }
    } else {
    for(int col=0; col<NeurekaInFeatScalarBufferCount; col++)
      for(int row=0; row<NeurekaComputeRowCount; row++)
      {
          binconv_enable_[pe][col][row] = true & col_enable_[pe][col] & (~ctrl_config_.filter_mask_bit[row]);
      }
    }
  }

  return binconv_enable_;
}


void UpdateTileIndex() {
    prev_tiles = tiles;
    if (tiles.index.kout >= tiles.count.kout) {
        return; 
    }

    tiles.index.kin++;
    if (tiles.index.kin >= tiles.count.kin) {
        tiles.index.kin = 0;
        tiles.index.wout++;
        if (tiles.index.wout >= tiles.count.wout) {
            tiles.index.wout = 0;
            tiles.index.hout++;
            if (tiles.index.hout >= tiles.count.hout) {
                tiles.index.hout = 0;
                tiles.index.kout++;
            }
        }
    }
  tiles.index.win = tiles.index.wout;
  tiles.index.hin = tiles.index.hout;

}

void CheckTileStatus(){
  tiles.done.kin  = tiles.index.kin == tiles.count.kin-1 ? true : false;
  tiles.done.kout = tiles.index.kout == tiles.count.kout-1 ? true : false;
  tiles.done.hout = tiles.index.hout == tiles.count.hout-1 ? true : false;
  tiles.done.wout = tiles.index.wout == tiles.count.wout-1 ? true : false;
  tiles.finish    = tiles.done.wout && tiles.done.hout && tiles.done.kout && tiles.done.kin;
}


void SetConfig(RegConfig config) {
  ctrl_config_ = config;
  tiles.count.wout = ctrl_config_.wout_tile_count;
  tiles.count.hout = ctrl_config_.hout_tile_count;
  tiles.count.kin  = (ctrl_config_.config0.filter_mode == Depthwise ?1 : ctrl_config_.kin_tile_count);
  tiles.count.kout = ctrl_config_.kout_tile_count;
  tiles.done.hout  = false;
  tiles.done.wout  = false;
  tiles.done.kout  = false;
  tiles.done.kin   = false;
  tiles.done.hin   = false;
  tiles.done.win   = false;
  tiles.finish     = false;
  tiles.count.hin  = tiles.count.hout;
  tiles.count.win  = tiles.count.wout;

  // hwpe_instance_->trace.msg(" control >> SetConfig INDEXES (kin : %d, kout : %d, hout : %d, wout : %d)\n",tiles.index.kin, tiles.index.kout, tiles.index.hout, tiles.index.wout);
  // hwpe_instance_->trace.msg(" control >> SetConfig done : %d , COUNTS (kin : %d, kout : %d, hout : %d, wout : %d)\n",tiles.finish, tiles.count.kin, tiles.count.kout, tiles.count.hout, tiles.count.wout); 
  // PrintReg();
}

void ResetInFeatLoadIteration(){
  load_store_status.infeat.done = false; 
  load_store_status.infeat.index.win = 0;
  load_store_status.infeat.index.hin = 0;
}

void InFeatLoadIteration(){
  int hin_count = load_store_status.infeat.count.hin - 1;
  int win_count = load_store_status.infeat.count.win - 1;
  int win_index = load_store_status.infeat.index.win;
  int hin_index = load_store_status.infeat.index.hin;
  load_store_status.infeat.done=false;
  load_store_status.infeat.index.hinXwin = hin_index*NeurekaInFeatBufferSizeX+win_index;
  load_store_status.infeat.index.win = (win_index == win_count) ? 0 : 1 + load_store_status.infeat.index.win ;
  load_store_status.infeat.index.hin = (win_index == win_count) && (hin_index == hin_count) ? 0 : (win_index == win_count) && (hin_index < hin_count) ? 1 + load_store_status.infeat.index.hin : load_store_status.infeat.index.hin;
  load_store_status.infeat.done      = (win_index == win_count) && (hin_index == hin_count) ? true : false;

}

int GetWeightLoadWeightIndex(){ 
    return load_store_status.weight.index.wgt;// unused for depthwise mode
}

int GetWeightLoadKoutIndex(){ 
    return load_store_status.weight.index.kout;// unused for depthwise mode
}

void ResetWeightLoadIteration(){
  load_store_status.weight.index.kout=0;
  load_store_status.weight.index.wgt=0;
}

void WeightLoadIteration(){
  int qw_count = ctrl_config_.config0.weight_bit_count-1;
  int qw_index = load_store_status.weight.index.wgt;
  int kout_index = load_store_status.weight.index.kout;
  int kout_count = current_tile_size.kout-1;
  Mode mode = ctrl_config_.config0.filter_mode;

  load_store_status.weight.done = false;
  switch(mode){
    case Depthwise :
      load_store_status.weight.index.kout = 0;
      load_store_status.weight.index.wgt  = qw_index < qw_count ? load_store_status.weight.index.wgt+1 : load_store_status.weight.index.wgt;
      load_store_status.weight.done       = qw_index < qw_count ? false : true;
      break;
    case Pointwise : 
      load_store_status.weight.index.wgt  = 0;
      load_store_status.weight.index.kout = kout_index < kout_count ? load_store_status.weight.index.kout + 1 : load_store_status.weight.index.kout;
      load_store_status.weight.done       = kout_index < kout_count ? false : true;
      break;
    case Dense3x3 :
      load_store_status.weight.index.wgt  = qw_index == qw_count ? 0 : 1 + load_store_status.weight.index.wgt;
      load_store_status.weight.index.kout = (kout_index < kout_count) && (qw_index == qw_count) ? 1 + load_store_status.weight.index.kout : (kout_index == kout_count) && (qw_index == qw_count) ? 0 : load_store_status.weight.index.kout;
      load_store_status.weight.done       = (kout_index == kout_count) && (qw_index == qw_count) ? true : false;
      break;
    default:
      hwpe_instance_->trace.msg("Unsupported Filter mode");
  }
}

void ResetOutFeatStoreIteration(){
  load_store_status.outfeat.done = false; 
  load_store_status.outfeat.index.word = 0;// Use for 8 and 32 bit quantizations
  load_store_status.outfeat.index.wout = 0;
  load_store_status.outfeat.index.hout = 0;
}

// generates a signal on what to store and what not.
int OutFeatStoreWidth(){
  int current_tile_sizekout_rem = current_tile_size.kout%8 ? current_tile_size.kout%8  : 8;
  int current_tile_sizekout_quo = current_tile_size.kout%8 ? 1 + (current_tile_size.kout/8) : (current_tile_size.kout/8);
  if(ctrl_config_.config0.quantization_bit_count==32)
    if(load_store_status.outfeat.index.word < load_store_status.outfeat.count.word-1) return L1BandwidthInBytes;
    else return 4*current_tile_sizekout_rem;
  else return current_tile_size.kout;
}

void OutFeatStoreIteration(){
  int word_index = load_store_status.outfeat.index.word;
  int wout_index = load_store_status.outfeat.index.wout;
  int hout_index = load_store_status.outfeat.index.hout;
  int word_count = load_store_status.outfeat.count.word - 1;
  int hout_count = load_store_status.outfeat.count.hout - 1;
  int wout_count = load_store_status.outfeat.count.wout - 1;

  int offs = ctrl_config_.config0.strided2x2 ? 2 : 1;
  load_store_status.outfeat.index.word = (word_index  < word_count) ? 1 + load_store_status.outfeat.index.word : 0;
  load_store_status.outfeat.index.wout = (word_index == word_count) && (wout_index+offs < wout_count+1) ? offs + load_store_status.outfeat.index.wout : (word_index == word_count) && (wout_index+offs >= wout_count+1) ? 0 : load_store_status.outfeat.index.wout;
  load_store_status.outfeat.index.hout = (word_index == word_count) && (wout_index+offs >= wout_count+1) && (hout_index+offs < hout_count+1) ? offs + load_store_status.outfeat.index.hout : (word_index == word_count) && (wout_index == wout_count) && (hout_index+offs >= hout_count+1) ? 0 : load_store_status.outfeat.index.hout;
  load_store_status.outfeat.done       = (word_index == word_count) && ((wout_index+offs) >= (wout_count+1)) && ((hout_index+offs) >= (hout_count+1)) ? true : false;
}

int GetOutFeatStoreLinearBufferIndex(){ 
  int index = load_store_status.outfeat.index.hout*NeurekaPECountX + load_store_status.outfeat.index.wout;
  return index;// unused for depthwise mode
}

int GetOutFeatStoreWordIndex(){ 
  int index = load_store_status.outfeat.index.word*8;
  return index;// unused for depthwise mode
}

int GetOutFeatStoreWordSize(){ 
  int index = load_store_status.outfeat.count.word;
  return index;// unused for depthwise mode
}


int GetStreaminLinearBufferIndex(){ 
  int index = load_store_status.streamin.index.hout*NeurekaPECountX + load_store_status.streamin.index.wout;
  return index;// unused for depthwise mode
}

int GetStreaminWordIndex(){ 
  int index = load_store_status.streamin.index.word*8;
  return index;// unused for depthwise mode
}

int GetStreaminWordSize(){ 
  int index = load_store_status.streamin.count.word;
  return index;// unused for depthwise mode
}

int StreaminLoadWidth(){
  int current_tile_sizekout_rem = current_tile_size.kout%8 ? current_tile_size.kout%8  : 8;
  int current_tile_sizekout_quo = current_tile_size.kout%8 ? 1 + (current_tile_size.kout/8) : (current_tile_size.kout/8);
  if(ctrl_config_.config0.streamin_bit_count==32)
    if(load_store_status.streamin.index.word < load_store_status.streamin.count.word-1) return L1BandwidthInBytes;
    else return 4*current_tile_sizekout_rem;
  else return current_tile_size.kout;
}

StreamerConfig GetInFeatLoadStreamerConfig(){
  AddrType addr_kin, addr_win, addr_hin;
  if(ctrl_config_.config0.filter_mode!=Depthwise){
    addr_kin = tiles.index.kin*NeurekaInFeatScalarBufferCount;
    if(ctrl_config_.padding.left&&ctrl_config_.padding.right&&ctrl_config_.padding.top&&ctrl_config_.padding.bottom){
      addr_win = tiles.index.win*NeurekaPECountX*total_size.kin;
      addr_hin = tiles.index.hin*NeurekaPECountY*(total_size.win-2*ctrl_config_.padding.left)*total_size.kin;
    }else {
      addr_win = tiles.index.win*NeurekaPECountX*total_size.kin;
      addr_hin = tiles.index.hin*NeurekaPECountY*total_size.win*total_size.kin;
    }
  } else {
    addr_kin = ctrl_config_.config0.broadcast ? NeurekaInFeatScalarBufferCount : tiles.index.kout*NeurekaInFeatScalarBufferCount;
     if(ctrl_config_.padding.left&&ctrl_config_.padding.right&&ctrl_config_.padding.top&&ctrl_config_.padding.bottom){
        addr_win = ctrl_config_.config0.broadcast ? tiles.index.win*NeurekaPECountX : tiles.index.win*NeurekaPECountX*total_size.kout;
        addr_hin = ctrl_config_.config0.broadcast ? tiles.index.hin*NeurekaPECountY*(total_size.win-2*ctrl_config_.padding.left) : tiles.index.hin*NeurekaPECountY*(total_size.win-2)*total_size.kout;
      }else {
        addr_win = ctrl_config_.config0.broadcast ? tiles.index.win*NeurekaPECountX : tiles.index.win*NeurekaPECountX*total_size.kout;
        addr_hin = ctrl_config_.config0.broadcast ? tiles.index.hin*NeurekaPECountY*total_size.win : tiles.index.hin*NeurekaPECountY*total_size.win*total_size.kout;
      }
  }


  StreamerConfig infeat;
  infeat.base_addr = L1_MASK & (ctrl_config_.infeat_ptr + addr_hin + addr_win + addr_kin);
  infeat.stride.d0 = ctrl_config_.infeat_stride.d0;
  infeat.stride.d1 = ctrl_config_.infeat_stride.d1;
  infeat.stride.d2 = ctrl_config_.infeat_stride.d2;
  infeat.length.d0 = load_store_status.infeat.count.win;
  infeat.length.d1 = load_store_status.infeat.count.hin;
  infeat.length.d2 = 1; // unused
  return infeat;
}

StreamerConfig GetWeightLoadStreamerConfig(){
  Mode mode = ctrl_config_.config0.filter_mode;
  int qw = ctrl_config_.config0.weight_bit_count;
  AddrType addr_fs   = (NeurekaInFeatScalarBufferCount/8)*(mode == Pointwise ? 1 : 3*3);
  AddrType addr_qw   = (mode == Depthwise ? tiles.index.kout :tiles.index.kin)*addr_fs*qw;
  AddrType addr_kout = mode == Depthwise ? 0 : tiles.index.kout*ctrl_config_.kin_tile_count*NeurekaAccumulatorPerPECount*qw*addr_fs;
  StreamerConfig weight;
  weight.base_addr =  ctrl_config_.weight_ptr + addr_kout + addr_qw;
  weight.stride.d0 = ctrl_config_.weight_stride.d0;
  weight.stride.d1 = ctrl_config_.weight_stride.d1;
  weight.stride.d2 = ctrl_config_.weight_stride.d2;
  weight.length.d0 = mode == Pointwise ? 1 : ctrl_config_.config0.weight_bit_count ;
  weight.length.d1 = mode == Depthwise ? 1 : current_tile_size.kout;
  weight.length.d2 = 1; // unused
  return weight;
}

StreamerConfig GetOutFeatStoreStreamerConfig(){
  int offs = ctrl_config_.config0.strided2x2 ? 2 : 1;
  int h_size = ctrl_config_.config0.strided2x2==0 ? ctrl_config_.outfeat_stride.d2 : total_size.kout*(total_size.wout%2 ? (1+total_size.wout)/offs : total_size.wout/offs);
  AddrType addr_kout = (ctrl_config_.config0.quantization_bit_count == 32 ? 4 : 1) * prev_tiles.index.kout*NeurekaAccumulatorPerPECount;
  AddrType addr_wout = (ctrl_config_.config0.quantization_bit_count == 32 ? 4 : 1) * prev_tiles.index.wout*(NeurekaPECountX/offs)*ctrl_config_.outfeat_stride.d1;
  AddrType addr_hout = (ctrl_config_.config0.quantization_bit_count == 32 ? 4 : 1) * prev_tiles.index.hout*(NeurekaPECountY/offs)*h_size;

  StreamerConfig outfeat;
  outfeat.base_addr = L1_MASK & (ctrl_config_.outfeat_ptr + addr_kout + addr_wout + addr_hout);
  outfeat.stride.d0 = ctrl_config_.outfeat_stride.d0;
  outfeat.stride.d1 = ctrl_config_.outfeat_stride.d1;
  outfeat.stride.d2 = ctrl_config_.outfeat_stride.d2;
  outfeat.length.d0 = load_store_status.outfeat.count.word;
  outfeat.length.d1 = ctrl_config_.config0.strided2x2==0 ?  load_store_status.outfeat.count.wout : load_store_status.outfeat.count.wout % offs ? (1+load_store_status.outfeat.count.wout)/2 : load_store_status.outfeat.count.wout/2;
  outfeat.length.d2 = ctrl_config_.config0.strided2x2==0 ?  load_store_status.outfeat.count.hout : load_store_status.outfeat.count.hout % offs ? (1+load_store_status.outfeat.count.hout)/2 : load_store_status.outfeat.count.hout/2; // unused
  return outfeat;
}

void ResetStreaminIteration(){
  load_store_status.streamin.index.word = 0;
  load_store_status.streamin.index.hout = 0;
  load_store_status.streamin.index.wout = 0;
}
void StreaminIteration(){
  int word_index = load_store_status.streamin.index.word;
  int wout_index = load_store_status.streamin.index.wout;
  int hout_index = load_store_status.streamin.index.hout;
  int word_count = load_store_status.streamin.count.word - 1;
  int hout_count = load_store_status.streamin.count.hout - 1;
  int wout_count = load_store_status.streamin.count.wout - 1;

  load_store_status.streamin.index.word = (word_index  < word_count) ? 1 + load_store_status.streamin.index.word : 0;
  load_store_status.streamin.index.wout = (word_index == word_count) && (wout_index < wout_count) ? 1 + load_store_status.streamin.index.wout : (word_index == word_count) && (wout_index == wout_count) ? 0 : load_store_status.streamin.index.wout;
  load_store_status.streamin.index.hout = (word_index == word_count) && (wout_index == wout_count) && (hout_index < hout_count) ? 1 + load_store_status.streamin.index.hout : (word_index == word_count) && (wout_index == wout_count) && (hout_index == hout_count) ? 0 : load_store_status.streamin.index.hout;
  load_store_status.streamin.done       = (word_index == word_count) && (wout_index == wout_count) && (hout_index == hout_count) ? true : false;
}
StreamerConfig GetStreaminStreamerConfig(){
  AddrType addr_kout = (ctrl_config_.config0.streamin_bit_count == 32 ? 4 : 1) * tiles.index.kout*NeurekaAccumulatorPerPECount;
  AddrType addr_wout = (ctrl_config_.config0.streamin_bit_count == 32 ? 4 : 1) * tiles.index.wout*NeurekaPECountX*total_size.kout;
  AddrType addr_hout = (ctrl_config_.config0.streamin_bit_count == 32 ? 4 : 1) * tiles.index.hout*NeurekaPECountX*total_size.wout*total_size.kout;

  int quant_bits = ctrl_config_.config0.quantization_bit_count;
  int streamin_bits = ctrl_config_.config0.streamin_bit_count;
  StreamerConfig streamin;
  streamin.base_addr = L1_MASK & (ctrl_config_.streamin_ptr + addr_kout + addr_wout + addr_hout);
  streamin.stride.d0 = ctrl_config_.outfeat_stride.d0;
  streamin.stride.d1 = quant_bits==streamin_bits ? ctrl_config_.outfeat_stride.d1 : (quant_bits==32 && streamin_bits==8) ? ctrl_config_.outfeat_stride.d1/4 : 4*ctrl_config_.outfeat_stride.d1;
  streamin.stride.d2 = quant_bits==streamin_bits ? ctrl_config_.outfeat_stride.d2 : (quant_bits==32 && streamin_bits==8) ? ctrl_config_.outfeat_stride.d2/4 : 4*ctrl_config_.outfeat_stride.d2;
  streamin.length.d0 = load_store_status.streamin.count.word;
  streamin.length.d1 = load_store_status.streamin.count.wout;
  streamin.length.d2 = load_store_status.streamin.count.hout; // unused
  return streamin;
}

void ResetNormQuantMultIteration(){tiles.index.norm_quant_mult = 0;}
void ResetNormQuantShiftIteration(){tiles.index.norm_quant_shift = 0;}
void ResetNormQuantBiasIteration(){tiles.index.norm_quant_bias = 0;}

bool NormQuantMultIteration(){
  tiles.index.norm_quant_mult++; 
  if(ctrl_config_.config0.normalization_bit_count == 32){
    tiles.count.norm_quant_mult = current_tile_size.kout%8 ? 1+ current_tile_size.kout/8 : current_tile_size.kout/8;
  } else {
    tiles.count.norm_quant_mult = 1;
  }
  if(tiles.index.norm_quant_mult==tiles.count.norm_quant_mult)
    return true;
  else 
    return false;
}
bool NormQuantShiftIteration(){
  tiles.index.norm_quant_shift++;
  if(tiles.index.norm_quant_shift==tiles.count.norm_quant_shift)
    return true;
  else 
    return false;
}
bool NormQuantBiasIteration(){
  tiles.index.norm_quant_bias++;
  tiles.count.norm_quant_bias = current_tile_size.kout%8 ? 1+ current_tile_size.kout/8 : current_tile_size.kout/8;
  if(tiles.index.norm_quant_bias==tiles.count.norm_quant_bias)
    return true;
  else 
    return false;
}

int GetNormQuantMultWidth(){
  if(ctrl_config_.config0.normalization_bit_count == 8)
    return current_tile_size.kout;
  else {
    if(tiles.index.norm_quant_mult==tiles.count.norm_quant_mult-1)
      return  4*(current_tile_size.kout-tiles.index.norm_quant_mult*8);
    else
      return 32;
  }
}

int GetNormQuantBiasWidth(){
  tiles.count.norm_quant_bias = current_tile_size.kout%8 ? 1+ current_tile_size.kout/8 : current_tile_size.kout/8;
  if(tiles.index.norm_quant_bias==tiles.count.norm_quant_bias-1)
    return  4*(current_tile_size.kout-tiles.index.norm_quant_bias*8);
  else
    return 32;
}

int GetNormQuantShiftWidth(){
  return  current_tile_size.kout;
}


StreamerConfig GetNormquantMultStreamerConfig(){
  AddrType addr_kout = (ctrl_config_.config0.normalization_bit_count == 32 ? 4 : 1) * prev_tiles.index.kout*NeurekaAccumulatorPerPECount;
  StreamerConfig normquant_mult;
  normquant_mult.base_addr = L1_MASK & (ctrl_config_.scale_ptr + addr_kout );
  normquant_mult.stride.d0 = 32; // if it is 4 bits then 1 iteration is enough
  normquant_mult.stride.d1 = 0;// unused
  normquant_mult.stride.d2 = 0;//unused
  int num_iters = current_tile_size.kout%8 ? 1 + current_tile_size.kout/8 : current_tile_size.kout/8;
  normquant_mult.length.d0 = ctrl_config_.config0.normalization_bit_count==32 ? num_iters : 1;
  normquant_mult.length.d1 = 1;
  normquant_mult.length.d2 = 1;
  return normquant_mult;
}

StreamerConfig GetNormquantBiasStreamerConfig(){
  AddrType addr_kout = 4*prev_tiles.index.kout*NeurekaAccumulatorPerPECount;// always 32bits
  StreamerConfig normquant_bias;
  normquant_bias.base_addr = L1_MASK & (ctrl_config_.scale_bias_ptr + addr_kout);
  normquant_bias.stride.d0 = 32; // if it is 4 bits then 1 iteration is enough
  normquant_bias.stride.d1 = 0;// unused
  normquant_bias.stride.d2 = 0;//unused
  int num_iters = current_tile_size.kout%8 ? 1 + current_tile_size.kout/8 : current_tile_size.kout/8;
  normquant_bias.length.d0 = num_iters;
  normquant_bias.length.d1 = 1;
  normquant_bias.length.d2 = 1;
  return normquant_bias;
}

StreamerConfig GetNormquantShiftStreamerConfig(){
  AddrType addr_kout = prev_tiles.index.kout*NeurekaAccumulatorPerPECount;
  StreamerConfig normquant_shift;
  normquant_shift.base_addr = L1_MASK & (ctrl_config_.scale_shift_ptr + addr_kout );
  normquant_shift.stride.d0 = 32; // if it is 4 bits then 1 iteration is enough
  normquant_shift.stride.d1 = 0;// unused
  normquant_shift.stride.d2 = 0;//unused
  normquant_shift.length.d0 = 1;
  normquant_shift.length.d1 = 1;
  normquant_shift.length.d2 = 1;
  return normquant_shift;
}


std::array<bool, NeurekaInFeatScalarBufferCount>  GetPaddingEnable(){
  int h_index = load_store_status.infeat.index.hin;
  int w_index = load_store_status.infeat.index.win;
  int h_count = load_store_status.infeat.count.hin;
  int w_count = load_store_status.infeat.count.win;
  Padding padding = ctrl_config_.padding;
  std::array<bool, NeurekaInFeatScalarBufferCount> enable;
  std::fill(enable.begin(), enable.end(), 0);
  int padding_lim = padding.left;
  if(padding.left > 0 && w_index<padding.left && tiles.index.win==0)
    std::fill(enable.begin(), enable.end(), 1);
  if(padding.top > 0 && h_index<padding.top && tiles.index.hin==0)
    std::fill(enable.begin(), enable.end(), 1);
  if(padding.right > 0 && w_index>=w_count-padding_lim && tiles.index.win==tiles.count.win-1)
    std::fill(enable.begin(), enable.end(), 1);
  if(padding.bottom > 0 && h_index>=h_count-padding_lim && tiles.index.hin==tiles.count.hin-1)
    std::fill(enable.begin(), enable.end(), 1);

  return enable;
}

};

#endif