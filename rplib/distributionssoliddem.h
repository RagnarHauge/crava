#ifndef RPLIB_DISTRIBUTIONSSOLIDDEM_H
#define RPLIB_DISTRIBUTIONSSOLIDDEM_H

#include "rplib/distributionssolid.h"

class DistributionWithTrend;

class DistributionsSolidDEM : public DistributionsSolid {
public:

  DistributionsSolidDEM(DistributionsSolid                           * distr_solid,
                        std::vector<DistributionsSolid*>             & distr_solid_inc,
                        std::vector< DistributionWithTrend * >       & distr_incl_spectrum,
                        std::vector< DistributionWithTrend * >       & distr_incl_concentration,
                        std::vector<double>                          & alpha);

  DistributionsSolidDEM(const DistributionsSolidDEM & dist);

  virtual                       ~DistributionsSolidDEM();

  virtual DistributionsSolid  * Clone() const;

  virtual Solid               * GenerateSample(const std::vector<double> & trend_params);

  virtual bool                  HasDistribution() const;

  virtual std::vector<bool>     HasTrend() const;

  virtual Solid               * UpdateSample(double                      corr_param,
                                             bool                        param_is_time,
                                             const std::vector<double> & trend,
                                             const Solid               * sample);
protected:

private:
  Solid                       * GetSample(const std::vector<double>  & u,
                                          const std::vector<double>  & trend_params,
                                          const Solid                * solid,
                                          const std::vector< Solid* >& solid_inc);

  DistributionsSolid                           * distr_solid_;              // Pointer to external object.
  std::vector< DistributionsSolid*>              distr_solid_inc_;          // Pointer to external object.
  std::vector< DistributionWithTrend * >         distr_incl_spectrum_;      // Pointers to external objects.
  std::vector< DistributionWithTrend * >         distr_incl_concentration_; // Pointers to external objects.
};

#endif
