#ifndef RPLIB_DISTRIBUTIONSFLUIDMIX_H
#define RPLIB_DISTRIBUTIONSFLUIDMIX_H

#include "rplib/distributionsfluid.h"

#include "rplib/demmodelling.h"

class DistributionWithTrend;
class FluidMix;

class DistributionsFluidMix : public DistributionsFluid {
public:

  DistributionsFluidMix(const std::vector<double>                    & alpha,
                        std::vector< DistributionsFluid * >          & distr_fluid,
                        std::vector< DistributionWithTrend * >       & distr_vol_frac,
                        DEMTools::MixMethod                            mix_method);

  DistributionsFluidMix(const DistributionsFluidMix & dist);

  virtual ~DistributionsFluidMix();

  virtual DistributionsFluid                    * Clone() const;

  virtual Fluid                                 * GenerateSample(const std::vector<double> & trend_params);

  virtual bool                                    HasDistribution() const;

  virtual std::vector<bool>                       HasTrend() const;

  virtual Fluid                                 * UpdateSample(double                      corr_param,
                                                               bool                        param_is_time,
                                                               const std::vector<double> & trend,
                                                               const Fluid               * sample);

protected:

private:

  Fluid                                         * GetSample(const std::vector<double>  & u,
                                                            const std::vector<double>  & trend_params,
                                                            const std::vector<Fluid *> & fluid_samples);

  std::vector< DistributionsFluid * >             distr_fluid_;     // Pointers to external objects.
  std::vector< DistributionWithTrend * >          distr_vol_frac_;  // Pointers to external objects.
  DEMTools::MixMethod                             mix_method_;
};

#endif
