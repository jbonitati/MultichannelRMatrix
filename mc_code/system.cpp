#include <cmath>
#include <gsl/gsl_sf.h>
#include <fstream>
#include "system.h"
#include <iostream>
#include <vector>
#include "wavefunction.h"

const double hbarc = 197.3269718; //MeV * fm
const double mass_unit=931.494; //MeV / c^2
const arma::cx_double I(0.0,1.0);
const arma::cx_double nrmlz = I/2.0;//normalization constant

using namespace boost::numeric::ublas;

System::System(const double a_size, double e,
  const double m1, const double m2, const double z1, const double z2,
  int c0, std::vector<Channel> c,
  OpticalPotential op, NonLocalOpticalPotential nlop, int b_size,
  double step, double max, matrix<Coupling_Potential> coupling): 
  a(a_size), energy(e), proj(m1, z1), targ(m2, z2), 
  entrance_channel(c0), channels(c),
  pot(op), nlpot(nlop), basis_size(b_size), step_size(step), r_max(max),
  lagrangeBasis(basis_size, a_size), cmatrix(c.size()*basis_size, c.size()*basis_size),
  invcmatrix(c.size()*basis_size, c.size()*basis_size), rmatrix(c.size(), c.size()), 
  umatrix(c.size(),c.size()), coupling_matrix(coupling)
{ 
  std::cout << "Calculating C matrix..." << std::endl;
  //calculate C matrix and inverse C matrix
  cmatrixCalc();
  
  std::cout << "Calculating R matrix..." << std::endl;
  //calculate R matrix
  rmatrixCalc();
  std::cout << rmatrix << std::endl;
  
  std::cout << "Calculating Collision (U) matrix..." << std::endl;
  //calculate U matrix
  umatrixCalc();
  std::cout << umatrix << std::endl;
}


//calculates the coupling potential between two channels
double System::couplingPotential(double r1, double r2, int c1, int c2){
  return coupling_matrix(c1,c2).getValue(r1,r2,targ);
}

void System::cmatrixCalc(){
  //the C matrix is made up of a different matrix for each pair of channels
  int N = basis_size;
  
  //TLmatrix uses the T_0, L(0), and energy operators
  //Vmatrix uses the potential, centrifugal, and channel energy operators
  for(unsigned int c = 0; c < channels.size(); c++){
    Channel * c1 = &channels[c];
    
    for(unsigned int cprime = 0; cprime < channels.size(); cprime++){
      //Channel * c2 = &channels[cprime];
      arma::cx_mat Vmatrix(N,N, arma::fill::zeros);
      if(c == cprime){
        //include TLmatrix as well as Vmatrix
        arma::cx_mat TLmatrix(N, N, arma::fill::zeros);
    
        double mu = c1->getMu();
        double TLcoeff = hbarc*hbarc/(2*mass_unit*mu);
        
        for(int i = 0; i < N; i++){ 
          double xi = lagrangeBasis.x(i);
          double ri = a*xi;
          
          TLmatrix(i,i) = TLcoeff*((4*N*N+4*N+3)*xi*(1-xi)-6*xi+1)/
            (3*a*a*xi*xi*(1-xi)*(1-xi)) - c1->getB() - energy;
          Vmatrix(i,i) = c1->central_potential(ri) 
            + c1->getE()
            + pot.totalPotential(ri, targ, c1)
            + proj.coulomb_potential_to(ri, targ);
          for(int j = 0; j < N; j++){
            double xj = lagrangeBasis.x(j);
            double rj = a*xj;
            
            if(j > i){
              TLmatrix(i,j)=TLcoeff 
              * pow(-1,i+j)/(a*a*sqrt(xi*xj*(1-xi)*(1-xj)))
              * (N*N + N + 1 + (xi+xj-2*xi*xj) / ((xi-xj)*(xi-xj)) 
                - 1.0/(1-xi) - 1.0/(1-xj)) - c1->getB();
              TLmatrix(j,i)=TLmatrix(i,j);
            }
            
            Vmatrix(i,j) += a*sqrt(lagrangeBasis.w(i)*lagrangeBasis.w(j)) 
              * nlpot.totalPotential(ri, rj, targ, c1);
          }
        }
        cmatrix.submat(c*N, cprime*N, (c+1)*N-1, (cprime+1)*N-1) = TLmatrix + Vmatrix;
        
      }else{
        //only include coupling potential for different channels
        for(int i = 0; i < N; i++){
          double xi = lagrangeBasis.x(i);
          double ri = a*xi;
          
          for(int j = 0; j < N; j++){
            double xj = lagrangeBasis.x(j);
            double rj = a*xj;
            
            Vmatrix(i,j) = a*sqrt(lagrangeBasis.w(i)*lagrangeBasis.w(j))
              *couplingPotential(ri,rj,c,cprime);
          }
        }
        cmatrix.submat(c*N, cprime*N, (c+1)*N-1, (cprime+1)*N-1) = Vmatrix;
      }
    }
  }
  std::cout << "Calculating inverse C matrix..." << std::endl;
  //std::cout << arma::inv(arma::real(cmatrix(0,1))) << std::endl;
  //now create inverted C matrix
  //for(unsigned int i = 0; i < channels.size(); i++){
  //  for(unsigned int j = 0; j < channels.size(); j++){
      invcmatrix = arma::inv(cmatrix);
  //  }
  //}
}


