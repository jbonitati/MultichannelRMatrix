#include "channel.h"
#include "particle.h"
#include "OpticalPotential.h"
#include "lagbasis.h"

#include <vector>
#include <armadillo>
#include <complex>


class Collision{
  private:
    const double a; // channel radius
    double b; //Bloch constant
    double energy; //projectile energy
    
    const Particle proj;  //projectile particle
    const Particle targ; //target particle
    
    const int num_channels;
    std::vector<Channel*> channels;
    
    const OpticalPotential pot; //manages local potential
    const NonLocalOpticalPotential nlpot; //manages nonlocal potential
    
    int basis_size;
    LagBasis lagrangeBasis;
    
  public:
    ~Collision(){
      for (std::vector<Channel *>::iterator it = channels.begin() ; it != channels.end(); ++it)
        delete (*it);
        
      channels.clear();
    }
    
    Collision(const double a_size, double bloch, double e,
      const double m1, const double m2, const double z1, const double z2,
      const int num_channels_, std::vector<Channel*> channels_,
      OpticalPotential op, NonLocalOpticalPotential nlop, int basis_size): 
      a(a_size), b(bloch), energy(e), 
      proj(m1, z1), targ(m2, z2), 
      num_channels(num_channels_), channels(channels_),
      pot(op), nlpot(nlop), basis_size(n), lagrangeBasis(n)
    { }
    
    arma::cx_cube cmatrixCalc();
};
