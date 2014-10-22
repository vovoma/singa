// Copyright © 2014 Wei Wang. All Rights Reserved.
// 2014-10-20 12:53
#include <armci.h>
#include <glog/logging.h>
#include <armci.h>
#include <stdlib.h>
#include <utility>

#include "da/gary.h"
namespace lapis {
int GAry::id_=0;
int GAry::groupsize_=1;

GAry::~GAry(){
  ARMCI_Free((void*) dptrs_[id_]);
  free(dptrs_);
}
float* GAry::Setup(const Shape& shape, Partition* part){
  shape_=shape;
  shape2d_.size=shape_.size;
  shape2d_.dim=2;
  pdim_=part->getpDim();
  if(pdim_>0)
    shape2d_.s[0]=shape.s[0];
  else
    shape2d_.s[0]=1;
  for (int i = 1; i < pdim_; i++) {
    shape2d_.s[0]*=shape.s[i];
  }
  shape2d_.s[1]=shape.size/shape2d_.s[0];
  CHECK(shape2d_.s[1]%groupsize_==0);

  lo_[0]=0;
  hi_[0]=shape2d_.s[0];
  lo_[1]=shape2d_.s[1]/groupsize_*id_;
  hi_[1]=shape2d_.s[1]/groupsize_*(id_+1);

  part->start=lo_[1];
  part->stepsize=hi_[1]-lo_[1];
  part->stride=shape2d_.s[1];
  part->end=shape.size-(part->stride-hi_[1]);
  part->size=shape.size/groupsize_;
  part->stride=part->size<part->stride?part->size:part->stride;

  dptrs_=(float**) malloc(sizeof(float*)* groupsize_);//new FloatPtr[groupsize_];
  ARMCI_Malloc((void**)dptrs_, sizeof(float)*shape.size/groupsize_);
  return dptrs_[id_];
}

const Range GAry::IndexRange(int k){
  if(k==pdim_)
    return std::make_pair(shape_.s[k]/groupsize_*id_, shape_.s[k]/groupsize_*(id_+1));
  else return std::make_pair(0,shape_.s[k]);
}


/**
 * dst<-src
 * fetch data from src that is the same area as the partition of dst
 * offset is src's offset to its base dary
 * this function fetches against base dary
 * into two dimension area.
 */
float* GAry::Fetch(const Partition& part, int offset)const {
  int unit=sizeof(float), stridelevel=1;
  int width=shape2d_.s[1], gwidth=width/groupsize_;
  CHECK(part.stride%width==0||width%part.stride==0);
  int srow=(offset+part.start)/width;
  int count[2];
  count[0]=std::min(part.stepsize, gwidth)*unit;
  count[1]=(part.end-part.start)/width+((part.end-part.start)%width!=0);
  if(part.stride-part.stepsize>width)
    count[1]/=part.stride/part.stepsize;
  int start=(offset+part.start)%width;
  int srcstride=unit*((width%part.stride!=0)+width/part.stride)*std::min(width, part.stepsize);
  int tgtstride=gwidth*unit;
  float* ret=new float[part.size];
  float* srcaddr=ret;
  for(int i=0, len=0;i<part.size*unit/(count[0]*count[1]); i++){
    int gid=(len+start)/gwidth;
    float* tgtaddr=dptrs_[gid]+srow*gwidth+(len+start)%gwidth;
    ARMCI_GetS((void*)tgtaddr, &tgtstride, (void*)srcaddr, &srcstride, count, stridelevel, gid);
    srcaddr+=std::min(gwidth, part.stepsize);
    len+=std::min(part.stride, gwidth);
    if(len==width){
      len=0;
      srow+=part.stride/width;
      start+=width*part.stride/width;
    }
  }
 return ret;
}

/**
 * this is called to fetch data of the based dary which has the same shape as
 * the gary
 */
float* GAry::Fetch(const vector<Range>& slice) const {
  bool local=true;
  int size=1;
  CHECK(2==slice.size());
  for(int i=0; i<2;i++){
    size*=slice[i].second-slice[i].first;
    local&=(slice[i].second==hi_[i]&&slice[i].first==lo_[i]);
  }
  if(local)
    return dptrs_[id_];

  float* ret=new float[size];
  int lo0=slice[0].first, hi0=slice[0].second;
  int lo1=slice[1].first, hi1=slice[1].second;
  int w=hi_[1]-lo_[1];
  int sgroup=lo1/w;
  int egroup=hi1/w+hi1%w!=0;
  float *srcaddr=ret;
  int unit=sizeof(float);
  int tgtstride=w*unit;
  int srcstride=(hi1-lo1)*unit;
  int count[2];
  int stridelevel=1;
  count[1]=hi0-lo0;
  for(int g=sgroup; g<egroup;g++){
    if(g==sgroup)
      count[0]=std::min((w-lo1%w)*unit, srcstride);
    else if(g>sgroup&&g<egroup-1)
      count[0]=w*unit;
    else
      count[0]=(hi1%w==0?w:hi1%w)*unit;
    float *tgtaddr=dptrs_[g]+w*lo0+(g==sgroup?lo1%w:0);
    ARMCI_GetS(srcaddr, &srcstride, tgtaddr, &tgtstride, count, stridelevel,sgroup);
    srcaddr+=count[0]/unit;
  }
  return ret;
}
}  // namespace lapis
