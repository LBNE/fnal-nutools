////////////////////////////////////////////////////////////////////////
/// \file  GTruth.h
/// \Class to hold the additional information needed to recreate a genie::EventRecord
/// \author  nathan.mayer@tufts.edu
////////////////////////////////////////////////////////////////////////

/// This class stores/retrieves the additional information needed (and not in MCTruth) to recreate a genie::EventRecord
/// for genie based event reweighting.

#ifndef SIMB_GTRUTH_H
#define SIMB_GTRUTH_H

#include <TLorentzVector.h>

#include "art/Persistency/Common/Ptr.h"

#include <vector>
#include <iostream>
#include <string>

namespace simb {

  class GTruth {
    
  public:
    GTruth();

    ~GTruth();
    
    //interactions info
    int fGint; ///<interaction code
    int fGscatter; ///< neutrino scattering code
     
    //Event info
    double fweight; ///< event interaction weight (genie internal)
    double fprobability; ///< interaction probability 
    double fXsec; ///< cross section of interaction
    double fDiffXsec; ///< differential cross section of interaction
    
    //genie::XclsTag info
    int fNumPiPlus;  ///< number of pi pluses in the final state
    int fNumPiMinus; ///< number of pi minuses in the final state
    int fNumPi0;     ///< number of pi0 in the final state
    int fNumProton;  ///< number of protons in the final state
    int fNumNeutron; ///< number of neutrons in the final state
    bool fIsCharm;   ///< did the interaction produce a charmed hadron
    int fResNum;     ///< resonance number

    //kinematics info 
    ///<these are for the interal (on shell) genie kinematics
    double fgQ2;
    double fgq2;
    double fgW;
    double fgT;
    double fgX;
    double fgY;
    TLorentzVector fFShadSystP4;
    
    //Target info
    bool fIsSeaQuark;
    TLorentzVector fHitNucP4;
    int ftgtZ;
    int ftgtA;
    int ftgtPDG; //Target Nucleous(?) PDG
        
    //Initial State info
    int fProbePDG;
    TLorentzVector fProbeP4;
    TLorentzVector fVertex;
    
  };

} //end simp namespace

#endif // SIMB_GTRUTH_H
