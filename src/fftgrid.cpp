/***************************************************************************
*      Copyright (C) 2008 by Norwegian Computing Center and Statoil        *
***************************************************************************/

#include <iostream>
#include <fstream>
#include <math.h>
#include <time.h>
#include <assert.h>
#include <stdio.h>
#include <string>

#ifdef PARALLEL
#include <omp.h>
#endif

#include "lib/random.h"
#include "lib/utils.h"
#include "lib/timekit.hpp"

#include "fftw.h"
#include "rfftw.h"
#include "fftw-int.h"
#include "f77_func.h"

#include "nrlib/exception/exception.hpp"
#include "nrlib/iotools/logkit.hpp"
#include "nrlib/iotools/fileio.hpp"
#include "nrlib/segy/segy.hpp"

#include "src/fftgrid.h"
#include "src/simbox.h"
#include "src/timings.h"
#include "src/definitions.h"
#include "src/gridmapping.h"
#include "src/io.h"
#include "src/tasklist.h"
#include "src/seismicparametersholder.h"

FFTGrid::FFTGrid(int nx, int ny, int nz, int nxp, int nyp, int nzp)
{
  cubetype_       = CTMISSING;
  theta_          = RMISSING;
  nx_             = nx;
  ny_             = ny;
  nz_             = nz;
  nxp_            = nxp;
  nyp_            = nyp;
  nzp_            = nzp;
  scale_          = 1.0;
  rValMin_        = RMISSING;
  rValMax_        = RMISSING;
  rValAvg_        = RMISSING;
  cnxp_           = nxp_/2+1;
  rnxp_           = 2*(cnxp_);

  csize_          = cnxp_*nyp_*nzp_;
  rsize_          = rnxp_*nyp_*nzp_;

  counterForGet_  = 0;
  counterForSet_  = 0;
  istransformed_  = false;
  rvalue_         = NULL;
  add_            = true;

  // index= i+rnxp_*j+k*rnxp_*nyp_;
  // i index in x direction
  // j index in y direction
  // k index in z direction
}

FFTGrid::FFTGrid(FFTGrid * fftGrid, bool expTrans)
{
  cubetype_       = fftGrid->cubetype_;
  theta_          = fftGrid->theta_;
  nx_             = fftGrid->nx_;
  ny_             = fftGrid->ny_;
  nz_             = fftGrid->nz_;
  nxp_            = fftGrid->nxp_;
  nyp_            = fftGrid->nyp_;
  nzp_            = fftGrid->nzp_;
  scale_          = fftGrid->scale_;
  rValMin_        = fftGrid->rValMin_;
  rValMax_        = fftGrid->rValMax_;
  rValAvg_        = fftGrid->rValAvg_;

  cnxp_           = nxp_/2+1;
  rnxp_           = 2*(cnxp_);

  csize_          = cnxp_*nyp_*nzp_;
  rsize_          = rnxp_*nyp_*nzp_;

  counterForGet_  = fftGrid->getCounterForGet();
  counterForSet_  = fftGrid->getCounterForSet();
  add_            = fftGrid->add_;
  istransformed_  = fftGrid->getIsTransformed();

  if(istransformed_ == false) {
    createRealGrid(add_);
    for(int k=0;k<nzp_;k++) {
      for(int j=0;j<nyp_;j++) {
        for(int i=0;i<rnxp_;i++) {
          float value = fftGrid->getNextReal();
          if (expTrans)
            setNextReal(exp(value));
          else
            setNextReal(value);
        }
      }
    }
  }
  else {
    createComplexGrid();
    for(int k=0;k<nzp_;k++) {
      for(int j=0;j<nyp_;j++) {
        for(int i=0;i<cnxp_;i++) {
          fftw_complex value = fftGrid->getNextComplex();
          setNextComplex(value);
        }
      }
    }
  }
}

FFTGrid::~FFTGrid()
{
  if (rvalue_!=NULL)
  {
    if(add_==true)
      nGrids_ = nGrids_ - 1;
    fftw_free(rvalue_);
    FFTMemUse_ -= rsize_ * sizeof(fftw_real);
    LogKit::LogFormatted(LogKit::DebugLow,"\nFFTGrid Destructor: nGrids_ = %d",nGrids_);
  }
}

void
FFTGrid::fillInData(const Simbox      * timeSimbox,
                    StormContGrid     * grid,
                    const SegY        * segy,
                    float               smooth_length,
                    int               & missingTracesSimbox,
                    int               & missingTracesPadding,
                    int               & deadTracesSimbox,
                    const std::string & parName,
                    std::string       & errTxt,
                    bool                scale,
                    bool                is_segy)
{
  assert(cubetype_ != CTMISSING);

  double wall=0.0, cpu=0.0;
  TimeKit::getTime(wall,cpu);

  createRealGrid();

  size_t max_extrap_start = 0;
  size_t max_extrap_end   = 0;

  bool   seismic_data     = (cubetype_ == DATA);

  float  scalvert         = 1.0;
  float  scalhor          = 1.0;

  if (scale) {
    LogKit::LogFormatted(LogKit::Low,"Sgri file read. Rescaling z axis from s to ms, x and y from km to m. \n");
    scalvert       = 0.001f; //1000.0;
    scalhor        = 0.001f; //1000.0;
    smooth_length *= scalvert;
  }

  LogKit::LogFormatted(LogKit::Low,"\nResampling %s data into %dx%dx%d grid:",parName.c_str(),nxp_,nyp_,nzp_);

  setAccessMode(READANDWRITE);

  float monitorSize = std::max(1.0f, static_cast<float>(nyp_*rnxp_)*0.02f);
  float nextMonitor = monitorSize;
  std::cout
    << "\n  0%       20%       40%       60%       80%      100%"
    << "\n  |    |    |    |    |    |    |    |    |    |    |  "
    << "\n  ^";

  //
  // Find proper length of time samples to get N*log(N) performance in FFT.
  //
  size_t n_samples = 0;
  if (is_segy) {
    n_samples = segy->FindNumberOfSamplesInLongestTrace();
  }
  else {
    n_samples = grid->GetNK();
  }
  int nt = findClosestFactorableNumber(static_cast<int>(n_samples));

  int count1 = 0; // Part of simbox is outside seismic data
  int count2 = 0; // Part of padding is outside seismic data
  int count3 = 0; // Simbox is inside seismic data but trace is missing

#ifdef PARALLEL
  // Parallelize outer loop. The keyword collapse(2) would have added inner loop
  // The reduction in the omp pragma takes a local copy of the variable when
  // entering the for-loop and sums the variables on leaving the loop.

  int  chunk_size = 1;
  int  n_threads  = 4;
#pragma omp parallel for schedule(dynamic, chunk_size) reduction(+:count1, count2, count3) num_threads(n_threads)
#endif

  for (int j = 0 ; j < nyp_ ; j++) {
    for (int i = 0 ; i < rnxp_ ; i++) {

      int refi  = getFillNumber(i, nx_, nxp_ ); // Find index (special treatment for padding)
      int refj  = getFillNumber(j, ny_, nyp_ ); // Find index (special treatment for padding)
      int refk  = 0;

      double x, y, z0;
      timeSimbox->getCoord(refi, refj, refk, x, y, z0);  // Get lateral position and z-start (z0)

      double dz      = timeSimbox->getdz(refi, refj);
      float  xf      = static_cast<float>(x*scalhor);
      float  yf      = static_cast<float>(y*scalhor);
      float  dz_grid = static_cast<float>(dz*scalvert);
      float  z0_grid = static_cast<float>(z0*scalvert);

      bool is_inside = false;
      if (is_segy)
        is_inside = segy->GetGeometry()->IsInside(xf, yf);
      else
        is_inside = grid->IsInside(xf, yf);

      if (is_inside) {
        bool  missing = true;
        float z0_data = RMISSING;
        float dz_data = RMISSING;
        float dz_min  = RMISSING;

        std::vector<float> data_trace;

        if (is_segy) {
          segy->GetNearestTrace(data_trace, missing, z0_data, xf, yf);
          dz_data = segy->GetDz();
          dz_min  = dz_data/4.0f;
        }
        else {
          // Stormcontgrid does not return missing, but FindXYIndex has a throw if it is outside. Catch and turn into missing!!
          makeTraceFromStormGrid(grid, data_trace, z0_data, dz_data, dz_min, xf, yf);
        }

        /*
        std::cout << "INPUT: xf, yf, z0_data, dz_data = " << std::fixed << std::setprecision(2) << xf << " , " << yf << " , " << z0_data << " , " << dz_data << std::endl;
        for (unsigned int kk = 0 ; kk < data_trace.size() ; kk++) {
          std::cout << std::setw(3) << kk << "  "
                    << std::setprecision(2) << std::setw(20) << z0_data + kk*dz_data
                    << std::setprecision(6) << std::setw(20) << data_trace[kk] << std::endl;
        }
        */

        if (!is_segy || (is_segy && !missing)) { // Denne bør bli ==> !missing
          size_t n_data      = data_trace.size();
          float  trend_first = 0.0;
          float  trend_last  = 0.0;

          if (!seismic_data) {
            extrapolateAtEnds(data_trace,        // Get rid of leading and trailing zeros
                              max_extrap_start,
                              max_extrap_end);

            trend_first = data_trace[0];
            trend_last  = data_trace[n_data - 1];

            removeTrendFromTrace(data_trace,
                                 trend_first,
                                 trend_last);

            nt = findClosestFactorableNumber(static_cast<int>(n_data));
          }

          int          mt       = 4*nt; // Use four times the sampling density for the fine-meshed data
          int          cnt      = nt/2 + 1;
          int          rnt      = 2*cnt;
          int          cmt      = mt/2 + 1;
          int          rmt      = 2*cmt;

          rfftwnd_plan fftplan1 = rfftwnd_create_plan(1, &nt, FFTW_REAL_TO_COMPLEX, FFTW_ESTIMATE | FFTW_IN_PLACE);
          rfftwnd_plan fftplan2 = rfftwnd_create_plan(1, &mt, FFTW_COMPLEX_TO_REAL, FFTW_ESTIMATE | FFTW_IN_PLACE);

          fftw_real  * rAmpData = static_cast<fftw_real*>(fftw_malloc(sizeof(float)*rnt));
          fftw_real  * rAmpFine = static_cast<fftw_real*>(fftw_malloc(sizeof(float)*rmt));

          std::vector<float> grid_trace(nzp_);

          if (seismic_data) {
            smoothTraceInGuardZone(data_trace,
                                   dz_data,
                                   smooth_length);
          }

          resampleTrace(data_trace,
                        fftplan1,
                        fftplan2,
                        rAmpData,
                        rAmpFine,
                        cnt,
                        rnt,
                        cmt,
                        rmt);

          interpolateGridValues(grid_trace,
                                z0_grid,     // Centre of first cell
                                dz_grid,
                                rAmpFine,
                                z0_data,     // Time of first data sample
                                dz_min,
                                rmt);

          if (!seismic_data) {
            addTrendToTrace(grid_trace, // Trend is also added in padding so that the grid_trace is filled everywhere
                            trend_first,
                            trend_last,
                            n_data,
                            nz_,
                            dz_data,
                            dz_grid,
                            z0_grid,
                            z0_data);
          }

          /*
          for (size_t k = 0 ; k < static_cast<size_t>(grid_trace.size()) ; k++) {
            std::cout << std::fixed
                      << std::setw(3)  << k <<  " "
                      << std::setw(15) << std::setprecision(1) << z0_grid + k*dz_grid
                      << std::setw(12) << std::setprecision(6) << grid_trace[k] << std::endl;
          }
          */

          fftw_free(rAmpData);
          fftw_free(rAmpFine);

          //fftwnd_destroy_plan(fftplan1);
          //fftwnd_destroy_plan(fftplan2);

          setTrace(grid_trace, i, j);
        }
        else {
          setTrace(0.0f, i, j); // Dead traces (in case we allow them)
          count1++;
        }

      }
      else {
        setTrace(0.0f, i, j);   // Outside seismic data grid
        if (i < nx_ && j < ny_ )
          count2++;
        else
          count3++;
      }

#ifdef PARALLEL
#pragma omp critical
#endif
      if (rnxp_*j + i + 1 >= static_cast<int>(nextMonitor)) {
        nextMonitor += monitorSize;
        std::cout << "^";
        fflush(stdout);
      }
    }
  }
  LogKit::LogFormatted(LogKit::Low,"\n");
  endAccess();

  deadTracesSimbox     = count1; // Need to use temporary variables for parallelization
  missingTracesSimbox  = count2; // Need to use temporary variables for parallelization
  missingTracesPadding = count3; // Need to use temporary variables for parallelization

  if (max_extrap_start > 0) {
    LogKit::LogFormatted(LogKit::Low,"WARNING: Data did not cover grid. The top %d layers were partly or fully extrapolated from first defined value\n", max_extrap_start);
  }
  if (max_extrap_end > 0) {
    LogKit::LogFormatted(LogKit::Low,"WARNING: Data did not cover grid. The bottom %d layers were partly or fully extrapolated from last defined value\n", max_extrap_end);
  }

  Timings::setTimeResamplingSeismic(wall,cpu);
}


