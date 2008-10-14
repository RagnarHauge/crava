#ifndef BLOCKEDLOGS_H
#define BLOCKEDLOGS_H

#include "nrlib/iotools/logkit.hpp"
#include <stdlib.h>


class ModelSettings;
class RandomGen;
class FFTGrid;
class WellData;
class Simbox;

class BlockedLogs
{
public:
  BlockedLogs(WellData  * well, 
              Simbox    * simbox,
              RandomGen * random);
  ~BlockedLogs(void);

  const char   * getWellname(void)                  const { return wellname_ ;} 
  const int      getNumberOfBlocks(void)            const { return nBlocks_ ;}  
  const double * getXpos(void)                      const { return xpos_ ;}
  const double * getYpos(void)                      const { return ypos_ ;}
  const double * getZpos(void)                      const { return zpos_ ;}
  const int    * getIpos(void)                      const { return ipos_ ;}
  const int    * getJpos(void)                      const { return jpos_ ;}
  const int    * getKpos(void)                      const { return kpos_ ;}
  const float  * getAlpha(void)                     const { return alpha_ ;}
  const float  * getBeta(void)                      const { return beta_ ;}
  const float  * getRho(void)                       const { return rho_ ;}
  const int    * getFacies(void)                    const { return facies_ ;}
  const float  * getAlphaHighCutBackground(void)    const { return alpha_highcut_background_ ;} 
  const float  * getBetaHighCutBackground(void)     const { return beta_highcut_background_ ;}
  const float  * getRhoHighCutBackground(void)      const { return rho_highcut_background_ ;}
  const float  * getAlphaHighCutSeismic(void)       const { return alpha_highcut_seismic_ ;}
  const float  * getBetaHighCutSeismic(void)        const { return beta_highcut_seismic_ ;} 
  const float  * getRhoHighCutSeismic(void)         const { return rho_highcut_seismic_ ;}  
  const float  * getAlphaSeismicResolution(void)    const { return alpha_highcut_seismic_ ;}
  const float  * getBetaSeismicResolution(void)     const { return beta_highcut_seismic_ ;} 
  const float  * getRhoSeismicResolution(void)      const { return rho_highcut_seismic_ ;}  
  float       ** getRealSeismicData(void)           const { return real_seismic_data_; }
  float       ** getSyntSeismicData(void)           const { return synt_seismic_data_; }
  float       ** getCpp(void)                       const { return cpp_ ;}              
  void           getVerticalTrend(const float * blockedLog, float * trend);
  void           getVerticalTrend(const int * blockedLog,int * trend, RandomGen * random);
  void           getBlockedGrid(FFTGrid * grid, float * blockedLog);
  void           writeToFile(float dz, int type, bool exptrans) const;
  void           writeRMSWell(ModelSettings * modelSettings);

private:
  void           blockWell(WellData  * well, 
                           Simbox    * simbox,
                           RandomGen * random);
  void           blockContinuousLog(const int   *  bInd,
                                    const float *  wellLog,
                                    float       *& blockedLog);
  void           blockDiscreteLog(const int * bInd,
                                  const int *  wellLog,
                                  const int *  faciesNumbers,
                                  int          nFacies,
                                  int       *& blockedLog,
                                  RandomGen * random);
  int            findMostProbable(const int * count,
                                  int         nFacies,
                                  RandomGen * random); 
  void           findSizeAndBlockPointers(WellData * well,
                                          Simbox   * simbox,
                                          int      * bInd);
  void           findBlockIJK(WellData  * well,
                              Simbox    * simbox,
                              const int * bInd);
  void           findBlockXYZ(Simbox * simbox);

  char         * wellname_;                 ///< Name of well   

  double       * xpos_;                     ///<
  double       * ypos_;                     ///< Simbox XYZ value for block
  double       * zpos_;                     ///<

  int          * ipos_;                     ///<
  int          * jpos_;                     ///< Simbox IJK value for block
  int          * kpos_;                     ///<

  float        * alpha_;                    ///<
  float        * beta_;                     ///< Raw logs (log-domain)
  float        * rho_;                      ///<
  int 	       * facies_;                   ///< Facies numbers using *internal* numbering

  float        * alpha_highcut_background_; ///< 
  float        * beta_highcut_background_;  ///< Logs high-cut filtered to background resolution (log-domain)
  float        * rho_highcut_background_;   ///< 

  float        * alpha_highcut_seismic_;    ///< 
  float        * beta_highcut_seismic_;     ///< Logs high-cut filtered to approx. seismic resolution (log-domain)
  float        * rho_highcut_seismic_;      ///< 

  float        * alpha_seismic_resolution_; ///< 
  float        * beta_seismic_resolution_;  ///< Logs filtered to resolution of inversion result
  float        * rho_seismic_resolution_;   ///< 

  float       ** real_seismic_data_;        ///< Seismic data
  float       ** synt_seismic_data_;        ///< Forward modelled seismic data
  float       ** cpp_;                      ///< Reflection coefficients
  int            nAngles_;                  ///< Number of AVA stacks

  int            firstM_;                   ///< First well log entry contributing to blocked well
  int            lastM_;                    ///< Last well log entry contributing to blocked well

  int            firstB_;                   ///< First block with contribution from well log
  int            lastB_;                    ///< Last block with contribution from well log

  int            nBlocks_;                  ///< Number of blocks
  int            nLayers_;                  ///< Number of vertical layers (nz)

  const int    * faciesNumbers_;
  int            nFacies_;
};

#endif
