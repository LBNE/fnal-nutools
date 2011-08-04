////////////////////////////////////////////////////////////////////////
/// \file  GENIEHelper.h
/// \brief Wrapper for generating neutrino interactions with GENIE
///
/// \version $Id: GENIEHelper.h,v 1.10 2011-08-04 22:42:25 rhatcher Exp $
/// \author  brebel@fnal.gov
////////////////////////////////////////////////////////////////////////
#ifndef EVGB_GENIEHELPER_H
#define EVGB_GENIEHELPER_H

#include <vector>
#include <set>
#include "EVGDrivers/GFluxI.h"
#include "EVGDrivers/GeomAnalyzerI.h"
#include "EVGDrivers/GMCJDriver.h"

class TH1D;

///parameter set interface
namespace fhicl {
  class ParameterSet;
}

namespace simb  { class MCTruth;     }
namespace simb  { class MCFlux;      }

///GENIE neutrino interaction simulation
namespace genie { class EventRecord; }

namespace evgb{

  class GENIEHelper {
    
  public:
  
    explicit GENIEHelper(fhicl::ParameterSet const& pset);
    ~GENIEHelper();

    void                   Initialize();
    bool                   Stop();
    bool                   Sample(simb::MCTruth &truth, 
				  simb::MCFlux  &flux);
    double                 TotalHistFlux();
    double                 POTUsed()          const { return fPOTUsed;        }
    std::string            FluxType()         const { return fFluxType;       }
    std::string            DetectorLocation() const { return fDetLocation;    }
    
    // methods for checking the various algorithms in GENIEHelper - please
    // do not use these in your code!!!!!
    std::vector<TH1D*>     FluxHistograms()   const { return fFluxHistograms; }   
    double                 TotalMass()        const { return fDetectorMass+fSurroundingMass; }
    double                 TargetA()          const { return fTargetA;        }

  private:

    void InitializeGeometry();
    void InitializeFluxDriver();
    void PackNuMIFlux(simb::MCFlux &flux);
    void PackSimpleFlux(simb::MCFlux &flux);
    void PackMCTruth(genie::EventRecord *record, simb::MCTruth &truth);

    void FindFluxPath(std::string userpattern);

    genie::GeomAnalyzerI*    fGeomD;       
    genie::GFluxI*           fFluxD;             ///< real flux driver
    genie::GFluxI*           fFluxD2GMCJD;       ///< flux driver passed to genie GMCJDriver, might be GFluxBlender
    genie::GMCJDriver*       fDriver;

    std::string              fFluxType;          ///< histogram or ntuple
    std::string              fFluxFile;          ///< name of file containing histograms or ntuples, or txt
    std::string              fBeamName;          ///< name of the beam we are simulating
    std::string              fTopVolume;         ///< top volume in the ROOT geometry in which to generate events
    std::string              fWorldVolume;       ///< name of the world volume in the ROOT geometry
    std::string              fDetLocation;       ///< name of flux window location
    std::vector<TH1D *>      fFluxHistograms;    ///< histograms for each nu species

    double                   fTargetA;           ///< A of the target nucleus
    double                   fEventsPerSpill;    ///< number of events to generate in each spill if not using POT/spill
    double                   fPOTPerSpill;       ///< number of pot per spill
    double                   fHistEventsPerSpill;///< number of events per spill for histogram fluxes - changes each spill
    double                   fSpillTotal;        ///< total of either pot or events for this spill
    double                   fMonoEnergy;        ///< energy of monoenergetic neutrinos
    double                   fPOTUsed;           ///< pot used from flux ntuple
    double                   fXSecMassPOT;       ///< product of cross section, mass and POT/spill for histogram fluxes
    double                   fTotalHistFlux;     ///< total flux of neutrinos from flux histograms for used flavors
    TVector3                 fBeamDirection;     ///< direction of the beam for histogram fluxes
    TVector3                 fBeamCenter;        ///< center of beam for histogram fluxes - must be in meters
    double                   fBeamRadius;        ///< radius of cylindar for histogram fluxes - must be in meters
    double                   fDetLength;         ///< length of the detector in meters
    double                   fDetectorMass;      ///< mass of the detector in kg
    double                   fSurroundingMass;   ///< mass of material surrounding the detector that is intercepted by 
                                                 ///< the cylinder for the histogram flux in kg
    double                   fGlobalTimeOffset;  ///< overall time shift (ns) added to every particle time
    double                   fRandomTimeOffset;  ///< additional random time shift (ns) added to every particle time 
    double                   fZCutOff;           ///< distance in z beyond the end of the detector that you allow interactions, in m
    std::set<int>            fGenFlavors;        ///< pdg codes for flavors to generate
    std::vector<std::string> fEnvironment;       ///< environmental variables and settings used by genie
    std::string              fMixerConfig;       ///< configuration string for genie GFlavorMixerI
    double                   fMixerBaseline;     ///< baseline distance if genie flux can't calculate it
    unsigned int             fDebugFlags;        ///< set bits to enable debug info
  };
}
#endif //EVGB_GENIEHELPER_H