void
FFTGrid::makeTraceFromStormGrid(StormContGrid      * grid,
                                std::vector<float> & data_trace,
                                float              & z0_data,
                                float              & dz_data,
                                float              & dz_min,
                                float                xf,
                                float                yf)
{
  size_t grid_i =   0;
  size_t grid_j =   0;

  double grid_x = 0.0;
  double grid_y = 0.0;
  double grid_z = 0.0;

  float z_min   = 0.0;
  float z_max   = 0.0;

  grid->FindXYIndex(xf, yf, grid_i, grid_j);
  for (size_t k = 0; k < grid->GetNK(); k++) {
    grid->FindCenterOfCell(grid_i, grid_j, k, grid_x, grid_y, grid_z);
    float value  = grid->GetValueZInterpolated(grid_x, grid_y, grid_z);
    data_trace.push_back(value);

    if (k == 0)
      z_min = static_cast<float>(grid_z);
    if (k == grid->GetNK() - 1)
      z_max = static_cast<float>(grid_z);
  }

  dz_data = (z_max - z_min) / grid->GetNK();
  dz_min  = dz_data/4.0f;
  z0_data = z_min;
}

void
FFTGrid::extrapolateAtEnds(std::vector<float> & data_trace,
                           size_t             & max_extrap_start,
                           size_t             & max_extrap_end)
{
  size_t n = data_trace.size();
  //
  // Get rid of leading zeros (= undefined values)
  //
  size_t k = 0;
  while (data_trace[k] == 0.0) {
    k++;
  }
  size_t first_def = k;
  for (k = 0 ; k < first_def ; k++) {
    data_trace[k] = data_trace[first_def];
  }
  //
  // Get rid of trailing zeros (= undefined values)
  //
  k = n - 1;
  while (data_trace[k] == 0.0) {
    k--;
  }
  size_t last_def = k;
  for (k = last_def + 1 ; k < n ; k++) {
    data_trace[k] = data_trace[last_def];
  }

#ifdef PARALLEL
#pragma omp critical
#endif
  {
    max_extrap_start = std::max(max_extrap_start, first_def);
    max_extrap_end   = std::max(max_extrap_end, n - last_def - 1);
  }
}

void
FFTGrid::removeTrendFromTrace(std::vector<float> & data_trace,
                              float                trend_first,
                              float                trend_last)
{
  size_t n = data_trace.size();
  float  a = (trend_last - trend_first)/(n - 1); // Slope

  for (size_t k = 0 ; k < n ; k++) {
    data_trace[k] -= trend_first + a*k;
  }
}

void
FFTGrid::smoothTraceInGuardZone(std::vector<float> & data_trace,
                                float                dz_data,
                                float                smooth_length)
{
  // We recommend a guard zone of at least half a wavelet on each side of
  // the target zone and that half a wavelet of the guard zone is smoothed.
  //
  // By default, we have: guard_zone = smooth_length = 0.5*wavelet = 100ms
  //
  // Originally, we checked here that the guard zone was larger than or equal
  // to the smooth length. However, as a trace is generally not located in
  // the center of the grid cell, we made an invalid comparison of z-values
  // from different reference systems. Only when the top/base surface is flat
  // such a comparison becomes valid. Instead, we hope and assume that the
  // SegY volume made in ModelGeneral::readSegyFile() has been made with
  // the correct guard zone in method.

 //
  // k=n_smooth is the first sample within the simbox. For this sample
  // the smoothing factor (if it had been applied) should be one.
  //
  int n_smooth = static_cast<int>(floor(smooth_length/dz_data));

  for (int k = 0 ; k < n_smooth ; k++) {
    double theta   = static_cast<double>(k)/static_cast<double>(n_smooth);
    float  sinT    = static_cast<float>(std::sin(NRLib::PiHalf*theta));
    data_trace[k] *= sinT*sinT;
  }

  int n_data = data_trace.size();
  int kstart = n_data - n_smooth;

  for (int k = 0 ; k < n_smooth ; k++) {
    double theta   = static_cast<double>(k + 1)/static_cast<double>(n_smooth);
    float  cosT    = static_cast<float>(std::cos(NRLib::PiHalf*theta));
    data_trace[kstart + k] *= cosT*cosT;
  }
}


void
FFTGrid::resampleTrace(const std::vector<float> & data_trace,
                       const rfftwnd_plan       & fftplan1,
                       const rfftwnd_plan       & fftplan2,
                       fftw_real                * rAmpData,
                       fftw_real                * rAmpFine,
                       int                        cnt,
                       int                        rnt,
                       int                        cmt,
                       int                        rmt)
{
  fftw_complex * cAmpData = reinterpret_cast<fftw_complex*>(rAmpData);
  fftw_complex * cAmpFine = reinterpret_cast<fftw_complex*>(rAmpFine);

  //
  // Fill vector to be FFT'ed
  //
  int n_data = static_cast<int>(data_trace.size());

  for (int i = 0 ; i < n_data ; i++) {
    rAmpData[i] = data_trace[i];
  }
  // Pad with zeros
  for (int i = n_data ; i < rnt ; i++) {
    rAmpData[i] = 0.0f;
  }

  //
  // Transform to Fourier domain
  //
  rfftwnd_one_real_to_complex(fftplan1, rAmpData, cAmpData);

  //
  // Fill fine-sampled grid
  //
  for (int i = 0 ; i < cnt ; i++) {
    cAmpFine[i].re = cAmpData[i].re;
    cAmpFine[i].im = cAmpData[i].im;
  }
  // Pad with zeros (cmt is always greater than cnt)
  for (int i = cnt ; i < cmt ; i++) {
    cAmpFine[i].re = 0.0f;
    cAmpFine[i].im = 0.0f;
  }

  //
  // Fine-sampled grid: Fourier --> Time
  //
  rfftwnd_one_complex_to_real(fftplan2, cAmpFine, rAmpFine);

  //
  // Scale and fill grid_trace
  //
  float scale = 1/static_cast<float>(rnt);
  for(int i = 0 ; i < rmt ; i++) {
    rAmpFine[i] = scale*rAmpFine[i];
  }
}

void
FFTGrid::interpolateGridValues(std::vector<float> & grid_trace,
                               float                z0_grid,
                               float                dz_grid,
                               fftw_real          * rAmpFine,
                               float                z0_data,
                               float                dz_fine,
                               int                  n_fine)
{
  //
  // Bilinear interpolation
  //
  // refk establishes link between traces order and grid order
  // In trace:    A A A B B B B B B C C C     (A and C are values in padding)
  // In grid :    B B B B B B C C C A A A

  float z0_shift    = z0_grid - z0_data;
  float inv_dz_fine = 1.0f/dz_fine;

  int n_grid = static_cast<int>(grid_trace.size());

  for (int k = 0 ; k < n_grid ; k++) {
    int refk = getZSimboxIndex(k);
    float dl = (z0_shift + static_cast<float>(refk)*dz_grid)*inv_dz_fine;
    int   l1 = static_cast<int>(floor(dl));
    int   l2 = static_cast<int>(ceil(dl));

    if (l2 < 0 || l1 > n_fine - 1) {
      grid_trace[k] = 0.0f;
    }
    else {
      if (l1 < 0) {
        grid_trace[k] = rAmpFine[l2];
      }
      else if (l2 > n_fine - 1) {
        grid_trace[k] = rAmpFine[l1];
      }
      else if (l1 == l2) {
        grid_trace[k] = rAmpFine[l1];
      }
      else {
        float w1 = ceil(dl) - dl;
        float w2 = dl - floor(dl);
        grid_trace[k] = w1*rAmpFine[l1] + w2*rAmpFine[l2];
      }
    }
  }
}

void
FFTGrid::addTrendToTrace(std::vector<float> & grid_trace,
                         float                trend_first,
                         float                trend_last,
                         size_t               n_data,
                         int                  nz,
                         float                dz_data,
                         float                dz_grid,
                         float                z0_grid,
                         float                z0_data)
{
  //
  // First we have to transform trend values from data world to grid world
  //
  std::vector<float> trend(nz);
  float a = (trend_last - trend_first)/((n_data - 1)*dz_data); // Slope
  for (size_t k = 0 ; k < static_cast<size_t>(nz) ; k++) {
    float z  = z0_grid + k*dz_grid;
    trend[k] = a*(z - z0_data) + trend_first;
  }

  //    std::cout << "INPUT2 : nz, n_pad trend_last= " << nz << " " << grid_trace.size() - nz << "    " << trend_last << std::endl;

  for (size_t k = 0 ; k < static_cast<size_t>(nz) ; k++) {

    /*
      std::cout << std::fixed
              << std::setw(3)  << k <<  " "
              << std::setw(15) << std::setprecision(1) << z0_grid + k*dz_grid
              << std::setw(12) << std::setprecision(6) << grid_trace[k] << "   trend :  "
              << std::setw(15) << std::setprecision(1) << z0_grid + k*dz_grid
              << std::setw(12) << std::setprecision(6) << trend[k] << "  total :  "
              << std::setw(15) << std::setprecision(1) << z0_grid + k*dz_grid
              << std::setw(12) << std::setprecision(6) << grid_trace[k] + trend[k] << std::endl;
    */

    grid_trace[k] += trend[k];
  }

  size_t n_pad  = grid_trace.size() - nz;
  // Slope in padding. We are not using npad - 1.  We do not need padding to end at start value.
  a = (grid_trace[nz - 1] - grid_trace[0])/n_pad;

  for (size_t k = 0 ; k < n_pad ; k++) {

    /*
    std::cout << std::fixed
              << std::setw(3)  << nz + k
              << std::setw(27) <<  " " << "    padding trend :  "
              << std::setw( 8) << std::setprecision(1) << z0_grid + (nz + k)*dz_grid
              << std::setw(12) << std::setprecision(6) << trend_last - a*k << "   total : "
              << std::setw(15) << std::setprecision(1) << z0_grid + (nz + k)*dz_grid
              << std::setw(12) << std::setprecision(6) << trend_last - a*k << std::endl;
    */

    grid_trace[nz + k] = trend_last - a*k;
  }
}

