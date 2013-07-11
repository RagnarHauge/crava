// $Id: well.cpp 882 2011-09-23 13:10:16Z perroe $

// Copyright (c)  2011, Norwegian Computing Center
// All rights reserved.
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
// �  Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// �  Redistributions in binary form must reproduce the above copyright notice, this list of
//    conditions and the following disclaimer in the documentation and/or other materials
//    provided with the distribution.
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
// OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
// SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
// OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
// EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <cassert>
#include <fstream>
#include <string>

#include "well.hpp"
#include "norsarwell.hpp"
#include "rmswell.hpp"

using namespace NRLib;

Well::Well()
{
  well_rmissing_ = -999.0;
  well_imissing_  = -999;
}


Well::Well(const std::string & name,
           double              rmissing,
           int                 imissing)
{
  well_name_ = name;
  well_rmissing_ = rmissing;
  well_imissing_ = imissing;
}


Well::Well(const std::string & file_name,
           bool              & read_ok):
blocked_logs_common_orig_thick_(NULL)
{
  ReadWell(file_name, read_ok);
}


Well::Well(const std::map<std::string,std::vector<double> > & cont_log,
           const std::map<std::string,std::vector<int> >    & disc_log,
           const std::string                                & well_name)
{
  cont_log_       = cont_log;
  disc_log_       = disc_log;
  well_name_      = well_name;
  well_rmissing_  = -999.0;
  well_imissing_  = -999;
}

Well::~Well(){
  delete blocked_logs_common_orig_thick_;
}

void Well::MoveWell(const Simbox    * simbox, 
                    double            delta_X, 
                    double            delta_Y, 
                    double            k_move)
{

  double delta_Z;
  double top_old, top_new;

  if(HasContLog("X_pos") && HasContLog("Y_pos")){
    std::vector<double> x_pos = GetContLog("X_pos");
    std::vector<double> y_pos = GetContLog("Y_pos");
    std::vector<double> z_pos = GetContLog("TVD");
    top_old = simbox->getTop(x_pos[0], y_pos[0]);

    for(unsigned int i=0; i<x_pos.size(); i++)
    {
      if(x_pos[i] != RMISSING)
        x_pos[i] = x_pos[i]+delta_X;
      if(y_pos[i] != RMISSING)
        y_pos[i] = y_pos[i]+delta_Y;
    }

    top_new = simbox->getTop(x_pos[0], y_pos[0]);

    delta_Z = top_new - top_old + k_move;

    for(unsigned int i=0; i<z_pos.size(); i++)
      z_pos[i] = z_pos[i]+delta_Z;
  }
}

void
Well::ReadWell(const std::string  & file_name,
               bool               & read_ok)
{
  if(file_name.find(".nwh",0) != std::string::npos) {
    NRLib::NorsarWell well(file_name);
    well_name_ = well.GetWellName();
    cont_log_ = well.GetContLog();
    n_data_ = well.GetNData();
    disc_log_ = well.GetDiscLog();
    read_ok = true;
  }
  else if(file_name.find(".rms",0) != std::string::npos) {
    NRLib::RMSWell well(file_name);
    well_name_ = well.GetWellName();
    n_data_ = well.GetNData();
    cont_log_ = well.GetContLog();
    disc_log_ = well.GetDiscLog();
    read_ok = true;
  }
  else
    read_ok = false;
}


void
Well::AddContLog(const std::string& name, const std::vector<double>& log)
{
  cont_log_[name] = log;
}


bool
Well::HasContLog(const std::string& name) const
{
  std::map<std::string, std::vector<double> >::const_iterator item = cont_log_.find(name);
  if(item != cont_log_.end())
    return(true);
  else
    return(false);
}


std::vector<double>&
Well::GetContLog(const std::string& name)
{
  std::map<std::string, std::vector<double> >::iterator item = cont_log_.find(name);
  assert(item != cont_log_.end());
  return item->second;
}


const std::vector<double>&
Well::GetContLog(const std::string& name) const
{
  std::map<std::string, std::vector<double> >::const_iterator item = cont_log_.find(name);
  assert(item != cont_log_.end());
  return item->second;
}


void
Well::RemoveContLog(const std::string& name)
{
  cont_log_.erase(name);
}


void
Well::AddDiscLog(const std::string& name, const std::vector<int>& log)
{
  disc_log_[name] = log;
}


std::vector<int> &
Well::GetDiscLog(const std::string& name)
{
  std::map<std::string, std::vector<int> >::iterator item = disc_log_.find(name);
  assert(item != disc_log_.end());
  return item->second;
}


const std::vector<int> &
Well::GetDiscLog(const std::string& name) const
{
  std::map<std::string, std::vector<int> >::const_iterator item = disc_log_.find(name);
  assert(item != disc_log_.end());
  return item->second;
}


void
Well::RemoveDiscLog(const std::string& name)
{
  disc_log_.erase(name);
}


void Well::SetWellName(const std::string& wellname)
{
  well_name_ = wellname;
}


