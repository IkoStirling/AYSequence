#pragma once
#include <algorithm>
#include <cmath>
namespace ayt::sequence::detail {
// Average the authored square-wave gate over a finite exposure. No sample loop,
// and no subtraction of large absolute cycle integrals at late show times.
inline double strobeExposureAverage(double frequency,double duty,double phaseCycles,double begin,double end) noexcept {
 if(frequency<=0)return 1;
 if(duty<=0)return 0;if(duty>=1)return 1;
 const double length=(end-begin)*frequency;
 double start=begin*frequency+phaseCycles;start-=std::floor(start);
 if(length<=0)return start<duty?1:0;
 const double full=std::floor(length),remaining=length-full;
 const auto primitive=[&](double x){const double n=std::floor(x);return n*duty+std::min(x-n,duty);};
 return std::clamp((full*duty+primitive(start+remaining)-primitive(start))/length,0.0,1.0);
}
// Looped shows restart the authored strobe clock at each show boundary. Average
// whole show periods analytically, so a very short looping show stays bounded.
inline double strobeShowExposureAverage(double frequency,double duty,double phaseCycles,double time,double shutter,double showDuration,bool loop) noexcept {
 if(shutter<=0)return strobeExposureAverage(frequency,duty,phaseCycles,time,time);
 if(!loop){const double begin=std::max(0.0,time-shutter*.5),end=std::min(showDuration,time+shutter*.5);return strobeExposureAverage(frequency,duty,phaseCycles,begin,end);}
 const double remaining=std::fmod(shutter,showDuration);
 double result=(shutter-remaining)/shutter*strobeExposureAverage(frequency,duty,phaseCycles,0,showDuration);
 double begin=std::fmod(time-shutter*.5,showDuration);if(begin<0)begin+=showDuration;
 const double first=std::min(remaining,showDuration-begin),second=remaining-first;
 if(first>0)result+=first/shutter*strobeExposureAverage(frequency,duty,phaseCycles,begin,begin+first);
 if(second>0)result+=second/shutter*strobeExposureAverage(frequency,duty,phaseCycles,0,second);
 return std::clamp(result,0.0,1.0);
}

}