// Trilinear interpolation
double FFTGrid::InterpolateTrilinear(double x_min,
                                     double x_max,
                                     double y_min,
                                     double y_max,
                                     double z_min,
                                     double z_max,
                                     double x,
                                     double y,
                                     double z)
{
  int i1,j1,k1,i2,j2,k2;

  double dx = (x_max - x_min)/nx_;
  double dy = (y_max - y_min)/ny_;
  double dz = (z_max - z_min)/nz_;

  // If values are outside definition area return MISSING
  if (z_min == z_max){
    return InterpolateBilinearXY(x_min, x_max, y_min, y_max, x, y);
  }
  else if(x < x_min || x > x_max || y < y_min || y > y_max  || z<z_min || z> z_max){
    return RMISSING;
  // Can only interpolate from x_min+dx/2 to x_max-dx/2 etc.
  } /*else if (x<x_min+dx/2 || x>x_max-dx/2 || y<y_min+dy/2 || y>y_max-dy/2 || z<z_min+dz/2 || z>z_max-dz/2 ){
    return 0;
  }*/
  else{
    // i1,j1,k1 can take values in the interval [0,nx],[0,ny],[0,nz]
    i1 = static_cast<int>(floor((x-x_min-dx/2)/dx));
    j1 = static_cast<int>(floor((y-y_min-dy/2)/dy));
    k1 = static_cast<int>(floor((z-z_min-dz/2)/dz));
    // i2,j2,k2 can take values in the interval [1,nx+1],[1,ny+1],[1,nz+1]
    i2 = i1+1;
    j2 = j1+1;
    k2 = k1+1;
  }
  // TRILINEAR INTERPOLATION
  double wi = (x - i1*dx-x_min-dx/2)/dx;
  double wj = (y - j1*dy-y_min-dy/2)/dy;
  double wk = (z - k1*dz-z_min-dz/2)/dz;

  assert (wi>=0 && wi<=1 && wj>=0 && wj<=1 && wk>=0 && wk<=1);


  double *value = new double[8];
  value[0] = 0;
  value[1] = 0;
  value[2] = 0;
  value[3] = 0;
  value[4] = 0;
  value[5] = 0;
  value[6] = 0;
  value[7] = 0;

  if(i1<0 && j1<0 && k1<0){
    value[7] = std::max<double>(0,this->getRealValue(i2,j2,k2));
  }
  else if(i1<0 && j1<0 && k1>=0){
    value[6] = std::max<double>(0,this->getRealValue(i2,j2,k1));
    value[7] = std::max<double>(0,this->getRealValue(i2,j2,k2));
  }
  else if(i1<0 && j1>=0 && k1>=0){
    value[4] = std::max<double>(0,this->getRealValue(i2,j1,k1));
    value[5] = std::max<double>(0,this->getRealValue(i2,j1,k2));
    value[6] = std::max<double>(0,this->getRealValue(i2,j2,k1));
    value[7] = std::max<double>(0,this->getRealValue(i2,j2,k2));
  }
  else if(i1<0 && j1>=0 && k1<0){
    value[5] = std::max<double>(0,this->getRealValue(i2,j1,k2));
    value[7] = std::max<double>(0,this->getRealValue(i2,j2,k2));
  }
  else if(i1>=0 && j1<0 && k1<0){
    value[3] = std::max<double>(0,this->getRealValue(i1,j2,k2));
    value[7] = std::max<double>(0,this->getRealValue(i2,j2,k2));
  }
  else if(i1>=0 && j1>=0 && k1<0){
    value[1] = std::max<double>(0,this->getRealValue(i1,j1,k2));
    value[3] = std::max<double>(0,this->getRealValue(i1,j2,k2));
    value[5] = std::max<double>(0,this->getRealValue(i2,j1,k2));
    value[7] = std::max<double>(0,this->getRealValue(i2,j2,k2));
  }
  else if(i1>=0 && j1<0 && k1>=0){
    value[2] = std::max<double>(0,this->getRealValue(i1,j2,k1));
    value[3] = std::max<double>(0,this->getRealValue(i1,j2,k2));
    value[6] = std::max<double>(0,this->getRealValue(i2,j2,k1));
    value[7] = std::max<double>(0,this->getRealValue(i2,j2,k2));
  }
  else if(i1>=0 && j1>=0 && k1>=0){
    value[0] = std::max<double>(0,this->getRealValue(i1,j1,k1));
    value[1] = std::max<double>(0,this->getRealValue(i1,j1,k2));
    value[2] = std::max<double>(0,this->getRealValue(i1,j2,k1));
    value[3] = std::max<double>(0,this->getRealValue(i1,j2,k2));
    value[4] = std::max<double>(0,this->getRealValue(i2,j1,k1));
    value[5] = std::max<double>(0,this->getRealValue(i2,j1,k2));
    value[6] = std::max<double>(0,this->getRealValue(i2,j2,k1));
    value[7] = std::max<double>(0,this->getRealValue(i2,j2,k2));
  }

  //float value=0;
  double returnvalue = 0;
  returnvalue += (1.0f-wi)*(1.0f-wj)*(1.0f-wk)*value[0];
  returnvalue += (1.0f-wi)*(1.0f-wj)*(     wk)*value[1];
  returnvalue += (1.0f-wi)*(     wj)*(1.0f-wk)*value[2];
  returnvalue += (1.0f-wi)*(     wj)*(     wk)*value[3];
  returnvalue += (     wi)*(1.0f-wj)*(1.0f-wk)*value[4];
  returnvalue += (     wi)*(1.0f-wj)*(     wk)*value[5];
  returnvalue += (     wi)*(     wj)*(1.0f-wk)*value[6];
  returnvalue += (     wi)*(     wj)*(     wk)*value[7];

  delete [] value;

  return returnvalue;
}

double FFTGrid::InterpolateBilinearXY(double x_min,
                                      double x_max,
                                      double y_min,
                                      double y_max,
                                      double x,
                                      double y)
{
  int i1,j1,i2,j2;

  double dx = (x_max - x_min)/nx_;
  double dy = (y_max - y_min)/ny_;

  // If values are outside definition area return RMISSING
  if(x < x_min || x > x_max || y < y_min || y > y_max){
    return RMISSING;
  // Can only interpolate from x_min+dx/2 to x_max-dx/2 etc.
  }
  else{
    // i1,j1,k1 can take values in the interval [-1,nx-1],[-1,ny-1]
    i1 = static_cast<int>(floor((x-x_min-dx/2)/dx));
    j1 = static_cast<int>(floor((y-y_min-dy/2)/dy));
    // i2,j2,k2 can take values in the interval [0,nx],[0,ny]
    i2 = i1+1;
    j2 = j1+1;
  }
  // BILINEAR INTERPOLATION
  double wi = (x - i1*dx-x_min-dx/2)/dx;
  double wj = (y - j1*dy-y_min-dy/2)/dy;

  assert (wi>=0 && wi<=1 && wj>=0 && wj<=1);

  double *value = new double[4];
  value[0] = 0;
  value[1] = 0;
  value[2] = 0;
  value[3] = 0;

  // all indexes inside grid
  if(i1>=0 && j1>=0 && i2<nx_ && j2<ny_){
    value[0] = std::max<double>(0,this->getRealValue(i1,j1,0));
    value[1] = std::max<double>(0,this->getRealValue(i1,j2,0));
    value[2] = std::max<double>(0,this->getRealValue(i2,j1,0));
    value[3] = std::max<double>(0,this->getRealValue(i2,j2,0));
  }
  // i1 and j1 outside grid
  else if(i1<0 && j1<0 && i2<nx_ && j2<ny_){
    value[3] = std::max<double>(0,this->getRealValue(i2,j2,0));
  }
  // j1 outside grid
  else if(i1>=0 && j1<0 && i2<nx_ && j2<ny_){
    value[1] = std::max<double>(0,this->getRealValue(i1,j2,0));
    value[3] = std::max<double>(0,this->getRealValue(i2,j2,0));
  }
  // j1 and i2 outside grid
  else if(i1>=0 && j1<0 && i2>=nx_ && j2<ny_){
    value[1] = std::max<double>(0,this->getRealValue(i1,j2,0));
  }
  // i1 outside grid
  else if(i1<0 && j1>=0 && i2<nx_ && j2<ny_){
    value[2] = std::max<double>(0,this->getRealValue(i2,j1,0));
    value[3] = std::max<double>(0,this->getRealValue(i2,j2,0));
  }

  // i2 outside grid
  else if(i1>=0 && j1>=0 && i2>=nx_ && j2<ny_){
    value[0] = std::max<double>(0,this->getRealValue(i1,j1,0));
    value[1] = std::max<double>(0,this->getRealValue(i1,j2,0));
  }
  // i1 and j2 outside grid
  else if(i1<0 && j1>=0 && i2<nx_ && j2>=ny_){
    value[2] = std::max<double>(0,this->getRealValue(i2,j1,0));
  }
  // j2 outside grid
  else if(i1>=0 && j1>=0 && i2<nx_ && j2>=ny_){
    value[0] = std::max<double>(0,this->getRealValue(i1,j1,0));
    value[2] = std::max<double>(0,this->getRealValue(i2,j1,0));
  }
  // i2 and j2 outside grid
  else if(i1>=0 && j1>=0 && i2>=nx_ && j2>=ny_ ){
    value[0] = std::max<double>(0,this->getRealValue(i1,j1,0));
  }
  else {
    // something is wrong
  }

  double returnvalue = 0;
  returnvalue += (1.0f-wi)*(1.0f-wj)*value[0];
  returnvalue += (1.0f-wi)*(     wj)*value[1];
  returnvalue += (     wi)*(1.0f-wj)*value[2];
  returnvalue += (     wi)*(     wj)*value[3];

  delete [] value;

  return returnvalue;
}

void
FFTGrid::setTrace(const std::vector<float> & trace, size_t i, size_t j)
{
  for (int k = 0 ; k < nzp_ ; k++) {
    setRealValue(i, j, k, trace[k], true);
  }
}

void
FFTGrid::setTrace(float value, size_t i, size_t j)
{
  for (int k = 0 ; k < nzp_ ; k++) {
    setRealValue(i, j, k, value, true);
  }
}

void
FFTGrid::fillInConstant(float value, bool add)
{
  if(rvalue_ == NULL) // if(rvalue_ != NULL), the grid is already created
    createRealGrid(add);

  int i,j,k;
  setAccessMode(WRITE);
  for( k = 0; k < nzp_; k++)
    for( j = 0; j < nyp_; j++)
      for( i = 0; i < rnxp_; i++)
      {
        if(i<nxp_)
          setNextReal(value);
        else
          setNextReal(RMISSING);
      }
  endAccess();
}

void
FFTGrid::fillInFromArray(float *value) //NB works only for padding size up to nxp=2*nx, nyp=2*ny, nzp=2*nz
{
  if(rvalue_ == NULL) // if(rvalue_ != NULL), the grid is already created
    createRealGrid();

  cubetype_ = PARAMETER;
  int i,j,k, ii,jj,kk, iii, jjj, kkk;
  setAccessMode(WRITE);
  kkk = 1;
  for( k = 0; k < nzp_; k++)
  {
    jjj = 1;
    if(k<nz_)
      kk = k;
    else
    {
      kk = k-kkk;
      kkk++;
    }

    for( j = 0; j < nyp_; j++)
    {
      iii = 1;
      if(j<ny_)
        jj = j;
      else
      {
        jj = j-jjj;
        jjj++;
      }
      for( i = 0; i < rnxp_; i++)
      {
        if(i<nx_)
          ii = i;
        else
        {
          ii = i-iii;
          iii++;
        }
        if(i<nxp_)
          setNextReal(value[ii+jj*nx_+kk*nx_*ny_]);
        else
          setNextReal(RMISSING);
      }
    }
  }

  endAccess();
}

void
FFTGrid::calculateStatistics()
{
  rValMin_ = +std::numeric_limits<float>::infinity();
  rValMax_ = -std::numeric_limits<float>::infinity();
  rValAvg_ = 0.0f;

  long long count = 0;
  float sum_xyz = 0.0f;

  setAccessMode(RANDOMACCESS);
  for (int k = 0 ; k < nz_ ; k++) {
    float sum_xy = 0.0f;
    for (int j = 0 ; j < ny_ ; j++) {
      float sum_x = 0.0f;
      for (int i = 0 ; i < nx_ ; i++) {
        float value = getRealValue(i,j,k);
        if (value != RMISSING) {
          if (value < rValMin_)
            rValMin_ = value;
          if (value > rValMax_)
            rValMax_ = value;
          sum_x += value;
          count++;
        }
      }
      sum_xy += sum_x;
    }
    sum_xyz += sum_xy;
  }
  endAccess();

  if (count > 0) {
    rValAvg_ = sum_xyz/count;
  }
}

