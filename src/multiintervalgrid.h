/***************************************************************************
*      Copyright (C) 2008 by Norwegian Computing Center and Statoil        *
***************************************************************************/

#ifndef MULTIINTERVALGRID_H
#define MULTIINTERVALGRID_H

#include "src/definitions.h"
#include "src/modelsettings.h"
#include "src/inputfiles.h"
#include "nrlib/flens/nrlib_flens.hpp"
#include "src/cravatrend.h"

class CravaTrend;
class MultiIntervalGrid
{
public:
  MultiIntervalGrid();

  MultiIntervalGrid(ModelSettings  * model_settings,
                    InputFiles     * input_files,
                    const Simbox   * estimation_simbox,
                    std::string    & err_text,
                    bool           & failed);

  ~MultiIntervalGrid();

  //GET FUNCTIONS
  int                                                      GetNIntervals()       const           { return n_intervals_            ;}
  //const std::vector<Simbox>                              & GetSimboxes() const                   { return simboxes_               ;}
  const std::vector<Simbox>                              & GetIntervalSimboxes() const           { return interval_simboxes_      ;}
  std::vector<Simbox>                                    & GetIntervalSimboxes()                 { return interval_simboxes_      ;}
  //const Simbox                                           * GetSimbox(int i) const                { return &simboxes_[i]           ;}
  const Simbox                                           * GetIntervalSimbox(int i) const        { return &interval_simboxes_[i]  ;}
  Simbox                                                 * GetIntervalSimboxN(int i)             { return &interval_simboxes_[i]   ;}
  const std::vector<std::vector<NRLib::Grid<double> > >  & GetParametersAllIntervals() const     { return parameters_             ;}
  const std::vector<NRLib::Grid<double> >                & GetParametersForInterval(int i) const { return parameters_[i]          ;}
  const std::string                                      & GetIntervalName(int i)                { return interval_names_[i]      ;}
  //const std::vector<Surface>                             & GetErodedSurfaces() const             { return eroded_surfaces_       ;}
  const std::vector<int>                                 & GetErosionPriorities() const          { return erosion_priorities_     ;}
  const std::vector<std::string>                         & GetSurfaceFiles() const               { return surface_files_          ;}

  const std::vector<std::vector<NRLib::Grid<double> > > & GetCovParametersIntervals() const      { return prior_cov_              ;}
  const std::vector<NRLib::Grid<double> >               & GetCovParametersInterval(int i) const  { return prior_cov_[i]           ;}
  const std::vector<std::vector<NRLib::Grid<double> > > & GetCorrParametersIntervals() const     { return prior_corr_             ;}
  const std::vector<NRLib::Grid<double> >               & GetCorrParametersInterval(int i) const { return prior_corr_[i]          ;}

  const NRLib::Matrix                                   & GetPriorVar0(int i) const              { return prior_var_0_[i]         ;}

  const std::vector<CravaTrend>                         & GetTrendCubes() const                  { return trend_cubes_            ;}
  const CravaTrend                                      & GetTrendCube(int i) const              { return trend_cubes_[i]         ;}

  const std::vector<std::vector<NRLib::Grid<double> > > & GetPriorFaciesProbCubes()              const { return prior_facies_prob_cubes_    ;}
  const std::vector<NRLib::Grid<double> >               & GetPriorFaciesProbCubesInterval(int i) const { return prior_facies_prob_cubes_[i] ;}

  const std::vector<double>                             & GetBackgroundVsVpRatios()    const     { return background_vs_vp_ratios_    ;}
  const double                                          & GetBackgroundVsVpRatioInterval(int i)  { return background_vs_vp_ratios_[i] ;}

  //SET FUNCTIONS
  void AddParametersForInterval(int i, std::vector<NRLib::Grid<double> > parameters)             { parameters_[i]    = parameters  ;}
  void AddParameterForInterval(int i, int j, NRLib::Grid<double> parameter)                      { parameters_[i][j] = parameter   ;}
  void AddParametersCovForInterval(int i, std::vector<NRLib::Grid<double> > cov)                 { prior_cov_[i]     = cov         ;}
  void AddParametersCorrForInterval(int i, std::vector<NRLib::Grid<double> > corr)               { prior_corr_[i]    = corr        ;}
  void SetPriorVar0(int i, NRLib::Matrix prior_var_0)                                            { prior_var_0_[i]   = prior_var_0 ;}
  void AddTrendCubes(std::vector<CravaTrend> trend_cubes)                                        { trend_cubes_      = trend_cubes ;}

  void AddPriorFaciesCubes(std::vector<std::vector<NRLib::Grid<double> > > prior_cubes)          { prior_facies_prob_cubes_ = prior_cubes ;}

  void SetBackgroundVsVpRatios(std::vector<double> vs_vp_ratios)                                 { background_vs_vp_ratios_             = vs_vp_ratios ;}
  void SetBackgroundVsVpRatio(int i_interval, double vs_vp_ratio)                                { background_vs_vp_ratios_[i_interval] = vs_vp_ratio  ;}


private:

