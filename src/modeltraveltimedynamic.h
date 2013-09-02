/***************************************************************************
*      Copyright (C) 2008 by Norwegian Computing Center and Statoil        *
***************************************************************************/

#ifndef MODELTRAVELTIMEDYNAMIC_H
#define MODELTRAVELTIMEDYNAMIC_H

#include <stdio.h>

#include "nrlib/surface/regularsurface.hpp"

#include "src/definitions.h"
#include "src/modelsettings.h"
#include "src/inputfiles.h"

class Simbox;
class WellData;
class FFTGrid;
class InputFiles;
class ModelGeneral;
class SeismicParametersHolder;
class RMSTrace;

class ModelTravelTimeDynamic
{
public:
  ModelTravelTimeDynamic(const ModelSettings           * modelSettings,
                         const ModelGeneral            * modelGeneral,
                         const InputFiles              * inputFiles,
                         const int                     & vintage);

  ~ModelTravelTimeDynamic();

  bool                          getFailed()                const { return failed_                 ;}
  std::vector<bool>             getFailedDetails()         const { return failed_details_         ;}


private:

  void                          processHorizons(std::vector<Surface>   & horizons,
                                                const InputFiles       * inputFiles,
                                                std::string            & errTxt,
                                                bool                   & failed);

  void                          processRMSData(const ModelSettings      * modelSettings,
                                               const InputFiles         * inputFiles,
                                               const Simbox             * timeSimbox,
                                               std::string              & errTxt,
                                               bool                     & failed);

  void                          readRMSData(const std::string & fileName,
                                            std::string       & errTxt);

  double                        findMaxTime() const;

  NRLib::Grid2D<double>         calculateG(const std::vector<double> & rms_time,
                                           const double              & t_top,
                                           const double              & t_bot,
                                           const double              & dt_simbox,
                                           const int                 & n_layers,
                                           const int                 & n_layers_above,
                                           const int                 & n_layers_below,
                                           const int                 & n_layers_simbox,
                                           const int                 & n_layers_padding) const;

  void                          getCoordinates(const Simbox   * timeSimbox,
                                               const RMSTrace & rms_trace,
                                               double         & t_top,
                                               double         & t_bot,
                                               double         & dt_simbox) const;

  std::vector<Surface>      horizons_;              ///< Horizons used for horizon inversion
  std::vector<RMSTrace>     rms_traces_;

  bool                      failed_;                ///< Indicates whether errors occured during construction.
  std::vector<bool>         failed_details_;        ///< Detailed failed information.

  int                       thisTimeLapse_;         ///< Time lapse of the current travel time data set

};

#endif