void
FFTGrid::setUndefinedCellsToGlobalAverage() // Used for background model
{
  float globalAvg = getAvgReal();

  if (globalAvg == RMISSING) {
    calculateStatistics();
  }

  long long count = 0;

  setAccessMode(RANDOMACCESS);
  for (int k = 0 ; k < nzp_ ; k++) {
    for (int j = 0 ; j < nyp_ ; j++) {
      for (int i = 0 ; i < rnxp_ ; i++) {
        float value = getRealValue(i,j,k,true);
        if (value == RMISSING) {
          setRealValue(i,j,k,globalAvg,true);
          if (i < nx_ && j < ny_ && k < nz_) { // Do not count undefined in padding (confusing for user)
            count++;
          }
        }
      }
    }
  }
  endAccess();

  if (count > 0) {
    long long nxyz = static_cast<long>(nx_)*static_cast<long>(ny_)*static_cast<long>(nz_);
    LogKit::LogFormatted(LogKit::Medium, "\nThe grid contains %ld undefined grid cells (%.2f%). Setting these to global average\n",
                         count, 100.0f*static_cast<float>(count)/(static_cast<float>(count) + static_cast<float>(nxyz)),
                         globalAvg);
  }
}

void
FFTGrid::fillInErrCorr(const Surface * priorCorrXY,
                       float           gradI,
                       float           gradJ)
{
  // Note:  this contain the latteral correlation and the multiplyers for the
  // time correlation. The time correlation is further adjusted by the wavelet
  // and the derivative of the wavelet.
  // the angular correlation is given by a functional Expression elsewhere

  assert(istransformed_== false);

  int i,j,k,baseK,cycleI,cycleJ;
  float value,subK;
  int range = 1;
  setAccessMode(WRITE);
  for( k = 0; k < nzp_; k++)
    for( j = 0; j < nyp_; j++)
      for( i = 0; i < rnxp_; i++)
      {
        cycleI = i-nxp_;
        if(i < -cycleI)
          cycleI = i;
        cycleJ = j-nyp_;
        if(j < -cycleJ)
          cycleJ = j;

        subK  =  k+cycleI*gradI+cycleJ*gradJ;
        if(i < nxp_) {
          if(fabs(subK) < range*1.0f || fabs(subK-nzp_) < range*1.0f) {
            baseK =  int(subK);
            subK  -= baseK;
            while(baseK < -range)
              baseK += nzp_;       //Use cyclicity
            while(baseK >= range)
              baseK -= nzp_;       //Use cyclicity

            if(baseK < 0) {
              baseK = -1-baseK;
              subK  = 1-subK;
            }
            value = (1-subK)* float( (*(priorCorrXY))(i+nxp_*j) );
          }
          else
            value = 0;
        }
        else
          value = RMISSING;

        setNextReal(value);
      } //for k,j,i
      endAccess();
}

void
FFTGrid::fillInParamCorr(const Surface   * priorCorrXY,
                         const fftw_real * circCorrT,
                         float             gradI,
                         float             gradJ)
{
  assert(istransformed_== false);

  int baseK,cycleI,cycleJ;
  float value,subK;

  setAccessMode(WRITE);
  for(int k = 0; k < nzp_; k++) {
    for(int j = 0; j < nyp_; j++) {
      for(int i = 0; i < rnxp_; i++) {
        if(i < nxp_) {  // computes the index reference from the cube puts it in value
          cycleI = i-nxp_;
          if(i < -cycleI)
            cycleI = i;
          cycleJ = j-nyp_;
          if(j < -cycleJ)
            cycleJ = j;

          subK  =  k+cycleI*gradI+cycleJ*gradJ; //Subtract to counter rotation.
          baseK =  int(floor(subK));
          subK  -= baseK;
          while(baseK < 0)
            baseK += nzp_;       //Use cyclicity
          while(baseK >= nzp_)
            baseK -= nzp_;       //Use cyclicity
          value = (1-subK)*circCorrT[baseK];
          if(baseK != nzp_-1)
            value += subK*circCorrT[baseK+1];
          else
            value += subK*circCorrT[0];
          value *= float( (*(priorCorrXY))(i+nxp_*j));
        }
        else
          value = RMISSING;

        setNextReal(value);
      }
    }
  }

  endAccess();
}



void
FFTGrid::fillInTimeCov(const fftw_real * circCorrT)
{
  assert(istransformed_== false);
  float value;

  setAccessMode(WRITE);
  for(int k = 0; k < nzp_; k++) {
    for(int j = 0; j < nyp_; j++) {
      for(int i = 0; i < rnxp_; i++) {
          if(i==0 && j==0)
             value=circCorrT[k];
          else
            value=0.0;

          setNextReal(value);
      }
    }
  }
  endAccess();
}

void
FFTGrid::fillInGenExpCorr(double Rx,
                          double Ry,
                          double Rz,
                          float             gradI,
                          float             gradJ)
{
  assert(istransformed_== false);

  int cycleI,cycleJ;
  float value,subK;
  cubetype_=FFTGrid::COVARIANCE;

  setAccessMode(WRITE);
  for(int k = 0; k < nzp_; k++) {
    for(int j = 0; j < nyp_; j++) {
      for(int i = 0; i < rnxp_; i++) {
        if(i < nxp_) {  // computes the index reference from the cube puts it in value
          cycleI = i-nxp_;
          if(i < -cycleI)
            cycleI = i;
          cycleJ = j-nyp_;
          if(j < -cycleJ)
            cycleJ = j;

          subK  =  k+cycleI*gradI+cycleJ*gradJ; //Subtract to counter rotation
          while(subK <  -nzp_/2.0)
            subK += nzp_;       //Use cyclicity
          while(subK >= nzp_/2.0)
            subK -= nzp_;       //Use cyclicity

          double qDist=float(cycleI*cycleI)/(Rx*Rx)+ (cycleJ*cycleJ)/(Ry*Ry) +(subK*subK)/(Rz*Rz);
          double dist=std::sqrt( qDist  );
          value=float(std::exp(-3.0*dist));
        }
        else
          value = RMISSING;

        setNextReal(value);
      }
    }
  }

  endAccess();
}


void
FFTGrid::fillInComplexNoise(RandomGen * ranGen)
{
  assert(ranGen);
  istransformed_ = true;
  int i;
  cubetype_=PARAMETER;
  float std = float(1/sqrt(2.0));
  for(i=0;i<csize_;i++)
  {
    //if(xind == 0 || xind == nx-1 && nx is even)
    if(((i % cnxp_) == 0) || (((i % cnxp_) == cnxp_-1) && ((i % 2) == 1)))
    {
      int xshift = i % cnxp_;
      int jkind  = (i-xshift)/cnxp_;
      int jind   = jkind % nyp_;       //Index j along y-direction
      int kind   = int(jkind/nyp_);    //Index k along z-direction
      int jccind, kccind, jkccind;     //Indexes for complex conjugated
      if(jind == 0)
        jccind = 0;
      else
        jccind = nyp_-jind;
      if(kind == 0)
        kccind = 0;
      else
        kccind = nzp_-kind;
      jkccind = jccind+kccind*nyp_;
      if(jkccind == jkind)             //Number is its own cc, i. e. real
      {
        cvalue_[i].re = float(ranGen->rnorm01());
        cvalue_[i].im = 0;
      }
      else if(jkccind > jkind)         //Have not simulated cc yet.
      {
        cvalue_[i].re = float(std*ranGen->rnorm01());
        cvalue_[i].im = float(std*ranGen->rnorm01());
      }
      else                             //Look up cc value
      {
        int cci = jkccind*cnxp_+xshift;
        cvalue_[i].re = cvalue_[cci].re;
        cvalue_[i].im = -cvalue_[cci].im;
      }
    }
    else
    {
      cvalue_[i].re = float(std*ranGen->rnorm01());
      cvalue_[i].im = float(std*ranGen->rnorm01());
    }
  }
}

void
FFTGrid::createRealGrid(bool add)
{
  istransformed_=false;
  add_ = add;
  if(add==true)
    nGrids_ += 1;
  createGrid();
}

void
FFTGrid::createComplexGrid()
{
  istransformed_  = true;
  nGrids_        += 1;
  createGrid();
}

void FFTGrid::createGrid()
{
  rvalue_         = static_cast<fftw_real*>(fftw_malloc(rsize_ * sizeof(fftw_real)));
  cvalue_         = reinterpret_cast<fftw_complex*>(rvalue_); //

  counterForGet_  = 0;
  counterForSet_  = 0;

 // LogKit::LogFormatted(LogKit::Error,"\nFFTGrid createGrid : nGrids = %d    maxGrids = %d\n",nGrids_,maxAllowedGrids_);
  if (nGrids_ > maxAllowedGrids_) {
    std::string text;
    text += "\n\nERROR in FFTGrid createGrid. You have allocated too many FFTGrids. The fix";
    text += "\nis to increase the nGrids variable calculated in Model::checkAvailableMemory().\n";
    text += "\nDo you REALLY need to allocate more grids?\n";
    text += "\nAre there no grids that can be released?\n";
    if(terminateOnMaxGrid_==true)
    {
      LogKit::LogFormatted(LogKit::Error, text);
      exit(1);
    }
    else if(nGrids_ == maxAllowedGrids_+1) {
      //NBNB-PAL: Commented out until memory handling is fixed in 4.0 release
      //TaskList::addTask("Crava needs more memory than expected. The results are still correct. \n Norwegian Computing Center would like to have a look at your project.");
    }
  }
  maxAllocatedGrids_ = std::max(nGrids_, maxAllocatedGrids_);

  FFTMemUse_ += rsize_ * sizeof(fftw_real);
  if(FFTMemUse_ > maxFFTMemUse_) {
    maxFFTMemUse_ = FFTMemUse_;
    LogKit::LogFormatted(LogKit::DebugLow,"\nNew FFT-grid memory peak (%2d): %10.2f MB\n",nGrids_, FFTMemUse_/(1024.f*1024.f));
  }



}

int
FFTGrid::getFillNumber(int i, int n, int np )
{
  //  for the series                 i = 0,1,2,3,4,5,6,7
  //  GetFillNumber(i, 5 , 8)  returns   0,1,2,3,4,4,1,0 (cut middle, i.e 3,2)
  //  GetFillNumber(i, 4 , 8)  returns   0,1,2,3,3,2,1,0 (copy)
  //  GetFillNumber(i, 3 , 8)  returns   0,1,2,2,1,1,1,0 (drag middle out, i.e. 1)

  int refi     =  0;
  int BeloWnp, AbovEn;

  if (i< np)
  {
    if (i<n)
      // then it is in the main cube
      refi  =  i;
    else
    {
      // Get cyclic extention
      BeloWnp  = np-i-1;
      AbovEn   = i-n+1;
      if( AbovEn < BeloWnp )
      {
        // Then the index is closer to end than start.
        refi=std::max(n-AbovEn,n/2);
      }else{
        // The it is closer to  start than the end
        refi=std::min(BeloWnp,n/2);
      }//endif
    }//endif
  }//endif
  else
  {
    // This happens when the index is larger than the padding size
    // this happens in some cases because rnxp_ is larger than nxp_
    // and the x cycle is of length rnxp_
    refi=IMISSING;
  }//endif
  return(refi);
}

int
FFTGrid::getZSimboxIndex(int k)
{
  int refk;

  if(k < (nz_+nzp_)/2)
    refk=k;
  else
    refk=k-nzp_;

  return refk;
}


float
FFTGrid::getDistToBoundary(int i, int n, int np )
{
  //  for the series                 i = 0,1,2,3,4,5,6,7
  //  GetFillNumber(i, 5 , 8)  returns   0,0,0,0,0,p,r,p  p is between 0 and 1, r is larger than 1
  //  GetFillNumber(i, 4 , 8)  returns   0,0,0,0,p,r,r,p  p is between 0 and 1, r's are larger than 1
  //  GetFillNumber(i, 3 , 8)  returns   0,0,0,p,r,r,r,p  p is between 0 and 1, r's are larger than 1

  float dist     =    0.0;
  float taperlength = 0.0;
  int   BeloWnp, AbovEn;

  if (i< np)
  {
    if (i<n)
      // then it is in the main cube
      dist  =  0.0;
    else
    {
      taperlength = static_cast<float>((std::min(n,np-n)/2.1)) ;// taper goes to zero  at taperlength
      BeloWnp  = np-i;
      AbovEn   = i-(n-1);
      if( AbovEn < BeloWnp )
      {
        // Then the index is closer to end than start.
        dist = static_cast<float>(AbovEn/taperlength);
      }
      else
      {
        // The it is closer to  start than the end (or identical to)
        dist = static_cast<float>(BeloWnp/taperlength);
      }//endif
    }//endif
  }//endif
  else
  {
    // This happens when the index is larger than the padding size
    // this happens in some cases because rnxp_ is larger than nxp_
    // and the x cycle is of length rnxp_
    dist=RMISSING;
  }//endif
  return(dist);
}