  void  SetupIntervalSimboxes(ModelSettings                             * model_settings,
                              const Simbox                              * estimation_simbox,
                              const std::vector<std::string>            & interval_names,
                              const std::vector<Surface>                & eroded_surfaces,
                              std::vector<Simbox>                       & interval_simboxes,
                              const std::map<std::string, std::string>  & corr_dir_single_surfaces,
                              const std::map<std::string, std::string>  & corr_dir_top_surfaces,
                              const std::map<std::string, std::string>  & corr_dir_base_surfaces,
                              const std::map<std::string, bool>         & corr_dir_top_conform,
                              const std::map<std::string, bool>         & corr_dir_base_conform,
                              std::vector<double>                       & desired_grid_resolution,
                              std::vector<double>                       & relative_grid_resolution,
                              std::string                               & err_text,
                              bool                                      & failed) const;

  void  SetupIntervalSimbox(ModelSettings                               * model_settings,
                            const Simbox                                * estimation_simbox,
                            Simbox                                      & interval_simboxes,
                            const std::string                           & corr_dir_single_surf,
                            const std::string                           & corr_dir_top_surf,
                            const std::string                           & corr_dir_base_surf,
                            bool                                          corr_dir_top_conform,
                            bool                                          corr_dir_base_conform,
                            double                                      & desired_grid_resolution,
                            double                                      & relative_grid_resolution,
                            std::string                                 & err_text,
                            bool                                        & failed) const;

  Surface * MakeSurfaceFromFileName(const std::string        & file_name,
                                    const Simbox             & estimation_simbox) const;

  void ErodeSurface(Surface       &  surface,
                    const Surface &  priority_surface,
                    const Simbox  &  simbox,
                    const bool    &  compare_upward) const;

  void ErodeAllSurfaces(std::vector<Surface>            & eroded_surfaces,
                        const std::vector<int>          & erosion_priorities,
                        const std::vector<Surface>      & surface,
                        const Simbox                    & simbox) const;

  void  FindSmallestSurfaceGeometry(const double   x0,
                                    const double   y0,
                                    const double   lx,
                                    const double   ly,
                                    const double   rot,
                                    double       & x_min,
                                    double       & y_min,
                                    double       & x_max,
                                    double       & y_max) const;

  void  BuildSeismicPropertyIntervals(std::vector<NRLib::Grid<double> >          & vp_interval,
                                      std::vector<NRLib::Grid<double> >          & vs_interval,
                                      std::vector<NRLib::Grid<double> >          & rho_interval,
                                      const std::vector<Simbox>                 & interval_simboxes,
                                      std::vector<double>                       & relative_grid_resolution) const;

  void  EstimateZPaddingSize(Simbox          * simbox,
                             ModelSettings   * model_settings) const;

  int   SetPaddingSize(int        nx,
                       double     px) const;

  int   FindClosestFactorableNumber(int leastint) const;

  void  LogIntervalInformation(const Simbox         & simbox,
                               const std::string    & interval_name,
                               const std::string    & header_text1,
                               const std::string    & header_text2) const;

  void  LogIntervalInformation(const Simbox      * simbox,
                               const std::string & header_text1,
                               const std::string & header_text2) const;

  double  FindResolution(const Surface * top_surface,
                         const Surface * base_surface,
                         const Simbox  * estimation_simbox,
                         int             n_layers) const;

  void  EstimateXYPaddingSizes(Simbox          * interval_simbox,
                               ModelSettings   * model_settings) const;

  // CLASS VARIABLES
  size_t                                               n_intervals_;
  bool                                                 multiple_interval_setting_;
  std::vector<std::string>                             interval_names_;
  std::vector<Surface>                                 eroded_surfaces_; ///H Eroded surfaces stored in simboxes?
  std::vector<int>                                     erosion_priorities_;
  std::vector<std::string>                             surface_files_;

  //std::vector<Simbox>                                  simboxes_;                 // original inversion interval without correlation directions
  std::vector<Simbox>                                  interval_simboxes_;        // extended simbox with padding and correlation direction, must have same size as the parameters vector
  std::vector<std::vector<NRLib::Grid<double> > >      parameters_;               // must have same size as the simbox vector
  //H Change parameters_ to map?

  std::vector<double>                                  background_vs_vp_ratios_; //vs_vp_ratios from generation of backgroundmodel for multiinteval

  std::vector<std::vector<NRLib::Grid<double> > >      prior_facies_prob_cubes_;   //Vector over facies, then intervals.

  std::vector<std::vector<NRLib::Grid<double> > >      prior_cov_; //Vp, vs, rho //From CommonData -> SetupPriorCorrelation
  std::vector<std::vector<NRLib::Grid<double> > >      prior_corr_; //Vp-vs, Vp-Rho, Vs-Rho
  std::vector<NRLib::Matrix>                           prior_var_0_;

  std::vector<double>                                  desired_grid_resolution_;  //Max vertical distance between original interval surfaces divided by number of layers
  std::vector<double>                                  relative_grid_resolution_; //Actual grid resolution relative to the wanted grid resolution.

  std::vector<CravaTrend>                              trend_cubes_;  //Trend cubes per interval.

};

#endif