/*calculates the rmatrix from the inverse C matrix*/
void System::rmatrixCalc(){
  for(unsigned int c = 0; c < channels.size(); c++){
    Channel * c1 = &channels[c];
    for(unsigned int cprime = 0; cprime < channels.size(); cprime++){
      Channel * c2 = &channels[cprime];
      arma::cx_mat cinv = invcmatrix.submat(c*basis_size,cprime*basis_size,
        (c+1)*basis_size-1, (cprime+1)*basis_size-1);
      
      double coeff = hbarc*hbarc / (2*a*mass_unit*sqrt(c1->getMu() * c2->getMu()));
      
      arma::cx_double expansion = 0;
      for(int i = 0; i < basis_size; i++){
        for(int j = 0; j < basis_size; j++){
          expansion += lagrangeBasis.phi(i+1,a)*cinv(i,j)*lagrangeBasis.phi(j+1,a);
        }
      }
      
      rmatrix(c,cprime) = coeff*expansion;
      
    }
  }
}

//calculates and returns the collision matrix from the R matrix
void System::umatrixCalc(){
  arma::cx_mat zimatrix(channels.size(),channels.size());
  arma::cx_mat zomatrix(channels.size(),channels.size());
  arma::cx_double ovalue, ivalue, opvalue, ipvalue, ovalue2, ivalue2, opvalue2, ipvalue2;
  
  for(unsigned int c = 0; c < channels.size(); c++){
    Channel * c1 = &channels[c];
    double kc = c1->getKc(energy);
    c1->io_coulomb_functions(kc*a, energy, targ, proj, 
      &ivalue, &ovalue, &ipvalue, &opvalue);
    
    for(unsigned int cprime = 0; cprime < channels.size(); cprime++){
      Channel * c2 = &channels[cprime];
      double kc2 = c2->getKc(energy);
      arma::cx_double coeff = nrmlz*1.0 / (sqrt(kc2*a)); 
      
      c2->io_coulomb_functions(kc2*a, energy, targ, proj, 
        &ivalue2, &ovalue2, &ipvalue2, &opvalue2);
      
      zomatrix(c,cprime) = coeff*(-1*kc2*a*rmatrix(c,cprime)*opvalue2);
      zimatrix(c,cprime) = coeff*(-1*kc2*a*rmatrix(c,cprime)*ipvalue2);
      if(c == cprime){
        zomatrix += coeff*ovalue;
        zimatrix += coeff*ivalue;
      }
    }
  }
  umatrix = (arma::inv(zomatrix)*zimatrix);
}

//whittaker function
//used in calculating the wave function
double whittaker(double k, double m, double z){
  double coeff = exp(-1.0*z/2)*pow(z,m+0.5);
  return coeff * gsl_sf_hyperg_U(0.5+m-k, 1+2*m, z);
}