fftw_complex
FFTGrid::getNextComplex()
{
  assert(istransformed_==true);
  assert(counterForGet_ < csize_);
  counterForGet_  +=  1;
  if(counterForGet_ == csize_)
  {
    counterForGet_=0;
    return(cvalue_[csize_ - 1]);
  }
  else
    return(cvalue_[counterForGet_ - 1] );
}



float
FFTGrid::getNextReal()
{
  assert(istransformed_ == false);
  assert(counterForGet_ < rsize_);
  counterForGet_  +=  1;
  float r;
  if(counterForGet_==rsize_)
  {
    counterForGet_=0;
    r = static_cast<float>(rvalue_[rsize_-1]);
  }
  else
    r = static_cast<float>(rvalue_[counterForGet_-1]);
  return r;
}

float
FFTGrid::getRealValue(int i, int j, int k, bool extSimbox) const
{
  // when index is in simbox (or the extended simbox if extSimbox is true) it returns the grid value
  // else it returns RMISSING
  // i index in x direction
  // j index in y direction
  // k index in z direction
  float value;

  assert(istransformed_==false);

  bool  inSimbox   = (extSimbox ? ( (i < nxp_) && (j < nyp_) && (k < nzp_)):
    ((i < nx_) && (j < ny_) && (k < nz_)));
  bool  notMissing = ( (i > -1) && (j > -1) && (k > -1));

  if( inSimbox && notMissing )
  { // if index in simbox
    int index=i+rnxp_*j+k*rnxp_*nyp_;
    value = static_cast<float>(rvalue_[index]);
  }
  else
  {
    value = RMISSING;
  }

  return( value );
}

void
FFTGrid::getRealTrace(float * value, int i, int j)
{
  for(int k = 0 ; k < nz_ ; k++)
    value[k] = FFTGrid::getRealValue(i,j,k);
}

std::vector<float>
FFTGrid::getRealTrace2(int i, int j) const
{
  std::vector<float> value;
  for(int k = 0; k < nz_; k++)
    value.push_back(getRealValue(i,j,k));
  return value;
}

float
FFTGrid::getRealValueCyclic(int i, int j, int k)
{
  float value;
  if(i<0)
    i = nxp_+i;
  if(j<0)
    j = nyp_+j;
  if(k<0)
    k = nzp_+k;

  if(i<nxp_ && j<nyp_ && k<nzp_)
  {
    int index=i+rnxp_*j+k*rnxp_*nyp_;
    value = static_cast<float>(rvalue_[index]);
  }
  else
  {
    value = RMISSING;
  }

  return( value );

}

float
FFTGrid::getRealValueInterpolated(int i, int j, float kindex, bool extSimbox)
{
  // when index is in simbox (or the extended simbox if extSimbox is true) it returns the grid value
  // else it returns RMISSING
  // i index in x direction
  // j index in y direction
  // k index in z direction, float, should interpolate

  float value, val1, val2;

  int k1 = int(floor(kindex));
  val1 = getRealValue(i,j,k1,extSimbox);
  if(val1==RMISSING)
    return(RMISSING);
  int k2 = k1+1;
  val2 = getRealValue(i,j,k2,extSimbox);
  if(val2==RMISSING)
    return(val1);
  value = float(1.0-(kindex-k1))*val1+float(kindex-k1)*val2;
  return(value);

}

fftw_complex
FFTGrid::getComplexValue(int i, int j, int k, bool extSimbox) const
{
  // when index is in simbox (or the extended simbox if extSimbox is true) it returns the grid value
  // else it returns RMISSING
  // i index in x direction
  // j index in y direction
  // k index in z direction
  fftw_complex value;

  assert(istransformed_==true);

  bool  inSimbox   = (extSimbox ? ( (i < nxp_) && (j < nyp_) && (k < nzp_)):
    ((i < nx_) && (j < ny_) && (k < nz_)));
  bool  notMissing = ( (i > -1) && (j > -1) && (k > -1));


  if( inSimbox && notMissing )
  { // if index in simbox
    int index=i + j*cnxp_ + k*cnxp_*nyp_;
    value = fftw_complex (cvalue_[index]);
  }
  else
  {
    value.re = RMISSING;
    value.im = RMISSING;
  }

  return( value );
}


fftw_complex
FFTGrid::getFirstComplexValue()
{
  assert(istransformed_);
  fftw_complex value;

  setAccessMode(READ);
  counterForGet_=0;

  value = getNextComplex();

  counterForGet_=0;
  endAccess();

  return( value );
}

float
FFTGrid::getFirstRealValue()
{
  assert(istransformed_==false);
  float value = static_cast<float>(rvalue_[0]);
  return( value );
}

int
FFTGrid::setNextComplex(fftw_complex value)
{
  assert(istransformed_==true);
  assert(counterForSet_ < csize_);
  counterForSet_  +=  1;
  if(counterForSet_==csize_)
  {
    counterForSet_=0;
    cvalue_[csize_-1]=value;
  }
  else
    cvalue_[counterForSet_-1]=value;
  return(0);
}

int
FFTGrid::SetNextComplex(std::complex<double> & value)
{
  assert(istransformed_==true);
  assert(counterForSet_ < csize_);
  counterForSet_ += 1;
  if(counterForSet_==csize_)
  {
    counterForSet_=0;
    cvalue_[csize_-1].re = static_cast<float>(value.real());
    cvalue_[csize_-1].im = static_cast<float>(value.imag());
  }
  else {
    cvalue_[counterForSet_-1].re = static_cast<float>(value.real());
    cvalue_[counterForSet_-1].im = static_cast<float>(value.imag());
  }

  return(0);
}

int
FFTGrid::setNextReal(float value)
{
  assert(istransformed_== false);
  assert(counterForSet_ < rsize_);
  counterForSet_  +=  1;
  if(counterForSet_==rsize_)
  {
    counterForSet_=0;
    rvalue_[rsize_-1] = static_cast<fftw_real>(value);
  }
  else
    rvalue_[counterForSet_-1] = static_cast<fftw_real>(value);
  return(0);
}


int
FFTGrid::setRealValue(int i, int j ,int k, float  value, bool extSimbox)
{
  assert(istransformed_== false);

  bool  inSimbox   = (extSimbox ? ( (i < rnxp_) && (j < nyp_) && (k < nzp_)) : ((i < nx_) && (j < ny_) && (k < nz_)));
  bool  notMissing = ( (i > -1) && (j > -1) && (k > -1));

  if( inSimbox && notMissing )
  { // if index in simbox
    int index=i+rnxp_*j+k*rnxp_*nyp_;
    rvalue_[index] = value;
    return( 0 );
  }
  else
    return( 1 );
}

int FFTGrid::setRealTrace(int i, int j, float *value)
{
  int notok;
  for(int k=0;k<nz_;k++)
  {
    notok = setRealValue(i,j,k,value[k]);
    if(notok==1)
      return(1);
  }
  return(0);


}
int
FFTGrid::setComplexValue(int i, int j ,int k, fftw_complex value, bool extSimbox)
{
  assert(istransformed_== true);

  bool  inSimbox   = (extSimbox ? ( (i < nxp_) && (j < nyp_) && (k < nzp_)):
    ((i < nx_) && (j < ny_) && (k < nz_)));
  bool  notMissing = ( (i > -1) && (j > -1) && (k > -1));

  if( inSimbox && notMissing )
  { // if index in simbox
    int index=i + j*cnxp_ + k*cnxp_*nyp_;
    cvalue_[index] = value;
    return( 0 );
  }
  else
    return(1);
}

int
FFTGrid::square()
{
  int i;

  if(istransformed_==true)
  {
    for(i = 0;i < csize_; i++)
    {
      if ( cvalue_[i].re == RMISSING || cvalue_[i].im == RMISSING)
      {
        cvalue_[i].re = RMISSING;
        cvalue_[i].im = RMISSING;
      }
      else
      {
        cvalue_[i].re = cvalue_[i].re * cvalue_[i].re + cvalue_[i].im * cvalue_[i].im;
        cvalue_[i].im = 0.0;
      }
    } // i
  }
  else
  {
    for(i = 0;i < rsize_; i++)
    {
      if( rvalue_[i]== RMISSING)
      {
        rvalue_[i] = RMISSING;
      }
      else
      {
        rvalue_[i] = float( rvalue_[i]*rvalue_[i] );
      }

    }// i
  }

  return(0);
}

int
FFTGrid::expTransf()
{
  assert(istransformed_==false);
  int i;
  for(i = 0;i < rsize_; i++)
  {
    if( rvalue_[i]== RMISSING)
    {
      rvalue_[i] = RMISSING;
    }
    else
    {
      rvalue_[i] = float( exp(rvalue_[i]) );
    }

  }// i
  return(0);
}

int
FFTGrid::logTransf()
{
  assert(istransformed_==false);
  int i;
  for(i = 0;i < rsize_; i++)
  {
    if( rvalue_[i]== RMISSING ||  rvalue_[i] <= 0.0 )
    {
      rvalue_[i] = 0;
    }
    else
    {
      rvalue_[i] = float( log(rvalue_[i]) );
    }

  }// i
  return(0);
}

int
FFTGrid::collapseAndAdd(float * grid)
{
  assert(istransformed_==false);
  int   i,j;
  float value;


  for(j = 0; j < nyp_; j++)
    for(i=0;i<nxp_;i++)
    {
      value = rvalue_[i + j*rnxp_ ];
      grid[i + j*nxp_] += value ;
    }
    return(0);
}

void
FFTGrid::fftInPlace()
{
  // uses norm preserving transform for parameter and data
  // in case of correlation and cross correlation it
  // scale  by 1/N on the inverse such that it maps between
  // the correlation function and eigen values of the corresponding circular matrix

  time_t timestart, timeend;
  time(&timestart);

  assert(istransformed_==false);
  assert(cubetype_!= CTMISSING);

  if( cubetype_!= COVARIANCE )
    FFTGrid::multiplyByScalar(1.0f/sqrt(static_cast<float>(nxp_*nyp_*nzp_)));

  int flag;
  rfftwnd_plan plan;
  flag = FFTW_ESTIMATE | FFTW_IN_PLACE;
  plan= rfftw3d_create_plan(nzp_,nyp_,nxp_,FFTW_REAL_TO_COMPLEX,flag);
  rfftwnd_one_real_to_complex(plan,rvalue_,cvalue_);
  fftwnd_destroy_plan(plan);
  istransformed_=true;
  time(&timeend);
  LogKit::LogFormatted(LogKit::DebugLow,"\nFFT of grid type %d finished after %ld seconds \n",cubetype_, timeend-timestart);
}

void
FFTGrid::invFFTInPlace()
{
  // uses norm preserving transform for parameter and data
  // in case of correlation and cross correlation it
  // scale  by 1/N on the inverse such that it maps between
  // the correlation function and eigen values of the corresponding circular matrix

  time_t timestart, timeend;
  time(&timestart);

  assert(istransformed_==true);
  assert(cubetype_!= CTMISSING);

  float scale;
  int flag;
  rfftwnd_plan plan;
  if(cubetype_==COVARIANCE)
    scale=float( 1.0/(nxp_*nyp_*nzp_));
  else
    scale=float( 1.0/sqrt(float(nxp_*nyp_*nzp_)));

  flag = FFTW_ESTIMATE | FFTW_IN_PLACE;
  plan= rfftw3d_create_plan(nzp_,nyp_,nxp_,FFTW_COMPLEX_TO_REAL,flag);
  rfftwnd_one_complex_to_real(plan,cvalue_,rvalue_);
  fftwnd_destroy_plan(plan);
  istransformed_=false;

  FFTGrid::multiplyByScalar(scale);
  time(&timeend);
  LogKit::LogFormatted(LogKit::DebugLow,"\nInverse FFT of grid type %d finished after %ld seconds \n",cubetype_, timeend-timestart);
}

void
FFTGrid::realAbs()
{
  assert(istransformed_==true);
  int i;
  for(i=0;i<csize_;i++)
  {
    cvalue_[i].re = float (  sqrt( cvalue_[i].re * cvalue_[i].re ) );
    cvalue_[i].im = 0.0;
  }
}