bool Well::IsMissing(double x)const
{
  if (x == well_rmissing_)
    return true;
  else
    return false;
}

bool Well::IsMissing(int n)const
{
  if (n == well_imissing_)
    return true;
  else
    return false;
}


int Well::GetDiscValue(size_t index, const std::string& logname) const
{
  std::map<std::string, std::vector<int> >::const_iterator item = disc_log_.find(logname);
  assert(item != disc_log_.end());
  const std::vector<int>& log = item->second;
  assert(index < log.size());
  return log[index];
}


double Well::GetContValue(size_t index, const std::string& logname) const
{
  std::map<std::string,std::vector<double> >::const_iterator item = cont_log_.find(logname);
  assert(item != cont_log_.end());
  const std::vector<double>& log = item->second;
  assert(index < log.size());
  return log[index];
}


void Well::SetDiscValue(int value, size_t index, const std::string& logname)
{
  std::map<std::string,std::vector<int> >::iterator item = disc_log_.find(logname);
  assert(item != disc_log_.end());
  assert(index < item->second.size());
  (item->second)[index] = value;
}


void Well::SetContValue(double value, size_t index, const std::string& logname)
{
  std::map<std::string,std::vector<double> >::iterator item = cont_log_.find(logname);
  assert(item != cont_log_.end());
  assert(index < item->second.size());
  (item->second)[index] = value;
}


size_t Well::GetNlog() const
{
  return cont_log_.size() + disc_log_.size();
}


size_t Well::GetNContLog() const
{
  return cont_log_.size();
}


size_t Well::GetContLogLength(const std::string& logname) const
{
  std::map<std::string,std::vector<double> >::const_iterator item = cont_log_.find(logname);
  assert(item != cont_log_.end());

  return (item->second).size();
}

//----------------------------------------------------------------------------

void  Well::CalculateDeviation(const ModelSettings    * const model_settings,
                               float                  & dev_angle,
                               Simbox                 * simbox,
                               int                      use_for_wavelet_estimation)
{
  float maxDevAngle   = model_settings->getMaxDevAngle();
  float thr_deviation = float(tan(maxDevAngle*NRLib::Pi/180.0));  // Largest allowed deviation
  float max_deviation =  0.0f;
  float max_dz        = 10.0f;                      // Calculate slope each 10'th millisecond.

  std::vector<double> x_pos = this->GetContLog("X_pos");
  std::vector<double> y_pos = this->GetContLog("Y_pos");
  std::vector<double> z_pos = this->GetContLog("TVD");

  //
  // Find first log entry in simbox
  //
  int iFirst = IMISSING;
  for(unsigned int i=0 ; i < x_pos.size() ; i++)
  {
    if(simbox->isInside(x_pos[i], y_pos[i]))
    {
      if (z_pos[i] > simbox->getTop(x_pos[i], y_pos[i]))
      {
        iFirst = i;
        break;
      }
    }
  }

  if (iFirst != IMISSING) {
    //
    // Find last log entry in simbox
    //
    int iLast = iFirst;
    for(unsigned int i = iFirst + 1 ; i < x_pos.size() ; i++)
    {
      if(simbox->isInside(x_pos[i], y_pos[i]))
        {
          if (z_pos[i] > simbox->getBot(x_pos[i], y_pos[i]))
            break;
        }
      else
        break;
      iLast = i;
    }

    if (iLast > iFirst) {
      double x0 = x_pos[iFirst];
      double y0 = y_pos[iFirst];
      double z0 = z_pos[iFirst];
      for (int i = iFirst+1 ; i < iLast+1 ; i++) {
        double x1 = x_pos[i];
        double y1 = y_pos[i];
        double z1 = z_pos[i];
        float dz = static_cast<float>(z1 - z0);

        if (dz > max_dz || i == iLast) {
          float deviation = static_cast<float>(sqrt((x1 - x0)*(x1 - x0) + (y1 - y0)*(y1 - y0))/dz);
          if (deviation > max_deviation) {
            x0 = x1;
            y0 = y1;
            z0 = z1;
            max_deviation = deviation;
          }
        }
      }
    }
    dev_angle = static_cast<float>(atan(max_deviation)*180.0/NRLib::Pi);
    LogKit::LogFormatted(LogKit::Low,"   Maximum local deviation is %.1f degrees.",dev_angle);

    if (max_deviation > thr_deviation)
    {
      if(use_for_wavelet_estimation == ModelSettings::NOTSET) {
        use_for_wavelet_estimation = ModelSettings::NO;
      }
      is_deviated_ = true;
      LogKit::LogFormatted(LogKit::Low," Well is treated as deviated.\n");
    }
    else
    {
      is_deviated_ = false;
      LogKit::LogFormatted(LogKit::Low,"\n");
    }
  }
  else {
    is_deviated_ = false;
    LogKit::LogFormatted(LogKit::Low,"Well is outside inversion interval. Cannot calculate deviation.\n");
  }

}
