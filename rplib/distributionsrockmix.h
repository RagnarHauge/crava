#ifndef RPLIB_DISTRIBUTIONS_ROCK_MIX_H
#define RPLIB_DISTRIBUTIONS_ROCK_MIX_H

#include "rplib/distributionsrock.h"
#include "rplib/demmodelling.h"

class Rock;
class DistributionWithTrend;

//This file contains two classes DistributionsRockMixOfRock and DistributionsRockMixOfSolidAndFluid.

//-------------------------------------- DistributionsRockMixOfRock ---------------------------------------------------------

class DistributionsRockMixOfRock : public DistributionsRock {
public:

  DistributionsRockMixOfRock(const std::vector< DistributionsRock * >           & distr_rock,
                             const std::vector< DistributionWithTrend * >       & distr_vol_frac,
                             DEMTools::MixMethod                                  mix_method);

  virtual ~DistributionsRockMixOfRock();

  // Rock is an abstract class, hence pointer must be used here. Allocated memory (using new) MUST be deleted by caller.
  virtual Rock                          * GenerateSample(const std::vector<double> & trend_params) const;

  virtual Rock                          * UpdateSample(const std::vector<double> & /*corr*/,
                                                       const Rock                & /*rock*/)       const { return NULL; }

  virtual std::vector<double>             GetExpectation(const std::vector<double> & trend_params) const;

  virtual NRLib::Grid2D<double>           GetCovariance(const std::vector<double> & trend_params)  const;

  virtual Pdf3D                         * GeneratePdf()                                            const;

  virtual bool                            HasDistribution()                                        const;

  virtual std::vector<bool>               HasTrend()                                               const;

private:
  std::vector< DistributionsRock * >      distr_rock_;
  std::vector< DistributionWithTrend * >  distr_vol_frac_;
  DEMTools::MixMethod                     mix_method_;
};

//-------------------------------------- DistributionsRockMixOfSolidAndFluid ---------------------------------------------------------

class DistributionsSolid;
class DistributionsFluid;


class DistributionsRockMixOfSolidAndFluid : public DistributionsRock {
public:

  DistributionsRockMixOfSolidAndFluid(const std::vector< DistributionsSolid * >           & distr_solid,
                                      const std::vector< DistributionsFluid * >           & distr_fluid,
                                      const std::vector< DistributionWithTrend * >        & distr_vol_frac,
                                      const DistributionWithTrend                         * distr_porosity,
                                      DEMTools::MixMethod                                   mix_method);

  virtual ~DistributionsRockMixOfSolidAndFluid();

  // Rock is an abstract class, hence pointer must be used here. Allocated memory (using new) MUST be deleted by caller.
  virtual Rock                          * GenerateSample(const std::vector<double> & trend_params) const;

  virtual Rock                          * UpdateSample(const std::vector<double> & /*corr*/,
                                                       const Rock                & /*rock*/)       const { return NULL; }

  virtual std::vector<double>             GetExpectation(const std::vector<double> & trend_params) const;

  virtual NRLib::Grid2D<double>           GetCovariance(const std::vector<double> & trend_params)  const;

  virtual Pdf3D                         * GeneratePdf()                                            const;

  virtual bool                            HasDistribution()                                        const;

  virtual std::vector<bool>               HasTrend()                                               const;

private:
  std::vector< DistributionsSolid * >     distr_solid_;
  std::vector< DistributionsFluid * >     distr_fluid_;
  std::vector< DistributionWithTrend * >  distr_vol_frac_;
  const DistributionWithTrend           * distr_porosity_;
  DEMTools::MixMethod                     mix_method_;
};

#endif