void
FFTGrid::add(FFTGrid* fftGrid)
{
  assert(nxp_==fftGrid->getNxp());
  if(istransformed_==true)
  {
    int i;
    for(i=0;i<csize_;i++)
    {
      cvalue_[i].re += fftGrid->cvalue_[i].re;
      cvalue_[i].im += fftGrid->cvalue_[i].im;
    }
  }

  if(istransformed_==false)
  {
    int i;
    for(i=0;i < rsize_;i++)
    {
      rvalue_[i] += fftGrid->rvalue_[i];
    }
  }
}
void
FFTGrid::addScalar(float scalar)
{
  // Only addition of scalar in real domain
  assert(istransformed_==false);
  int i;
  for(i=0;i < rsize_;i++)
  {
    rvalue_[i] += scalar;
  }
}

void
FFTGrid::subtract(FFTGrid* fftGrid)
{
  assert(nxp_==fftGrid->getNxp());
  if(istransformed_==true)
  {
    int i;
    for(i=0;i<csize_;i++)
    {
      cvalue_[i].re -= fftGrid->cvalue_[i].re;
      cvalue_[i].im -= fftGrid->cvalue_[i].im;
    }
  }

  if(istransformed_==false)
  {
    int i;
    for(i=0;i < rsize_;i++)
    {
      rvalue_[i] -= fftGrid->rvalue_[i];
    }
  }
}
void
FFTGrid::changeSign()
{
  if(istransformed_==true)
  {
    int i;
    for(i=0;i<csize_;i++)
    {
      cvalue_[i].re = -cvalue_[i].re;
      cvalue_[i].im = -cvalue_[i].im;
    }
  }

  if(istransformed_==false)
  {
    int i;
    for(i=0;i < rsize_;i++)
    {
      rvalue_[i] = -rvalue_[i];
    }
  }
}
void
FFTGrid::multiply(FFTGrid* fftGrid)
{
  assert(nxp_==fftGrid->getNxp());
  if(istransformed_==true)
  {
    int i;
    for(i=0;i<csize_;i++)
    {
      fftw_complex tmp = cvalue_[i];
      cvalue_[i].re = fftGrid->cvalue_[i].re*tmp.re - fftGrid->cvalue_[i].im*tmp.im;
      cvalue_[i].im = fftGrid->cvalue_[i].im*tmp.re + fftGrid->cvalue_[i].re*tmp.im;
    }
  }

  if(istransformed_==false)
  {
    int i;
    for(i=0;i < rsize_;i++)
    {
      rvalue_[i] *= fftGrid->rvalue_[i];
    }
  }
}

void
FFTGrid::conjugate()
{
  assert(istransformed_==true);
  for(int i=0;i<csize_;i++)
  {
    cvalue_[i].im = -cvalue_[i].im;
  }
}

void
FFTGrid::multiplyByScalar(float scalar)
{
  assert(istransformed_==false);
  for(int i=0;i<rsize_;i++)
  {
    rvalue_[i]*=scalar;
  }
}

fftw_complex*
FFTGrid::fft1DzInPlace(fftw_real*  in, int nzp)
{
  // in is over vritten by out
  // not norm preservingtransform ifft(fft(funk))=N*funk

  int flag;

  rfftwnd_plan plan;
  fftw_complex* out;
  out = reinterpret_cast<fftw_complex*>(in);

  flag    = FFTW_ESTIMATE | FFTW_IN_PLACE;
  plan    = rfftwnd_create_plan(1, &nzp ,FFTW_REAL_TO_COMPLEX,flag);
  rfftwnd_one_real_to_complex(plan,in ,out);
  fftwnd_destroy_plan(plan);

  return out;
}

fftw_real*
FFTGrid::invFFT1DzInPlace(fftw_complex* in, int nzp)
{
  // in is over vritten by out
  // not norm preserving transform  ifft(fft(funk))=N*funk

  int flag;
  rfftwnd_plan plan;
  fftw_real*  out;
  out = reinterpret_cast<fftw_real*>(in);

  flag = FFTW_ESTIMATE | FFTW_IN_PLACE;
  plan= rfftwnd_create_plan(1,&nzp,FFTW_COMPLEX_TO_REAL,flag);
  rfftwnd_one_complex_to_real(plan,in,out);
  fftwnd_destroy_plan(plan);
  return out;
}

bool
FFTGrid::consistentSize(int nx,int ny, int nz, int nxp, int nyp, int nzp)
{
  bool consistent = (nx==nx_);
  if( consistent ) consistent = (ny==ny_);
  if( consistent ) consistent = (nz==nz_);
  if( consistent ) consistent = (nxp==nxp_);
  if( consistent ) consistent = (nyp==nyp_);
  if( consistent ) consistent = (nzp==nzp_);
  return consistent;
}


void
FFTGrid::writeFile(const std::string              & fName,
                   const std::string              & subDir,
                   const Simbox                   * simbox,
                   const std::string                label,
                   const float                      z0,
                   const GridMapping              * depthMap,
                   const GridMapping              * timeMap,
                   const TraceHeaderFormat        & thf,
                   bool                             padding,
                   bool                             scientific_format,
                   const std::vector<std::string> & headerText)
{
  std::string fileName = IO::makeFullFileName(subDir, fName);

  if (formatFlag_ > 0) //Output format specified.
  {
    if ((domainFlag_ & IO::TIMEDOMAIN) > 0) {
      if (timeMap == NULL) { //No resampling of storm
        if ((formatFlag_ & IO::STORM) > 0)
          FFTGrid::writeStormFile(fileName, simbox, false,padding);
        if ((formatFlag_ & IO::ASCII) > 0)
          FFTGrid::writeStormFile(fileName, simbox, true,padding, false, scientific_format);
      }
      else {
        FFTGrid::writeResampledStormCube(timeMap, fileName, simbox, formatFlag_);
      }

      //SEGY, SGRI CRAVA are never resampled in time.
      if ((formatFlag_ & IO::SEGY) >0)
        FFTGrid::writeSegyFile(fileName, simbox, z0, thf, headerText);
      if ((formatFlag_ & IO::SGRI) >0)
        FFTGrid::writeSgriFile(fileName, simbox, label);
      if ((formatFlag_ & IO::CRAVA) >0)
        FFTGrid::writeCravaFile(fileName, simbox);
    }

    if (depthMap != NULL && (domainFlag_ & IO::DEPTHDOMAIN) > 0) { //Writing in depth. Currently, only stormfiles are written in depth.
      std::string depthName = fileName+"_Depth";
      if (depthMap->getMapping() == NULL) {
        if (depthMap->getSimbox() == NULL) {
          LogKit::LogFormatted(LogKit::Warning,
            "WARNING: Depth interval lacking when trying to write %s. Write cancelled.\n",depthName.c_str());
          return;
        }
        if ((formatFlag_ & IO::STORM) > 0)
          FFTGrid::writeStormFile(depthName, depthMap->getSimbox(), false);
        if ((formatFlag_ & IO::ASCII) > 0)
          FFTGrid::writeStormFile(depthName, depthMap->getSimbox(), true);
        if ((formatFlag_ & IO::SEGY) >0)
          makeDepthCubeForSegy(depthMap->getSimbox(),depthName);
      }
      else
      {
        if (depthMap->getSimbox() == NULL) {
          LogKit::LogFormatted(LogKit::Warning,
            "WARNING: Depth mapping incomplete when trying to write %s. Write cancelled.\n",depthName.c_str());
          return;
        }
        // Writes also cubes in depth if required
        LogKit::LogFormatted(LogKit::Low,"Resample cube and write file "+depthName+".segy...");
        FFTGrid::writeResampledStormCube(depthMap, depthName, simbox, formatFlag_);
        LogKit::LogFormatted(LogKit::Low,"done\n");
      }
    }
  }
}

void
FFTGrid::writeStormFile(const std::string & fileName,
                        const Simbox      * simbox,
                        bool                ascii,
                        bool                padding,
                        bool                flat,
                        bool                scientific_format)
{
  int nx, ny, nz;
  if(padding == true)
  {
    nx = nxp_;
    ny = nyp_;
    nz = nzp_;
  }
  else
  {
    nx = nx_;
    ny = ny_;
    nz = nz_;
  }

  std::string gfName;
  std::string header = simbox->getStormHeader(cubetype_, nx, ny, nz, flat, ascii);
  int i, j, k;
  float value;

  if(ascii == false) {
    gfName = fileName + IO::SuffixStormBinary();
    LogKit::LogFormatted(LogKit::Low,"\nWriting STORM binary file "+gfName+"...");
    std::ofstream binFile;
    NRLib::OpenWrite(binFile, gfName, std::ios::out | std::ios::binary);
    binFile << header;
    for(k=0;k<nz;k++)
      for(j=0;j<ny;j++)
        for(i=0;i<nx;i++)
        {
          value = getRealValue(i,j,k,true);
          NRLib::WriteBinaryFloat(binFile, value);
        }
    binFile << "0\n";
    binFile.close();
  }
  else {
    gfName = fileName + IO::SuffixGeneralData();
    std::ofstream file;
    NRLib::OpenWrite(file, gfName);
    LogKit::LogFormatted(LogKit::Low,"\nWriting STORM ascii file "+gfName+"...");
    file << header;
    if (scientific_format){
      file << std::scientific;
    }
    else{
      file << std::fixed << std::setprecision(6);
    }
    for(k=0;k<nz;k++)
      for(j=0;j<ny;j++) {
        for(i=0;i<nx-1;i++) {
          value = getRealValue(i,j,k,true);
          file << value << " ";              // Rearrangement to avoid trailing blanks
        }
        value = getRealValue(nx-1,j,k,true); // Rearrangement to avoid trailing blanks
        file << value << "\n";
      }
    file << "0\n";
  }

  LogKit::LogFormatted(LogKit::Low,"done\n");
}


int
FFTGrid::writeSegyFile(const std::string              & fileName,
                       const Simbox                   * simbox,
                       float                            z0,
                       const TraceHeaderFormat        & thf,
                       const std::vector<std::string> & headerText)
{
  //  long int timestart, timeend;
  //  time(&timestart);

  std::string gfName = fileName + IO::SuffixSegy();
  //SegY * segy = new SegY(gfName, simbox);
  TextualHeader header;
  if(headerText.size() == 0)
    header = TextualHeader::standardHeader();
  else {
    for(size_t i=0;i<headerText.size();i++) {
      header.SetLine(i+1, headerText[i]);
    }
  }

  float dz = float(floor(simbox->getdz()+0.5));
  if(dz==0)
    dz = 1.0;
  int segynz;
  double zmin,zmax;
  simbox->getMinMaxZ(zmin,zmax);
  segynz = int(ceil((zmax-z0)/dz));
  SegY *segy = new SegY(gfName.c_str(),z0,segynz,dz,header, thf);
  SegyGeometry * geometry = new SegyGeometry(simbox->getx0(), simbox->gety0(), simbox->getdx(), simbox->getdy(),
                                             simbox->getnx(), simbox->getny(),simbox->getIL0(), simbox->getXL0(),
                                             simbox->getILStepX(), simbox->getILStepY(),
                                             simbox->getXLStepX(), simbox->getXLStepY(),
                                             simbox->getAngle());
  segy->SetGeometry(geometry);
  delete geometry; //Call above takes a copy.
  LogKit::LogFormatted(LogKit::Low,"\nWriting SEGY file "+gfName+"...");

  int i,j,k;
  double x,y,z;
  std::vector<float> trace(segynz);//Maximum amount of data needed.

  // std::cout << "\nz0, dz = " << z0 << " " << dz << std::endl;

  for(j=0;j<simbox->getny();j++)
  {
    for(i=0;i<simbox->getnx();i++)
    {
      simbox->getCoord(i, j, 0, x, y, z);
      z = simbox->getTop(x,y);

      if(z == RMISSING || z == WELLMISSING)
      {
        //printf("Missing trace.\n");
        for(k=0;k<segynz;k++)
          trace[k] = 0;
      }
      else
      {
        double gdz       = simbox->getdz()*simbox->getRelThick(i,j);
        int    firstData = static_cast<int>(floor(0.5+(z-z0)/dz));
        int    endData   = static_cast<int>(floor(0.5+((z-z0)+nz_*gdz)/dz));

        //xxxxx
        /*
        std::cout << "OUTPUT: z = gdz, firstData, endData = " << z << " " << gdz << " " << firstData << " " << endData << std::endl;
        std::cout << "x , y = " << x << " , " << y << std::endl;
        */

        if(endData > segynz)
        {
          printf("Internal warning: SEGY-grid too small (%d, %d needed). Truncating data.\n", nz_, endData);
          endData = segynz;
        }
        for(k=0;k<firstData;k++)
          trace[k] = 0;
//          trace[k] = -1e35; //NBNB-Frode: Norsar-hack
        for(;k<endData;k++) {
          trace[k] = this->getRegularZInterpolatedRealValue(i,j,z0,dz,k,z,gdz);

        }
        for(;k<segynz;k++)
          trace[k] = 0;
//          trace[k] = -1e35; //NBNB-Frode: Norsar-hack
        float xx = static_cast<float>(x);
        float yy = static_cast<float>(y);
        segy->StoreTrace(xx, yy, trace, NULL);
      }
    }
  }

  segy->WriteAllTracesToFile();

  delete segy; //Closes file.
  // delete [] value;
  LogKit::LogFormatted(LogKit::Low,"done\n");
  //  time(&timeend);
  //printf("\n Write SEGY was performed in %ld seconds.\n",timeend-timestart);
  return(0);
}