//takes the C and U matrices and stores the wavefunction values for each
//channel in the specified file
void System::waveFunction(boost::filesystem::ofstream& file){
  
  file << "r";
  for(unsigned int i = 1; i <= channels.size(); i++){
    file << ", Channel " << i;
  }
  file << std::endl;
  
  std::vector<WaveFunction> wfvalues;
  wfvalues.reserve(std::floor(r_max / step_size) + 1);
  
  double r;
  std::cout << 0;
  for(r = 0; r < a && r <= r_max; r += step_size){ 
    std::cout << "\rradius: " << r << std::flush;   
    //file << r;
    WaveFunction wf(r);
    for(unsigned int c = 0; c < channels.size(); c++){
      //Channel * c1 = &channels[c];
      
      //compute partial wave function u^int_c(c0)(r)
      //sum contains the coefficient calculated over all open channels
      arma::cx_double sum = 0;
      arma::cx_double oval, ival, opval, ipval;
      
      for(unsigned int cprime = 0; cprime < channels.size(); cprime++){
        Channel * c2 = &channels[cprime];
      
        double kc = c2->getKc(energy);
        double vc = c2->getVc(energy);
        double mu = c2->getMu();
        c2->io_coulomb_functions(kc*a, energy, targ, proj, 
          &ival, &oval, &ipval, &opval);
        double coeff = hbarc*hbarc*kc/(2*mass_unit*mu*sqrt(vc));
        arma::cx_double outersum = 
          -1.0 *coeff*umatrix(cprime, entrance_channel)*opval;
        if(cprime == entrance_channel)
          outersum += coeff*ipval;
        
        arma::cx_double innersum = 0;
        for(int i = 0; i < basis_size; i++){
          for(int j = 0; j < basis_size; j++){
            innersum += lagrangeBasis.phi(i+1,r)
              *invcmatrix((c+1)*i, (cprime+1)*j)
              *lagrangeBasis.phi(j+1,a);
          }
        }
        sum += outersum*innersum;
        
      }
      sum *= nrmlz;
      //file << ", " << std::real(sum);
      wf.add(std::real(sum));
    }
    wfvalues.push_back(wf);
  }
  
  //now print the external wave function
  while(r <= r_max){
    std::cout << "\rradius: " << r << std::flush;
   //file << r;
   WaveFunction wf(r);
   for(unsigned int c = 0; c < channels.size(); c++){
      arma::cx_double oval, ival, opval, ipval;
      Channel * c1 = &channels[c];
      double kc = c1->getKc(energy);
      double vc = c1->getVc(energy);
      double etac = c1->getEta(energy, targ, proj);
      double mu = c1->getMu();
      c1->io_coulomb_functions(kc*r, energy, targ, proj, 
        &ival, &oval, &ipval, &opval);
      arma::cx_double wfvalue;
      if(energy >= c1->getE()){
        
        double coeff = 1.0/sqrt(vc);
        wfvalue = -1.0 *coeff*umatrix(c, entrance_channel)*oval;
        if(c == entrance_channel)
          wfvalue += coeff*ival;
          
      }else{
        double coeff = 1.0/whittaker(-1.0*etac, c1->getL() + 0.5, 2*kc*a);
        arma::cx_double sum = 0;
        for(unsigned int cprime = 0; cprime < channels.size();cprime++){
          Channel * c2 = &channels[cprime];
          arma::cx_double oval2, ival2, opval2, ipval2;
          double kc2 = c2->getKc(energy);
          //double mu2 = c2->getMu();
          c2->io_coulomb_functions(kc2*a, energy, targ, proj, 
            &ival2, &oval2, &ipval2, &opval2);
          arma::cx_double sumcoeff = sqrt(mass_unit*mu*kc2/hbarc)*a*rmatrix(c,cprime);
          sum += -1.0*sumcoeff*umatrix(cprime,entrance_channel)*opval2;
          if(cprime == entrance_channel)
            sum += sumcoeff*ipval2;
        }
        wfvalue = coeff*sum*whittaker(-1.0*etac, c1->getL()+0.5,2*kc*r);
      }
      wfvalue *= nrmlz;
      //file << ", " << std::real(wfvalue);
      wf.add(std::real(wfvalue));
    }
    wfvalues.push_back(wf);
  
   r += step_size; 
  }
  std::cout << std::endl;
  std::cout << "Printing Wave functions to file..." << std::endl;
  
  for(std::vector<WaveFunction>::iterator it = wfvalues.begin(); it != wfvalues.end(); it++)
    file << (*it);
  
  std::cout << "done" << std::endl;
}