void
FFTGrid::writeResampledStormCube(const GridMapping * gridmapping,
                                 const std::string & fileName,
                                 const Simbox      * simbox,
                                 const int           format)
{
  // simbox is related to the cube we resample from. gridmapping contains simbox for the cube we resample to.

  float time, kindex;
  StormContGrid *mapping = gridmapping->getMapping();
  StormContGrid *outgrid = new StormContGrid(*mapping);

  double x,y;
  int nz = static_cast<int>(mapping->GetNK());
  for(int i=0;i<nx_;i++)
  {
    for(int j=0;j<ny_;j++)
    {
      simbox->getXYCoord(i,j,x,y);
      for(int k=0;k<nz;k++)
      {
        time = (*mapping)(i,j,k);
        kindex = float((time - static_cast<float>(simbox->getTop(x,y)))/simbox->getdz());
        float value = getRealValueInterpolated(i,j,kindex);
        (*outgrid)(i,j,k) = value;
      }
    }
  }

  std::string gfName;
  std::string header;
  if ((format & IO::ASCII) > 0) // ASCII
  {
    gfName = fileName + IO::SuffixGeneralData();
    header = gridmapping->getSimbox()->getStormHeader(FFTGrid::PARAMETER,nx_,ny_,nz, 0, 1);
    outgrid->SetFormat(StormContGrid::STORM_ASCII);
    outgrid->WriteToFile(gfName,header);
  }

  if ((format & IO::STORM) > 0)
  {
    gfName =  fileName + IO::SuffixStormBinary();
    header = gridmapping->getSimbox()->getStormHeader(FFTGrid::PARAMETER,nx_,ny_,nz, 0, 0);
    outgrid->SetFormat(StormContGrid::STORM_BINARY);
    outgrid->WriteToFile(gfName,header);
  }
  if((formatFlag_ & IO::SEGY) > 0)
  {
    gfName =  fileName + IO::SuffixSegy();
    writeSegyFromStorm(gridmapping->getSimbox(), outgrid,gfName);
  }
  delete outgrid;
}

int
FFTGrid::writeSgriFile(const std::string & fileName, const Simbox *simbox, const std::string label)
{
  double vertScale = 0.001;
  double horScale  = 0.001;
  std::string fName = fileName + IO::SuffixSgriHeader();

  std::ofstream headerFile;
  NRLib::OpenWrite(headerFile, fName);

  LogKit::LogFormatted(LogKit::Low,"\nWriting SGRI header file "+fName+"...");

  headerFile << "NORSAR General Grid Format v1.0\n";
  headerFile << "3\n";
  headerFile << "X (km)\n";
  headerFile << "Y (km)\n";
  headerFile << "T (s)\n";
  headerFile << "FFT-grid\n";
  headerFile << "1\n";
  headerFile << label << std::endl;
  headerFile << "1 1 1\n";

  double zMin, zMax;
  simbox->getMinMaxZ(zMin, zMax);
/*  float dz = static_cast<float> (floor(simbox->getdz()+0.5)); //To have the same sampling as in SegY
  if (dz == 0.0)
    dz = 1.0; */
  float dz = static_cast<float> (simbox->getdz());
  int nz = static_cast<int> (ceil((zMax - zMin)/dz));
  int ny = simbox->getny();
  int nx = simbox->getnx();
  headerFile << nx << " " << ny << " " << nz << std::endl;
  headerFile << std::setprecision(10);
  headerFile << simbox->getdx()*horScale << " " << simbox->getdy()*horScale << " " << dz*vertScale << std::endl;
  double x0 = simbox->getx0() + 0.5 * simbox->getdx();
  double y0 = simbox->gety0() + 0.5 * simbox->getdy();
  double z0 = zMin + 0.5 * dz;
  headerFile << x0*horScale << " " << y0*horScale << " " << z0*vertScale << std::endl;
  headerFile << simbox->getAngle() << " 0\n";
  headerFile << RMISSING << std::endl;

  fName = fileName + IO::SuffixSgri();
  headerFile << fName << std::endl;
  headerFile << "0\n";

  std::ofstream binFile;
  NRLib::OpenWrite(binFile, fName, std::ios::out | std::ios::binary);

  int i,j,k;
  double x, y, z, zTop, zBot;
  float value;
  for(k=0;k<nz;k++) {
    for (j=0; j<ny; j++) {
      for (i=0; i<nx; i++) {
        simbox->getXYCoord(i, j, x, y);
        zTop = simbox->getTop(x, y);
        zBot = simbox->getBot(x, y);
        z = zMin + k*dz;
        if (z < zTop || z > zBot)
          value = RMISSING;
        else {
           int simboxK = static_cast<int> ((z - zTop)/ (simbox->getdz()*simbox->getRelThick(i,j)) + 0.5);
          value = getRealValue(i,j,simboxK);        }
#ifndef BIGENDIAN
        NRLib::WriteBinaryFloat(binFile, value);
#else
        NRLib::WriteBinaryFloat(binFile, value, END_LITTLE_ENDIAN);
#endif
      }
    }
  }
  return(0);
}


void
FFTGrid::writeCravaFile(const std::string & fileName, const Simbox * simbox)
{
  try {
    std::ofstream binFile;
    std::string fName = fileName + IO::SuffixCrava();
    NRLib::OpenWrite(binFile, fName, std::ios::out | std::ios::binary);

    std::string fileType = "crava_fftgrid_binary";
    binFile << fileType << "\n";

    NRLib::WriteBinaryDouble(binFile, simbox->getx0());
    NRLib::WriteBinaryDouble(binFile, simbox->gety0());
    NRLib::WriteBinaryDouble(binFile, simbox->getdx());
    NRLib::WriteBinaryDouble(binFile, simbox->getdy());
    NRLib::WriteBinaryInt(binFile, simbox->getnx());
    NRLib::WriteBinaryInt(binFile, simbox->getny());
    NRLib::WriteBinaryDouble(binFile, simbox->getIL0());
    NRLib::WriteBinaryDouble(binFile, simbox->getXL0());
    NRLib::WriteBinaryDouble(binFile, simbox->getILStepX());
    NRLib::WriteBinaryDouble(binFile, simbox->getILStepY());
    NRLib::WriteBinaryDouble(binFile, simbox->getXLStepX());
    NRLib::WriteBinaryDouble(binFile, simbox->getXLStepY());
    NRLib::WriteBinaryDouble(binFile, simbox->getAngle());
    NRLib::WriteBinaryInt(binFile, rnxp_);
    NRLib::WriteBinaryInt(binFile, nyp_);
    NRLib::WriteBinaryInt(binFile, nzp_);
    for(int i=0;i<rsize_;i++)
      NRLib::WriteBinaryFloat(binFile, rvalue_[i]);

    binFile.close();
  }
  catch (NRLib::Exception & e) {
    std::string message = "Error: "+std::string(e.what())+"\n";
    LogKit::LogMessage(LogKit::Error, message);
  }
}


void
FFTGrid::readCravaFile(const std::string & fileName, std::string & errText, bool nopadding)
{
  std::string error;
  try {
    std::ifstream binFile;
    NRLib::OpenRead(binFile, fileName, std::ios::in | std::ios::binary);

    std::string fileType;
    getline(binFile,fileType);

    double dummy = NRLib::ReadBinaryDouble(binFile);
    dummy = NRLib::ReadBinaryDouble(binFile);
    dummy = NRLib::ReadBinaryDouble(binFile);
    dummy = NRLib::ReadBinaryDouble(binFile);
    dummy = NRLib::ReadBinaryInt(binFile);
    dummy = NRLib::ReadBinaryInt(binFile);
    dummy = NRLib::ReadBinaryDouble(binFile);
    dummy = NRLib::ReadBinaryDouble(binFile);
    dummy = NRLib::ReadBinaryDouble(binFile);
    dummy = NRLib::ReadBinaryDouble(binFile);
    dummy = NRLib::ReadBinaryDouble(binFile);
    dummy = NRLib::ReadBinaryDouble(binFile);
    dummy = NRLib::ReadBinaryDouble(binFile);
    int rnxp = NRLib::ReadBinaryInt(binFile);
    int nyp  = NRLib::ReadBinaryInt(binFile);
    int nzp  = NRLib::ReadBinaryInt(binFile);

    if(rnxp != rnxp_ || nyp != nyp_ || nzp != nzp_) {
      LogKit::LogFormatted(LogKit::Low,"\n\nERROR: The grid has different dimensions than the model grid. Check the padding settings");
      LogKit::LogFormatted(LogKit::Low,"\n                rnxp   nyp   nzp");
      LogKit::LogFormatted(LogKit::Low,"\n--------------------------------");
      LogKit::LogFormatted(LogKit::Low,"\nModel grid  :   %4d  %4d  %4d",rnxp_,nyp_,nzp_);
      LogKit::LogFormatted(LogKit::Low,"\nGrid on file:   %4d  %4d  %4d\n",rnxp ,nyp ,nzp );
      binFile.close();
      throw(NRLib::Exception("Grid dimension is wrong for file '"+fileName+"'."));
    }
    createRealGrid(!nopadding);
    add_ = !nopadding;
    int i;
    for(i=0;i<rsize_;i++)
      rvalue_[i] = NRLib::ReadBinaryFloat(binFile);

    binFile.close();
  }
  catch (NRLib::Exception & e) {
    error = std::string("Error: ") + e.what() + "\n";
  }
  errText += error;
}


float
FFTGrid::getRegularZInterpolatedRealValue(int i, int j, double z0Reg,
                                          double dzReg, int kReg,
                                          double z0Grid, double dzGrid)
{
  float z     = static_cast<float> (z0Reg+dzReg*kReg);
  float t     = static_cast<float> ((z-z0Grid)/dzGrid);
  int   kGrid = int(t);

  t -= kGrid;

  if(kGrid<0) {
    if(kGrid<-1)
      return(RMISSING);
    else {
      kGrid = 0;
      t = 0;
    }
  }
  else if(kGrid>nz_-2) {
    if(kGrid > nz_-1)
      return(RMISSING);
    else {
      kGrid = nz_-2;
      t = 1;
    }
  }

  float v1 = getRealValue(i,j,kGrid);
  float v2 = getRealValue(i,j,kGrid+1);
  float v  = (1-t)*v1+t*v2;

  /*
  std::cout  << std::fixed
             << std::setprecision(2) << std::setw(10) << z0Grid + kGrid*dzGrid
             << std::setprecision(6) << std::setw(10) << getRealValue(i,j,kGrid) << "    "
             << std::setprecision(2) << std::setw(10) << z0Grid + (kGrid+1)*dzGrid
             << std::setprecision(6) << std::setw(10) << getRealValue(i,j,kGrid+1)
             << std::setprecision(2) << std::setw(20) << z
             << std::setprecision(6) << std::setw(20) << v << std::endl;
  */


  return(v);
}

void
FFTGrid::writeAsciiFile(const std::string & fileName)
{
  std::ofstream file;
  NRLib::OpenWrite(file, fileName);
  LogKit::LogFormatted(LogKit::Low,"\nWriting ASCII file "+fileName+"...");
  int i, j, k;
  float value;
  for(k=0;k<nzp_;k++)
    for(j=0;j<nyp_;j++)
      for(i=0;i<rnxp_;i++)
      {
        value = getNextReal();
        file << value << "\n";
      }
  file.close();
  LogKit::LogFormatted(LogKit::Low,"done.\n");
}

void
FFTGrid::writeAsciiRaw(const std::string & fileName)
{
  std::ofstream file;
  NRLib::OpenWrite(file, fileName);
  LogKit::LogFormatted(LogKit::Low,"\nWriting ASCII file "+fileName+"...");
  int i, j, k;
  float value;
  for(k=0;k<nzp_;k++)
    for(j=0;j<nyp_;j++)
      for(i=0;i<rnxp_;i++)
      {
        value = getNextReal();
        file << i << " " << j << " " << k << " " << value << " ";
      }
  file.close();
  LogKit::LogFormatted(LogKit::Low,"done.\n");
}


void
FFTGrid::interpolateSeismic(float energyTreshold)
{
  assert(cubetype_ == DATA);
  int i, j, k, index = 0;
  short int * flags = new short int[nx_*ny_];
  int curFlag, flag = 0; //Flag rules: bit 0 = this trace bad, bit 1 = any prev. bad
  int imin = nx_;
  int imax = 0;
  int jmin = ny_;
  int jmax = 0;
  float value, energy, totalEnergy = 0;;
  float * energyMap = new float[nx_*ny_];
  for(j=0;j<ny_;j++)
    for(i=0;i<nx_;i++)
    {
      energy = 0;
      for(k=0;k<nz_;k++) {
        value = getRealValue(i, j, k);
        energy += value*value;
      }
      energyMap[index] = energy;
      totalEnergy += energy;
      index++;
    }

    index = 0;
    float energyLimit = energyTreshold*totalEnergy/float(nx_*ny_);
    int nInter = 0;    //#traces interpolated.
    int nInter0 = 0;   //#traces interpolated where there was no response at all.
    for(j=0;j<ny_;j++)
      for(i=0;i<nx_;i++)
      {
        curFlag = 0;
        if(energyMap[index] <= energyLimit) {//Values in this trace are bogus, interpolate.
          curFlag = 1;
          nInter++;
          if(energyMap[index] == 0.0f)
            nInter0++;
        }
        flags[index] = short(flag+curFlag);
        if(curFlag == 1)
          flag = 2;
        else
        {
          if(i < imin)
            imin = i;
          if(i > imax)
            imax = i;
          if(j < jmin)
            jmin = j;
          if(j > jmax)
            jmax = j;
        }
        index++;
      }

      LogKit::LogFormatted(LogKit::Low,"\n%d of %d traces (%d with zero response)",
        nInter, nx_*ny_, nInter0);
      int curIndex = 0;
      for(j=0;j<ny_;j++)
        for(i=0;i<nx_;i++)
        {
          if((flags[curIndex] % 2) == 1)
          {
            if((interpolateTrace(curIndex, flags, i, j) % 2) == 0)
            {
              index = curIndex-1;
              while(index >= 0 && flags[index] > 1)
              {
                if((flags[index] % 2) == 1)
                  interpolateTrace(index, flags, (index % nx_), index/nx_);
                index--;
              }
              if(index < 0)
                index = 0;
              assert(flags[index] < 2);
              index++;
              while(flags[index-1] == 0 && index <= curIndex)
              {
                if(flags[index] > 1)
                  flags[index] -= 2;
                index++;
              }
            }
          }
          curIndex++;
        }
        extrapolateSeismic(imin, imax, jmin, jmax);
        delete [] energyMap;
        delete [] flags;
}


int
FFTGrid::interpolateTrace(int index, short int * flags, int i, int j)
{
  int  k, nt = 0;
  bool left = (i > 0 && (flags[index-1] % 2) == 0);
  bool right = (i < nx_-1 && (flags[index+1] % 2) == 0);
  bool up = (j > 0 && (flags[index-nx_] % 2) == 0);
  bool down = (j < ny_-1 && (flags[index+nx_] % 2) == 0);
  if(left == true) nt++;
  if(right == true) nt++;
  if(up == true) nt++;
  if(down == true) nt++;
  if(nt > 1)
  {
    float * mean = new float[nzp_];
    for(k=0;k<nzp_;k++)
    {
      if(left == true)
        mean[k] = getRealValue(i-1, j, k,true);
      else
        mean[k] = 0;
    }
    if(right == true)
      for(k=0;k<nzp_;k++)
        mean[k] += getRealValue(i+1,j,k,true);
    if(up == true)
      for(k=0;k<nzp_;k++)
        mean[k] += getRealValue(i,j-1,k,true);
    if(down == true)
      for(k=0;k<nzp_;k++)
        mean[k] += getRealValue(i,j+1,k,true);
    for(k=0;k<nzp_;k++)
      setRealValue(i, j, k, mean[k]/float(nt),true);
    delete [] mean;
    assert((flags[index] % 2) == 1);
    flags[index]--;
  }
  return(flags[index]);
}


void
FFTGrid::extrapolateSeismic(int imin, int imax, int jmin, int jmax)
{
  int i, j, k, refi, refj;
  float value, distx, disty, distz, mult;
  for(j=0;j<nyp_;j++)
  {
    refj = getYSimboxIndex(j);
    if(refj < jmin)
      refj = jmin;
    else if(refj > jmax)
      refj = jmax;
    for(i=0;i<nxp_;i++)
    {
      if(i < imin || i > imax || j < jmin || j > jmax)
      {
        refi = getXSimboxIndex(i);
        if(refi < imin)
          refi = imin;
        else if(refi > imax)
          refi = imax;
        for(k=0;k<nzp_;k++)
        {
          value = getRealValue(refi, refj, k, true);
          distx  = getDistToBoundary(i,nx_,nxp_);
          disty  = getDistToBoundary(j,ny_,nyp_);
          distz  = getDistToBoundary(k,nz_,nzp_);
          mult   = float(pow(std::max<double>(1.0-distx*distx-disty*disty-distz*distz,0.0),3));
          setRealValue(i, j, k, mult*value, true);
        }
      }
    }
  }
}




void
FFTGrid::checkNaN()
{
  /*
  #ifndef UNIX_ELLER_NOE_ANNET
  int i;
  if(istransformed_)
  {
  for(i=0;i<csize_;i++)
  if(_isnan(cvalue_[i].re) ||
  _isnan(cvalue_[i].im))
  break;
  if(i == csize_)
  i = -1;
  }
  else
  {
  for(i=0;i<rsize_;i++)
  if(_isnan(rvalue_[i]))
  break;
  if(i == rsize_)
  i = -1;
  }
  assert(i == -1);
  #endif
  */
}

void FFTGrid::writeSegyFromStorm(Simbox * simbox, StormContGrid *data, std::string fileName)
{
  int i,k,j;
  TextualHeader header = TextualHeader::standardHeader();
  int nx = static_cast<int>(data->GetNI());
  int ny = static_cast<int>(data->GetNJ());

  SegyGeometry geometry (simbox->getx0(), simbox->gety0(), simbox->getdx(), simbox->getdy(),
                         simbox->getnx(), simbox->getny(),simbox->getIL0(), simbox->getXL0(),
                         simbox->getILStepX(), simbox->getILStepY(),
                         simbox->getXLStepX(), simbox->getXLStepY(),
                         simbox->getAngle());

  //  SegyGeometry geometry(data->GetXMin(),data->GetYMin(),data->GetDX(),data->GetDY(),
  //                      nx,ny,data->GetAngle());


  float dz = float(floor((data->GetLZ()/data->GetNK())));
  //int nz = int(data->GetZMax()/dz);
  //float z0 = float(data->GetZMin());
  float z0 = 0.0;
  int nz = int(ceil((data->GetZMax())/dz));
  SegY segyout(fileName,0,nz,dz,header);
  segyout.SetGeometry(&geometry);

  std::vector<float> datavec;
  datavec.resize(nz);
  float x, y, xt, yt, z;
  for(j=0;j<ny;j++)
    for(i=0;i<nx;i++)
    {
      xt = float((i+0.5)*geometry.GetDx());
      yt = float((j+0.5)*geometry.GetDy());
      x = float(geometry.GetX0()+xt*geometry.GetCosRot()-yt*geometry.GetSinRot());
      y = float(geometry.GetY0()+yt*geometry.GetCosRot()+xt*geometry.GetSinRot());

      double zbot= data->GetBotSurface().GetZ(x,y);
      double ztop = data->GetTopSurface().GetZ(x,y);
      int    firstData = static_cast<int>(floor((ztop)/dz));
      int    endData   = static_cast<int>(floor((zbot)/dz));

      if(endData > nz)
      {
        printf("Internal warning: SEGY-grid too small (%d, %d needed). Truncating data.\n", nz, endData);
        endData = nz;
      }
      for(k=0;k<firstData;k++)
      {
        datavec[k] = 0.0;
      }

      for(k=firstData;k<endData;k++)
      {
         z = z0+k*dz;
        datavec[k] = float(data->GetValueZInterpolated(x,y,z));
      }
      for(k=endData;k<nz;k++)
      {
        datavec[k] = 0.0;
      }
      segyout.StoreTrace(x,y,datavec,NULL);
    }

  segyout.WriteAllTracesToFile();

}

void FFTGrid::makeDepthCubeForSegy(Simbox *simbox,const std::string & fileName)
{
  StormContGrid * stormcube = new StormContGrid((*simbox),nx_,ny_,nz_);

  int i,j,k;
  for(k=0;k<nz_;k++)
    for(j=0;j<ny_;j++)
      for(i=0;i<nx_;i++)
        (*stormcube)(i,j,k) = getRealValue(i,j,k,true);

  std::string fullFileName = fileName + IO::SuffixSegy();
  FFTGrid::writeSegyFromStorm(simbox, stormcube, fullFileName);
}

int FFTGrid::findClosestFactorableNumber(int leastint)
{
  int i,j,k,l,m,n;
  int factor   =       1;

  int maxant2    = static_cast<int>(ceil(static_cast<double>(log(static_cast<float>(leastint))) / log(2.0f) ));
  int maxant3    = static_cast<int>(ceil(static_cast<double>(log(static_cast<float>(leastint))) / log(3.0f) ));
  int maxant5    = static_cast<int>(ceil(static_cast<double>(log(static_cast<float>(leastint))) / log(5.0f) ));
  int maxant7    = static_cast<int>(ceil(static_cast<double>(log(static_cast<float>(leastint))) / log(7.0f) ));
  int maxant11   = 0;
  int maxant13   = 0;
  int closestprod= static_cast<int>(pow(2.0f,maxant2));

  /* kan forbedres ved aa trekke fra i endepunktene.i for lokkene*/
  for(i=0;i<maxant2+1;i++)
    for(j=0;j<maxant3+1;j++)
      for(k=0;k<maxant5+1;k++)
        for(l=0;l<maxant7+1;l++)
          for(m=0;m<maxant11+1;m++)
            for(n=maxant11;n<maxant13+1;n++)
            {
              factor = static_cast<int>(pow(2.0f,i)*pow(3.0f,j)*pow(5.0f,k)*
                pow(7.0f,l)*pow(11.0f,m)*pow(13.0f,n));
              if ((factor >=  leastint) &&  (factor <  closestprod))
              {
                closestprod=factor;
              }
            }
            return closestprod;
}


int FFTGrid::formatFlag_        = 0;
int FFTGrid::domainFlag_        = IO::TIMEDOMAIN;
int FFTGrid::maxAllowedGrids_   = 1;   // One grid is allocated and deallocated before memory check.
int FFTGrid::maxAllocatedGrids_ = 0;
int FFTGrid::nGrids_            = 0;
bool FFTGrid::terminateOnMaxGrid_ = false;
float FFTGrid::maxFFTMemUse_    = 0;
float FFTGrid::FFTMemUse_       = 0;
