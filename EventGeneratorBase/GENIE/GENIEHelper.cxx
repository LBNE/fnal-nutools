////////////////////////////////////////////////////////////////////////
/// \file  GENIEHelper.h
/// \brief Wrapper for generating neutrino interactions with GENIE
///
/// \version $Id: GENIEHelper.cxx,v 1.48 2012-09-06 19:06:11 rhatcher Exp $
/// \author  brebel@fnal.gov
/// \update 2010/3/4 Sarah Budd added simple_flux
////////////////////////////////////////////////////////////////////////

// C/C++ includes
#include <math.h>
#include <map>
#include <cassert>
#include <fstream>

//ROOT includes
#include "TH1.h"
#include "TH2.h" //used by GAtmoFlux
#include "TFile.h"
#include "TDirectory.h"
#include "TVector3.h"
#include "TLorentzVector.h"
#include "TCollection.h"
#include "TSystem.h"
#include "TString.h"
#include "TRandom.h" //needed for gRandom to be defined
#include "TRegexp.h"

//GENIE includes
#include "Conventions/Units.h"
#include "EVGCore/EventRecord.h"
#include "EVGDrivers/GMCJDriver.h"
#include "GHEP/GHepUtils.h"
#include "FluxDrivers/GCylindTH1Flux.h"
#include "FluxDrivers/GMonoEnergeticFlux.h"
#include "FluxDrivers/GNuMIFlux.h"
#include "FluxDrivers/GBartolAtmoFlux.h"  //for atmo nu generation
#include "FluxDrivers/GFlukaAtmo3DFlux.h" //for atmo nu generation
#include "FluxDrivers/GAtmoFlux.h"        //for atmo nu generation
#include "Conventions/Constants.h" //for calculating event kinematics

#ifndef  MISSING_GSIMPLENTPFLUX
#include "FluxDrivers/GSimpleNtpFlux.h"
#endif
#include "Geo/ROOTGeomAnalyzer.h"
#include "Geo/GeomVolSelectorFiducial.h"
#include "Geo/GeomVolSelectorRockBox.h"
#include "Utils/StringUtils.h"
#include "Utils/XmlParserUtils.h"
#include "Interaction/InitialState.h"
#include "Interaction/Interaction.h"
#include "Interaction/Kinematics.h"
#include "Interaction/KPhaseSpace.h"
#include "Interaction/ProcessInfo.h"
#include "Interaction/XclsTag.h"
#include "GHEP/GHepParticle.h"
#include "PDG/PDGCodeList.h"

// assumes in GENIE
#include "FluxDrivers/GFluxBlender.h"
#include "FluxDrivers/GFlavorMixerI.h"
#include "FluxDrivers/GFlavorMap.h"
#ifdef FLAVORMIXERFACTORY
#include "FluxDrivers/GFlavorMixerFactory.h"
#endif

//NuTools includes
#include "EventGeneratorBase/evgenbase.h"
#include "EventGeneratorBase/GENIE/GENIEHelper.h"
#include "SimulationBase/simbase.h"

//experiment includes - assumes every experiment has a Geometry package
#include "Geometry/geo.h"

// Framework includes
#include "art/Framework/Services/Registry/ServiceHandle.h"
#include "cetlib/search_path.h"
#include "cetlib/getenv.h"
#include "cetlib/split_path.h"
#include "fhiclcpp/ParameterSet.h"
#include "messagefacility/MessageLogger/MessageLogger.h"


namespace evgb {

  static const int kNue      = 0;
  static const int kNueBar   = 1;
  static const int kNuMu     = 2;
  static const int kNuMuBar  = 3;
  static const int kNuTau    = 4;
  static const int kNuTauBar = 5;

  //--------------------------------------------------
  GENIEHelper::GENIEHelper(fhicl::ParameterSet const& pset)
    : fGenieEventRecord  (0)
    , fGeomD             (0)
    , fFluxD             (0)
    , fFluxD2GMCJD       (0)
    , fDriver            (0)
    , fFluxType          (pset.get< std::string              >("FluxType")               )
    , fBeamName          (pset.get< std::string              >("BeamName")               )
    , fTopVolume         (pset.get< std::string              >("TopVolume")              )
    , fWorldVolume       ("volWorld")         
    , fDetLocation       (pset.get< std::string              >("DetectorLocation"     )  )
    , fFluxUpstreamZ     (pset.get< double                   >("FluxUpstreamZ",   -2.e30))
    , fEventsPerSpill    (pset.get< double                   >("EventsPerSpill",   0)    )
    , fPOTPerSpill       (pset.get< double                   >("POTPerSpill",      5.e13))
    , fHistEventsPerSpill(0.)
    , fSpillEvents       (0)
    , fSpillExposure     (0.)
    , fTotalExposure     (0.)
    , fMonoEnergy        (pset.get< double                   >("MonoEnergy",       2.0)  )
    , fBeamRadius        (pset.get< double                   >("BeamRadius",       3.0)  )
    , fSurroundingMass   (pset.get< double                   >("SurroundingMass",  0.)   )
    , fGlobalTimeOffset  (pset.get< double                   >("GlobalTimeOffset", 1.e4) )
    , fRandomTimeOffset  (pset.get< double                   >("RandomTimeOffset", 1.e4) )
    , fAtmoEmin          (pset.get< double                   >("AtmoEmin",         0.1)  )
    , fAtmoEmax          (pset.get< double                   >("AtmoEmax",         10.0) )
    , fAtmoRl            (pset.get< double                   >("Rl",               20.0) )
    , fAtmoRt            (pset.get< double                   >("Rt",               20.0) )
    , fEnvironment       (pset.get< std::vector<std::string> >("Environment")            )
    , fMixerConfig       (pset.get< std::string              >("MixerConfig",     "none"))
    , fMixerBaseline     (pset.get< double                   >("MixerBaseline",    0.)   )
    , fFiducialCut       (pset.get< std::string              >("FiducialCut",     "none"))
    , fGeomScan          (pset.get< std::string              >("GeomScan",        "default"))
    , fDebugFlags        (pset.get< unsigned int             >("DebugFlags",       0)    ) 
  {

    std::vector<double> beamCenter   (pset.get< std::vector<double> >("BeamCenter")   );
    std::vector<double> beamDirection(pset.get< std::vector<double> >("BeamDirection"));
    fBeamCenter.SetXYZ(beamCenter[0], beamCenter[1], beamCenter[2]);
    fBeamDirection.SetXYZ(beamDirection[0], beamDirection[1], beamDirection[2]);
    
    std::vector<std::string> fluxFiles (pset.get< std::vector<std::string> >("FluxFiles"));
    std::vector<int> genFlavors        (pset.get< std::vector<int> >("GenFlavors"));

    for (unsigned int i = 0; i < genFlavors.size(); ++i) fGenFlavors.insert(genFlavors[i]);

    // need to find the right alternative in FW_SEARCH_PATH to find 
    // the flux files without attempting to expand any actual wildcard
    // that might be in the name; the cet::search_path class seemed
    // like it could help in this.  In the end, it can't deal with
    // wildcarding in the way we want.
    /* was:
       cet::search_path sp("FW_SEARCH_PATH");

       sp.find_file(pset.get< std::string>("FluxFile"), fFluxFile);
    */

    cet::search_path sp("FW_SEARCH_PATH");
    if ( fluxFiles.size() == 1 && 
         fluxFiles[0].find_first_of("*?") != std::string::npos ) {
      mf::LogDebug("GENIEHelper") << "ctor() FindFluxPath" << fluxFiles[0];
      FindFluxPath(fluxFiles[0]);
    }
    else{
      std::string fileName;
      for(unsigned int i = 0; i < fluxFiles.size(); i++){
        sp.find_file(fluxFiles[i], fileName);
        if ( fileName != "" ) {
          mf::LogDebug("GENIEHelper") << "ctor() i=" << i << " " 
                                      << fluxFiles[i] << " found as " << fileName;
          fFluxFiles.insert(fileName);
        } else if ( fluxFiles[i][0] == '/' ) {
          // cet::search_path doesn't return files that start out as
          // absolute paths
          mf::LogDebug("GENIEHelper") << "ctor() i=" << i << " " 
                                      << fluxFiles[i] << " has /";
          fFluxFiles.insert(fluxFiles[i]);
        }
      }
    }

    // set the environment, the vector should come in pairs 
    // of variable name, then value

    // special processing of GSEED (GENIE's random seed)... priority:
    //    if set in .fcl file RandomSeed variable, use that
    //    else if already set in environment use that
    //    else use evgb::GetRandomNumberSeed()
    int dfltseed;
    const char* gseedstr = std::getenv("GSEED");
    if ( gseedstr ) {
      //dfltseed = atoi(gseedstr);
      dfltseed = strtol(gseedstr,NULL,0);
    } else {
      dfltseed = evgb::GetRandomNumberSeed();
    }
    TString junk = "";
    junk += pset.get< int >("RandomSeed", dfltseed);
    std::string seed(junk);
    fEnvironment.push_back("GSEED");
    fEnvironment.push_back(seed);

    // GXMLPATH is where GENIE will look for alternative configurations
    // (fcl file paths):(existing user environment):(FW_SEARCH_PATH)
    std::string gxmlpathadd = "";
    const char* gxmlpath_env = std::getenv("GXMLPATH");
    if ( gxmlpath_env ) {
      gxmlpathadd += gxmlpath_env;
    }
    const char* fwpath_env = std::getenv("FW_SEARCH_PATH");
    if ( fwpath_env ) {
      if ( gxmlpathadd != "" ) gxmlpathadd += ":";
      gxmlpathadd += fwpath_env;
    }
    int indxGXMLPATH = -1;
    for (unsigned int i = 0; i < fEnvironment.size(); i += 2) {
      if ( fEnvironment[i].compare("GXMLPATH") == 0 ) {
        indxGXMLPATH = i;
        break;
      }
    }
    if ( indxGXMLPATH < 0 ) {
      // nothing in fcl parameters
      indxGXMLPATH=fEnvironment.size();
      fEnvironment.push_back("GXMLPATH");
      fEnvironment.push_back(gxmlpathadd);
    } else {
      // append additonal paths to env value
      fEnvironment[indxGXMLPATH+1] += ":";
      fEnvironment[indxGXMLPATH+1] += gxmlpathadd;
    }

    for(unsigned int i = 0; i < fEnvironment.size(); i += 2){

      if(fEnvironment[i].compare("GSPLOAD") == 0){
        // currently GENIE doesn't internally use GXMLPATH when looking for
        // spline files, but instead wants a fully expanded path.
        // Do the expansion here using the extended GXMLPATH list
        // of locations (which included $FW_SEARCH_PATH)
        cet::search_path spGXML(fEnvironment[indxGXMLPATH+1]);
        std::string fullpath;
        mf::LogDebug("GENIEHelper") << "GSPLOAD as originally set: " 
                                    << fEnvironment[i+1]; 
        spGXML.find_file(fEnvironment[i+1], fullpath);
        if ( fullpath == "" ) {
          mf::LogError("GENIEHelper") 
            << "could not resolve fulll path for spline file GSPLOAD " 
            << "\"" << fEnvironment[i+1] << "\" using: " 
            << fEnvironment[indxGXMLPATH+1];
          throw cet::exception("UnresolvedGSPLOAD")
            << "can't find GSPLOAD file";
        }
        fEnvironment[i+1] = fullpath;
      }
      
      gSystem->Setenv(fEnvironment[i].c_str(), fEnvironment[i+1].c_str());
      mf::LogInfo("GENIEHelper") << "setting GENIE environment " 
                                 << fEnvironment[i] << " to \"" 
                                 << fEnvironment[i+1] <<"\"";
    }

    //For Atmo flux
    if(fFluxType.find("atmo") != std::string::npos){
      
      if(genFlavors.size() != fFluxFiles.size()){
        mf::LogInfo("GENIEHelper") <<  "ERROR: The number of generated neutrino flavors (" 
                                   << genFlavors.size() << ") doesn't correspond to the number of files (" 
                                   << fFluxFiles.size() << ")!!!";
        exit(1);
      }

      if(fEventsPerSpill !=1){
        mf::LogInfo("GENIEHelper") 
          <<  "ERROR: For Atmosphric Neutrino generation, EventPerSpill need to be 1!!";
        exit(1);
      }

      if (fFluxType.compare("atmo_FLUKA") == 0 ){
        mf::LogInfo("GENIEHelper") << "The sims are from FLUKA";
      }
      
      else if (fFluxType.compare("atmo_BARTOL") == 0 ){
        mf::LogInfo("GENIEHelper") << "The sims are from BARTOL";
      }      
      else {
        mf::LogInfo("GENIEHelper") << "Uknonwn flux simulation: " << fFluxType;
        exit(1);
      }
      
      mf::LogInfo("GENIEHelper") << "The energy range is between:  " << fAtmoEmin << " GeV and " 
                                 << fAtmoEmax << " GeV.";
  
      mf::LogInfo("GENIEHelper") << "Generation surface of: (" << fAtmoRl << "," 
                                 << fAtmoRt << ")";

    }// end if atmospheric fluxes
    
    // make the histograms
    if(fFluxType.compare("histogram") == 0){
      mf::LogInfo("GENIEHelper") << "setting beam direction and center at "
                                 << fBeamDirection.X() << " " << fBeamDirection.Y() << " " << fBeamDirection.Z()
                                 << " (" << fBeamCenter.X() << "," << fBeamCenter.Y() << "," << fBeamCenter.Z()
                                 << ") with radius " << fBeamRadius;

      TDirectory *savedir = gDirectory;
    
      fFluxHistograms.clear();

      TFile tf((*fFluxFiles.begin()).c_str());
      tf.ls();

      for(std::set<int>::iterator flvitr = fGenFlavors.begin(); flvitr != fGenFlavors.end(); flvitr++){
        if(*flvitr ==  12) fFluxHistograms.push_back(dynamic_cast<TH1D *>(tf.Get("nue")));
        if(*flvitr == -12) fFluxHistograms.push_back(dynamic_cast<TH1D *>(tf.Get("nuebar")));
        if(*flvitr ==  14) fFluxHistograms.push_back(dynamic_cast<TH1D *>(tf.Get("numu")));
        if(*flvitr == -14) fFluxHistograms.push_back(dynamic_cast<TH1D *>(tf.Get("numubar")));
        if(*flvitr ==  16) fFluxHistograms.push_back(dynamic_cast<TH1D *>(tf.Get("nutau")));
        if(*flvitr == -16) fFluxHistograms.push_back(dynamic_cast<TH1D *>(tf.Get("nutaubar")));
      }

      for(unsigned int i = 0; i < fFluxHistograms.size(); ++i){
        fFluxHistograms[i]->SetDirectory(savedir);
        fTotalHistFlux += fFluxHistograms[i]->Integral();
      }

      mf::LogInfo("GENIEHelper") << "total histogram flux over desired flavors = " 
                                 << fTotalHistFlux;

    }//end if getting fluxes from histograms

    std::string flvlist;
    for(std::set<int>::iterator itr = fGenFlavors.begin(); itr != fGenFlavors.end(); itr++)
      flvlist += Form(" %d",*itr);

    if(fFluxType.compare("mono")==0){
      fEventsPerSpill = 1;
      mf::LogInfo("GENIEHelper") << "Generating monoenergetic (" << fMonoEnergy 
                                 << " GeV) neutrinos with the following flavors: " 
                                 << flvlist;
    }
    else{
      mf::LogInfo("GENIEHelper") << "Generating flux with the following flavors: " << flvlist
                                 << "\n and these files: ";
      
      for(std::set<std::string>::iterator itr = fFluxFiles.begin(); itr != fFluxFiles.end(); itr++)
        mf::LogInfo("GENIEHelper") << "\t" << *itr;
    }

    if(fEventsPerSpill != 0)
      mf::LogInfo("GENIEHelper") << "Generating " << fEventsPerSpill 
                                 << " events for each spill";
    else
      mf::LogInfo("GENIEHelper") << "Using " << fPOTPerSpill << " pot for each spill";

    return;
  }

  //--------------------------------------------------
  GENIEHelper::~GENIEHelper()
  {
    // user request writing out the scan of the geometry
    if ( fMaxPathOutInfo != "" ) {
      genie::geometry::ROOTGeomAnalyzer* rgeom = 
        dynamic_cast<genie::geometry::ROOTGeomAnalyzer*>(fGeomD);

      string filename = "maxpathlength.xml";
      mf::LogInfo("GENIEHelper") 
        << "Saving MaxPathLengths as: \"" << filename << "\"";

      const genie::PathLengthList& maxpath = 
#ifdef GENIE_MISSING_GETMAXPL
        rgeom->ComputeMaxPathLengths(); // re-compute max pathlengths
#else
        rgeom->GetMaxPathLengths();
#endif

      maxpath.SaveAsXml(filename);
      // append extra info to file
      std::ofstream mpfile(filename.c_str(), std::ios_base::app);
      mpfile
        << std::endl
        << "<!-- this file is only relevant for a setup compatible with:" 
        << std::endl
        << fMaxPathOutInfo 
        << std::endl
        << "-->" 
        << std::endl;
      mpfile.close();
    }

    double probscale = fDriver->GlobProbScale();
    double rawpots   = 0;
    if      ( fFluxType.compare("ntuple")==0 ) {
      genie::flux::GNuMIFlux* numiFlux = dynamic_cast<genie::flux::GNuMIFlux *>(fFluxD);
      rawpots = numiFlux->UsedPOTs();
      numiFlux->PrintConfig();
    }
#ifndef MISSING_GSIMPLENTPFLUX
    else if ( fFluxType.compare("simple_flux")==0 ) {
      genie::flux::GSimpleNtpFlux* simpleFlux = dynamic_cast<genie::flux::GSimpleNtpFlux *>(fFluxD);
      rawpots = simpleFlux->UsedPOTs();
      simpleFlux->PrintConfig();
    }
#endif
    mf::LogInfo("GENIEHelper") 
      << " Total Exposure " << fTotalExposure
      << " GMCJDriver GlobProbScale " << probscale 
      << " FluxDriver base pots " << rawpots
      << " corrected POTS " << rawpots/TMath::Max(probscale,1.0e-100);

    // clean up owned genie object (other genie obj are ref ptrs)
    delete fGenieEventRecord;
    delete fDriver;
  }

  //--------------------------------------------------
  double GENIEHelper::TotalHistFlux() 
  {
    if(   fFluxType.compare("ntuple")       == 0
          || fFluxType.compare("mono")         == 0 
          || fFluxType.compare("simple_flux" ) == 0 ) return -999.;

    return fTotalHistFlux;
  }

  //--------------------------------------------------
  void GENIEHelper::Initialize()
  {
    fDriver = new genie::GMCJDriver(); // needs to be before ConfigGeomScan

    // initialize the Geometry and Flux drivers
    InitializeGeometry();
    InitializeFluxDriver();

    fDriver->UseFluxDriver(fFluxD2GMCJD);
    fDriver->UseGeomAnalyzer(fGeomD);

    // must come after creation of Geom, Flux and GMCJDriver
    ConfigGeomScan();  // could trigger fDriver->UseMaxPathLengths(*xmlfile*)

    fDriver->Configure();  // trigger GeomDriver::ComputeMaxPathLengths() 
    fDriver->UseSplines();
    fDriver->ForceSingleProbScale();

    if ( fFluxType.compare("histogram") == 0 && fEventsPerSpill < 0.01 ) {
      // fluxes are assumed to be given in units of neutrinos/cm^2/1e20POT/energy 
      // integral over all fluxes removes energy dependence
      // histograms should have bin width that reflects the value of the /energy bit
      // ie if /energy = /50MeV then the bin width should be 50 MeV
      
      // determine product of pot/spill, mass, and cross section
      // events = flux * pot * 10^-38 cm^2 (xsec) * (mass detector (in kg) / nucleon mass (in kg))
      fXSecMassPOT  = 1.e-38*1.e-20;
      fXSecMassPOT *= fPOTPerSpill*(fDetectorMass+fSurroundingMass)/(1.67262158e-27); 

      mf::LogInfo("GENIEHelper") << "Number of events per spill will be based on poisson mean of "
                                 << fXSecMassPOT*fTotalHistFlux;

      fHistEventsPerSpill = gRandom->Poisson(fXSecMassPOT*fTotalHistFlux);
    }

    // set the pot/event counters to zero
    fSpillEvents   = 0;
    fSpillExposure = 0.;
    fTotalExposure = 0.;

    // if the flux driver knows how to keep track of exposure (time,pots)
    // reset it now as some might have been used in determining
    // the geometry maxpathlength or internally scanning for weights

    return;
  }

  //--------------------------------------------------
  void GENIEHelper::InitializeGeometry()
  {
    art::ServiceHandle<geo::Geometry> geo;
    TGeoManager* rootgeom = geo->ROOTGeoManager();
    genie::geometry::ROOTGeomAnalyzer *rgeom = 
      new genie::geometry::ROOTGeomAnalyzer(rootgeom);

    // get the world volume name from the geometry
    fWorldVolume = geo->ROOTGeoManager()->GetTopVolume()->GetName();

    // the detector geometry uses cgs units.
    rgeom->SetLengthUnits(genie::units::centimeter);
    rgeom->SetDensityUnits(genie::units::gram_centimeter3);
    rgeom->SetTopVolName(fTopVolume.c_str());
    rgeom->SetMixtureWeightsSum(1.);

    //  casting to the GENIE geometry driver interface
    fGeomD        = rgeom; // dynamic_cast<genie::GeomAnalyzerI *>(rgeom);
    InitializeFiducialSelection();

    fDetLength    = geo->DetLength();  
    fDetectorMass = geo->TotalMass(fTopVolume.c_str());

    return;
  }

  //--------------------------------------------------
  void GENIEHelper::InitializeFiducialSelection()
  {
    genie::GeomAnalyzerI* geom_driver = fGeomD; // GENIEHelper name -> gNuMIExptEvGen name
    std::string fidcut = fFiducialCut;   // ditto

    if( fidcut.find_first_not_of(" \t\n") != 0) // trim any leading whitespace
      fidcut.erase( 0, fidcut.find_first_not_of(" \t\n")  );

    // convert string to lowercase
    std::transform(fidcut.begin(),fidcut.end(),fidcut.begin(),::tolower);

    if ( "" == fidcut || "none" == fidcut ) return;

    if ( fidcut.find("rock") != string::npos ) {
      // deal with RockBox separately than basic shapes
      InitializeRockBoxSelection();
      return;
    }

    // below is as it is in $GENIE/src/support/numi/EvGen/gNuMIExptEvGen
    // except the change in message logger from log4cpp (GENIE) to cet's MessageLogger used by art

    ///
    /// User defined fiducial volume cut
    ///      [0][M]<SHAPE>:val1,val2,...
    ///   "0" means reverse the cut (i.e. exclude the volume)
    ///   "M" means the coordinates are given in the ROOT geometry
    ///       "master" system and need to be transformed to "top vol" system
    ///   <SHAPE> can be any of "zcyl" "box" "zpoly" "sphere"
    ///       [each takes different # of args]
    ///   This must be followed by a ":" and a list of values separated by punctuation
    ///       (allowed separators: commas , parentheses () braces {} or brackets [] )
    ///   Value mapping:
    ///      zcly:x0,y0,radius,zmin,zmax           - cylinder along z at (x0,y0) capped at z's
    ///      box:xmin,ymin,zmin,xmax,ymax,zmax     - box w/ upper & lower extremes
    ///      zpoly:nfaces,x0,y0,r_in,phi,zmin,zmax - nfaces sided polygon in x-y plane
    //       sphere:x0,y0,z0,radius                - sphere of fixed radius at (x0,y0,z0)
    ///   Examples:    
    ///      1) 0mbox:0,0,0.25,1,1,8.75
    ///         exclude (i.e. reverse) a box in master coordinates w/ corners (0,0,0.25) (1,1,8.75)
    ///      2) mzpoly:6,(2,-1),1.75,0,{0.25,8.75}
    ///         six sided polygon in x-y plane, centered at x,y=(2,-1) w/ inscribed radius 1.75
    ///         no rotation (so first face is in y-z plane +r from center, i.e. hex sits on point)
    ///         limited to the z range of {0.25,8.75} in the master ROOT geom coordinates
    ///      3) zcly:(3,4),5.5,-2,10
    ///         a cylinder oriented parallel to the z axis in the "top vol" coordinates
    ///         at x,y=(3,4) with radius 5.5 and z range of {-2,10}
    ///
    genie::geometry::ROOTGeomAnalyzer * rgeom = 
      dynamic_cast<genie::geometry::ROOTGeomAnalyzer *>(geom_driver);
    if ( ! rgeom ) {
      mf::LogWarning("GENIEHelpler")
        << "Can not create GeomVolSelectorFiduction,"
        << " geometry driver is not ROOTGeomAnalyzer";
      return;
    }

    mf::LogInfo("GENIEHelper") << "fiducial cut: " << fidcut;

    // for now, only fiducial no "rock box"
    genie::geometry::GeomVolSelectorFiducial* fidsel =
      new genie::geometry::GeomVolSelectorFiducial();

    fidsel->SetRemoveEntries(true);  // drop segments that won't be considered

    vector<string> strtok = genie::utils::str::Split(fidcut,":");
    if ( strtok.size() != 2 ) {
      mf::LogWarning("GENIEHelper")
        << "Can not create GeomVolSelectorFiduction,"
        << " no \":\" separating type from values.  nsplit=" << strtok.size();
      for ( unsigned int i=0; i < strtok.size(); ++i )
        mf::LogWarning("GENIEHelper")
          << "strtok[" << i << "] = \"" << strtok[i] << "\"";
      return;
    }

    // parse out optional "x" and "m"
    string stype = strtok[0];
    bool reverse = ( stype.find("0") != string::npos );
    bool master  = ( stype.find("m") != string::npos );  // action after values are set

    // parse out values
    vector<double> vals;
    vector<string> valstrs = genie::utils::str::Split(strtok[1]," ,;(){}[]");
    vector<string>::const_iterator iter = valstrs.begin();
    for ( ; iter != valstrs.end(); ++iter ) {
      const string& valstr1 = *iter;
      if ( valstr1 != "" ) vals.push_back(atof(valstr1.c_str()));
    }
    size_t nvals = vals.size();
    // pad it out to at least 7 entries to avoid index issues if used
    for ( size_t nadd = 0; nadd < 7-nvals; ++nadd ) vals.push_back(0);
    
    //std::cout << "ivals = [";
    //for (unsigned int i=0; i < nvals; ++i) {
    //  if (i>0) cout << ",";
    //  std::cout << vals[i];
    //}
    //std::cout << "]" << std::endl;
    
    // std::vector elements are required to be adjacent so we can treat address as ptr
    
    if        ( stype.find("zcyl")   != string::npos ) {
      // cylinder along z direction at (x0,y0) radius zmin zmax
      if ( nvals < 5 ) 
        mf::LogError("GENIEHelper") << "MakeZCylinder needs 5 values, not " << nvals
                                    << " fidcut=\"" << fidcut << "\"";
      fidsel->MakeZCylinder(vals[0],vals[1],vals[2],vals[3],vals[4]);

    } else if ( stype.find("box")    != string::npos ) {
      // box (xmin,ymin,zmin) (xmax,ymax,zmax)
      if ( nvals < 6 ) 
        mf::LogError("GENIEHelper") << "MakeBox needs 6 values, not " << nvals
                                    << " fidcut=\"" << fidcut << "\"";
      double xyzmin[3] = { vals[0], vals[1], vals[2] };
      double xyzmax[3] = { vals[4], vals[5], vals[5] };
      fidsel->MakeBox(xyzmin,xyzmax);

    } else if ( stype.find("zpoly")  != string::npos ) {
      // polygon along z direction nfaces at (x0,y0) radius phi zmin zmax
      if ( nvals < 7 ) 
        mf::LogError("GENIEHelper") << "MakeZPolygon needs 7 values, not " << nvals
                                    << " fidcut=\"" << fidcut << "\"";
      int nfaces = (int)vals[0];
      if ( nfaces < 3 ) 
        mf::LogError("GENIEHelper") << "MakeZPolygon needs nfaces>=3, not " << nfaces
                                    << " fidcut=\"" << fidcut << "\"";
      fidsel->MakeZPolygon(nfaces,vals[1],vals[2],vals[3],vals[4],vals[5],vals[6]);

    } else if ( stype.find("sphere") != string::npos ) {
      // sphere at (x0,y0,z0) radius 
      if ( nvals < 4 ) 
        mf::LogError("GENIEHelper") << "MakeZSphere needs 4 values, not " << nvals
                                    << " fidcut=\"" << fidcut << "\"";
      fidsel->MakeSphere(vals[0],vals[1],vals[2],vals[3]);

    } else {
      mf::LogError("GENIEHelper")
        << "Can not create GeomVolSelectorFiduction for shape \"" << stype << "\"";
    }

    if ( master  ) {
      fidsel->ConvertShapeMaster2Top(rgeom);
      mf::LogInfo("GENIEHelper") << "Convert fiducial volume from master to topvol coords";
    }
    if ( reverse ) {
      fidsel->SetReverseFiducial(true);
      mf::LogInfo("GENIEHelper") << "Reverse sense of fiducial volume cut";
    }
    
    rgeom->AdoptGeomVolSelector(fidsel);

  }

  //--------------------------------------------------
  void GENIEHelper::InitializeRockBoxSelection()
  {
    genie::GeomAnalyzerI* geom_driver = fGeomD; // GENIEHelper name -> gNuMIExptEvGen name
    std::string fidcut = fFiducialCut;   // ditto

    if( fidcut.find_first_not_of(" \t\n") != 0) // trim any leading whitespace
      fidcut.erase( 0, fidcut.find_first_not_of(" \t\n")  );

    // convert string to lowercase
    std::transform(fidcut.begin(),fidcut.end(),fidcut.begin(),::tolower);

    genie::geometry::ROOTGeomAnalyzer * rgeom = 
      dynamic_cast<genie::geometry::ROOTGeomAnalyzer *>(geom_driver);
    if ( ! rgeom ) {
      mf::LogWarning("GENIEHelpler")
        << "Can not create GeomVolSelectorRockBox,"
        << " geometry driver is not ROOTGeomAnalyzer";
      return;
    }

    mf::LogInfo("GENIEHelper") << "fiducial (rock) cut: " << fidcut;

    // for now, only fiducial no "rock box"
    genie::geometry::GeomVolSelectorRockBox* rocksel =
      new genie::geometry::GeomVolSelectorRockBox();

    vector<string> strtok = genie::utils::str::Split(fidcut,":");
    if ( strtok.size() != 2 ) {
      mf::LogWarning("GENIEHelper")
        << "Can not create GeomVolSelectorRockBox,"
        << " no \":\" separating type from values.  nsplit=" << strtok.size();
      for ( unsigned int i=0; i < strtok.size(); ++i )
        mf::LogWarning("GENIEHelper")
          << "strtok[" << i << "] = \"" << strtok[i] << "\"";
      return;
    }

    string stype = strtok[0];

    // parse out values
    vector<double> vals;
    vector<string> valstrs = genie::utils::str::Split(strtok[1]," ,;(){}[]");
    vector<string>::const_iterator iter = valstrs.begin();
    for ( ; iter != valstrs.end(); ++iter ) {
      const string& valstr1 = *iter;
      if ( valstr1 != "" ) vals.push_back(atof(valstr1.c_str()));
    }
    size_t nvals = vals.size();

    rocksel->SetRemoveEntries(true);  // drop segments that won't be considered

    // assume coordinates are in the *master* (not "top volume") system
    // need to set fTopVolume to fWorldVolume as Sample() will keep setting it
    fTopVolume = fWorldVolume;
    rgeom->SetTopVolName(fTopVolume.c_str());

    if ( nvals < 6 ) {
      mf::LogError("GENIEHelper") << "rockbox needs at least 6 values, found " 
                                  << nvals << "in \"" << strtok[1] << "\"";
      assert( nvals >= 6 );
    }
    double xyzmin[3] = { vals[0], vals[1], vals[2] };
    double xyzmax[3] = { vals[3], vals[4], vals[5] };

    bool   rockonly  = true;
    double wallmin   = 800.;   // geometry in cm, ( 8 meter buffer)
    double dedx      = 2.5 * 1.7e-3; // GeV/cm, rho=2.5, 1.7e-3 ~ rock like loss
    double fudge     = 1.05;

    if ( nvals >=  7 ) rockonly = vals[6];
    if ( nvals >=  8 ) wallmin  = vals[7];
    if ( nvals >=  9 ) dedx     = vals[8];
    if ( nvals >= 10 ) fudge    = vals[9];

    rocksel->SetRockBoxMinimal(xyzmin,xyzmax);
    rocksel->SetMinimumWall(wallmin);
    rocksel->SetDeDx(dedx/fudge);

    // if not rock-only then make a tiny exclusion bubble
    // call to MakeBox shouldn't be necessary
    //  should be done by SetRockBoxMinimal but for some GENIE versions isn't
    if ( ! rockonly ) rocksel->MakeSphere(0,0,0,1.0e-10);
    else              rocksel->MakeBox(xyzmin,xyzmax); 

    rgeom->AdoptGeomVolSelector(rocksel);
  }

  //--------------------------------------------------
  void GENIEHelper::InitializeFluxDriver()
  {

    if(fFluxType.compare("ntuple") == 0){

      genie::flux::GNuMIFlux* numiFlux = new genie::flux::GNuMIFlux();
      std::set<string>::iterator fluxfileitrntuple = fFluxFiles.begin();
      numiFlux->LoadBeamSimData(*fluxfileitrntuple, fDetLocation);
    
      // initialize to only use neutrino flavors requested by user
      genie::PDGCodeList probes;
      for(std::set<int>::iterator flvitr = fGenFlavors.begin(); flvitr != fGenFlavors.end(); flvitr++)
        probes.push_back(*flvitr);
      numiFlux->SetFluxParticles(probes);

      if ( TMath::Abs(fFluxUpstreamZ) < 1.0e30 ) numiFlux->SetUpstreamZ(fFluxUpstreamZ);

      // set the number of cycles to run
      // +++++++++this is stupid - to really set it i have to get a 
      // value from the MCJDriver and i am not even sure what i have 
      // below is approximately correct.
      // for now just run on a set number of events that is kept track of 
      // in the sample method
      //  numiFlux->SetNumOfCycles(int(fPOT/fFluxNormalization));
    
      fFluxD = numiFlux; // dynamic_cast<genie::GFluxI *>(numiFlux);
    } //end if using ntuple flux files
    else if(fFluxType.compare("simple_flux")==0){

#ifdef MISSING_GSIMPLENTPFLUX
      mf::LogError("GENIEHelper") << "Not built with GSimpleNtpFlux enabled";
      assert(0);
#else
      genie::flux::GSimpleNtpFlux* simpleFlux = 
        new genie::flux::GSimpleNtpFlux();
      std::set<string>::iterator fluxfileitrsimple = fFluxFiles.begin();
      simpleFlux->LoadBeamSimData(*fluxfileitrsimple, fDetLocation);

      // initialize to only use neutrino flavors requested by user
      genie::PDGCodeList probes;
      for(std::set<int>::iterator flvitr = fGenFlavors.begin(); flvitr != fGenFlavors.end(); flvitr++)
        probes.push_back(*flvitr);
      simpleFlux->SetFluxParticles(probes);

      if ( TMath::Abs(fFluxUpstreamZ) < 1.0e30 ) simpleFlux->SetUpstreamZ(fFluxUpstreamZ);

      fFluxD = simpleFlux; // dynamic_cast<genie::GFluxI *>(simpleFlux);
#endif    
    
    } //end if using simple_flux flux files
    else if(fFluxType.compare("histogram") == 0){

      genie::flux::GCylindTH1Flux* histFlux = new genie::flux::GCylindTH1Flux();
    
      // now add the different fluxes - fluxes were added to the vector in the same 
      // order that the flavors appear in fGenFlavors
      int ctr = 0;
      for(std::set<int>::iterator i = fGenFlavors.begin(); i != fGenFlavors.end(); i++){
        histFlux->AddEnergySpectrum(*i, fFluxHistograms[ctr]);
        ++ctr;
      } //end loop to add flux histograms to driver

      histFlux->SetNuDirection(fBeamDirection);
      histFlux->SetBeamSpot(fBeamCenter);
      histFlux->SetTransverseRadius(fBeamRadius);
    
      fFluxD = histFlux; // dynamic_cast<genie::GFluxI *>(histFlux);
    } //end if using a histogram
    else if(fFluxType.compare("mono") == 0){

      // weight each species equally in the generation
      double weight = 1./(1.*fGenFlavors.size());
      //make a map of pdg to weight codes
      std::map<int, double> pdgwmap;
      for(std::set<int>::iterator i = fGenFlavors.begin(); i != fGenFlavors.end(); i++)
        pdgwmap[*i] = weight;

      genie::flux::GMonoEnergeticFlux *monoflux = new genie::flux::GMonoEnergeticFlux(fMonoEnergy, pdgwmap);
      monoflux->SetDirectionCos(fBeamDirection.X(), fBeamDirection.Y(), fBeamDirection.Z());
      monoflux->SetRayOrigin(fBeamCenter.X(), fBeamCenter.Y(), fBeamCenter.Z());
      fFluxD = monoflux; // dynamic_cast<genie::GFluxI *>(monoflux);
    } //end if using monoenergetic beam


    //Using the atmospheric fluxes
    else if(fFluxType.compare("atmo_FLUKA") == 0 || fFluxType.compare("atmo_BARTOL") == 0){

      // Instantiate appropriate concrete flux driver
      genie::flux::GAtmoFlux *atmo_flux_driver = 0;
      
      if(fFluxType.compare("atmo_FLUKA") == 0) {
        genie::flux::GFlukaAtmo3DFlux * fluka_flux = new genie::flux::GFlukaAtmo3DFlux;
        atmo_flux_driver = dynamic_cast<genie::flux::GAtmoFlux *>(fluka_flux);
      }
      if(fFluxType.compare("atmo_BARTOL") == 0) {
        genie::flux::GBartolAtmoFlux * bartol_flux = new genie::flux::GBartolAtmoFlux;
        atmo_flux_driver = dynamic_cast<genie::flux::GAtmoFlux *>(bartol_flux);
      } 
      
      atmo_flux_driver->ForceMinEnergy(fAtmoEmin);
      atmo_flux_driver->ForceMaxEnergy(fAtmoEmax);
      
      int ctrfv = 0;
      int ctrff = 0;
      for(std::set<int>::iterator flvitr = fGenFlavors.begin(); flvitr != fGenFlavors.end(); flvitr++){
        for(std::set<string>::iterator ffitr = fFluxFiles.begin(); ffitr != fFluxFiles.end(); ffitr++){
          if(ctrfv == ctrff){
            mf::LogInfo("GENIEHelper") << "FLAVOR: " << *flvitr << "  FLUX FILE: " <<  *ffitr;

            atmo_flux_driver->SetFluxFile(*flvitr, *ffitr);
            ctrff++;
          } 
          ctrfv++;
        }
      }    
      
      atmo_flux_driver->LoadFluxData();
      
      // configure flux generation surface:
      atmo_flux_driver->SetRadii(fAtmoRl, fAtmoRt);
            
      fFluxD = atmo_flux_driver;//dynamic_cast<genie::GFluxI *>(atmo_flux_driver);
    }//end if using atmospheric fluxes


    //
    // Is the user asking to do flavor mixing?
    //
    fFluxD2GMCJD = fFluxD;  // default: genie's GMCJDriver uses the bare flux generator
    if( fMixerConfig.find_first_not_of(" \t\n") != 0) // trim any leading whitespace
      fMixerConfig.erase( 0, fMixerConfig.find_first_not_of(" \t\n")  );
    std::string keyword = fMixerConfig.substr(0,fMixerConfig.find_first_of(" \t\n"));
    if ( keyword != "none" ) {
      // Wrap the true flux driver up in the adapter to allow flavor mixing
      genie::flux::GFlavorMixerI* mixer = 0;
      // here is where we map MixerConfig string keyword to actual class
      // first is a special case that is part of GENIE proper
      if ( keyword == "map" || keyword == "swap" || keyword == "fixedfrac" )
        mixer = new genie::flux::GFlavorMap();
#ifdef FLAVORMIXERFACTORY
      // if it wasn't one of the predefined known mixers then
      // see if the factory knows about it and can create one
      // assuming the keyword (first token) is the class name
      if ( ! mixer ) {
        genie::flux::GFlavorMixerFactory& mixerFactory = 
          genie::flux::GFlavorMixerFactory::Instance();
        mixer = mixerFactory.GetFlavorMixer(keyword);
        if ( mixer ) {
          // remove class name from config string
          fMixerConfig.erase(0,keyword.size()); 
          // trim any leading whitespace
          if ( fMixerConfig.find_first_not_of(" \t\n") != 0 )
            fMixerConfig.erase( 0, fMixerConfig.find_first_not_of(" \t\n")  );
        } else {
          const std::vector<std::string>& knownMixers = 
            mixerFactory.AvailableFlavorMixers();
          mf::LogWarning("GENIEHelper")
            << " GFlavorMixerFactory known mixers: ";
          for (unsigned int j=0; j < knownMixers.size(); ++j ) {
            mf::LogWarning("GENIEHelper")
              << "   [" << std::setw(2) << j << "]  " << knownMixers[j];
          }
        }
      }
#endif
      // configure the mixer
      if ( mixer ) mixer->Config(fMixerConfig);
      else {
        mf::LogWarning("GENIEHelper") 
          << "GENIEHelper MixerConfig keyword was \"" << keyword
          << "\" but that did not map to a class; " << std::endl
          << "GFluxBlender in use, but no mixer";
      }

      genie::GFluxI* realFluxD = fFluxD;
      genie::flux::GFluxBlender* blender = new genie::flux::GFluxBlender();
      blender->SetBaselineDist(fMixerBaseline);
      blender->AdoptFluxGenerator(realFluxD);
      blender->AdoptFlavorMixer(mixer);
      fFluxD2GMCJD = blender;
      if ( fDebugFlags & 0x01 ) {
        if ( mixer ) mixer->PrintConfig();
        blender->PrintConfig();
        std::cout << std::flush;
      }
    }

    return;
  }

  //--------------------------------------------------
  void GENIEHelper::ConfigGeomScan()
  {

    // trim any leading whitespace
    if( fGeomScan.find_first_not_of(" \t\n") != 0) 
      fGeomScan.erase( 0, fGeomScan.find_first_not_of(" \t\n")  );

    if ( fGeomScan.find("default") != std::string::npos ) return;

    genie::geometry::ROOTGeomAnalyzer* rgeom = 
      dynamic_cast<genie::geometry::ROOTGeomAnalyzer*>(fGeomD);

    if ( ! rgeom ) {
      mf::LogError("GENIEHelper") 
        << "fGeomD wasn't of type genie::geometry::ROOTGeomAnalyzer*";
      assert(rgeom);
    }

    // convert string to lowercase
    std::transform(fGeomScan.begin(),fGeomScan.end(),fGeomScan.begin(),::tolower);

    // parse out string
    vector<string> strtok = genie::utils::str::Split(fGeomScan," ");
    // first value is a string, others should be numbers unless "file:"
    string scanmethod = strtok[0];

    if ( scanmethod.find("file") != std::string::npos ) {
      // xml expand path before passing in
      string filename = strtok[1];
      string fullname = genie::utils::xml::GetXMLFilePath(filename);
      fDriver->UseMaxPathLengths(fullname);
      mf::LogInfo("GENIEHelper") 
        << "ConfigGeomScan getting MaxPathLengths from \"" << fullname << "\"";
      return;
    }

    vector<double> vals;
    for ( size_t indx=1; indx < strtok.size(); ++indx ) {
      const string& valstr1 = strtok[indx];
      if ( valstr1 != "" ) vals.push_back(atof(valstr1.c_str()));
    }
    size_t nvals = vals.size();
    // pad it out to at least 4 entries to avoid index issues
    for ( size_t nadd = 0; nadd < 4-nvals; ++nadd ) vals.push_back(0);

    double safetyfactor = 0;
    int    writeout = 0;
    if (        scanmethod.find("box") != std::string::npos ) {
      // use box method
      int np = (int)vals[0];
      int nr = (int)vals[1];
      if ( nvals >= 3 ) safetyfactor = vals[2];
      if ( nvals >= 4 ) writeout     = vals[3];
      // protect against too small values
      if ( np <= 10 ) np = rgeom->ScannerNPoints();
      if ( nr <= 10 ) nr = rgeom->ScannerNRays();
      mf::LogInfo("GENIEHelper") 
        << "ConfigGeomScan scan using box " << np << " points, "
        << nr << " rays";
      rgeom->SetScannerNPoints(np);
      rgeom->SetScannerNRays(nr);
    } else if ( scanmethod.find("flux") != std::string::npos ) {
      // use flux method
      int np = (int)vals[0];
      if ( nvals >= 2 ) safetyfactor = vals[1];
      if ( nvals >= 3 ) writeout     = vals[2];
      if ( np <= 10 ) np = rgeom->ScannerNParticles();
      mf::LogInfo("GENIEHelper") 
        << "ConfigGeomScan scan using flux " << np << " particles ";
      rgeom->SetScannerFlux(fFluxD);
      rgeom->SetScannerNParticles(np);
    } else {
      // unknown
      mf::LogError("GENIEHelper") 
        << "fGeomScan unknown method: \"" << fGeomScan << "\"";
      assert(0);
    }
    if ( safetyfactor > 0 ) {
      mf::LogInfo("GENIEHelper") 
        << "ConfigGeomScan setting safety factor to " << safetyfactor;
      rgeom->SetMaxPlSafetyFactor(safetyfactor);
    }
    if ( writeout != 0 ) SetMaxPathOutInfo();
  }

  //--------------------------------------------------
  void GENIEHelper::SetMaxPathOutInfo()
  {
    // create an info string based on:
    // ROOT geometry, TopVolume, FiducialCut, GeomScan, Flux

    mf::LogInfo("GENIEHelper") 
        << "about to create MaxPathOutInfo";

    art::ServiceHandle<geo::Geometry> geo;
    fMaxPathOutInfo = "\n";
    fMaxPathOutInfo += "   FluxType:     " + fFluxType + "\n";
    fMaxPathOutInfo += "   BeamName:     " + fBeamName + "\n";
    fMaxPathOutInfo += "   FluxFiles:    ";
    std::set<string>::iterator ffitr = fFluxFiles.begin();
    for ( ; ffitr != fFluxFiles.end() ; ++ffitr )
      fMaxPathOutInfo += "\n         " + *ffitr;
    fMaxPathOutInfo += "\n";
    fMaxPathOutInfo += "   DetLocation:  " + fDetLocation + "\n";
    fMaxPathOutInfo += "   ROOTFile:     " + geo->ROOTFile() + "\n";
    fMaxPathOutInfo += "   WorldVolume:  " + fWorldVolume + "\n";
    fMaxPathOutInfo += "   TopVolume:    " + fTopVolume + "\n";
    fMaxPathOutInfo += "   FiducialCut:  " + fFiducialCut + "\n";
    fMaxPathOutInfo += "   GeomScan:     " + fGeomScan + "\n";

    mf::LogInfo("GENIEHelper") 
        << "MaxPathOutInfo: \"" << fMaxPathOutInfo << "\"";

  }

  //--------------------------------------------------
  bool GENIEHelper::Stop()
  {
    //   std::cout << "in GENIEHelper::Stop(), fEventsPerSpill = " << fEventsPerSpill
    //      << " fPOTPerSpill = " << fPOTPerSpill << " fSpillExposure = " << fSpillExposure 
    //      << " fSpillEvents = " << fSpillEvents
    //      << " fHistEventsPerSpill = " << fHistEventsPerSpill << std::endl;

    // determine if we should keep throwing neutrinos for 
    // this spill or move on

    if(fFluxType.compare("atmo_FLUKA") == 0 || fFluxType.compare("atmo_BARTOL") == 0){
      if((fEventsPerSpill > 0) && (fSpillEvents < fEventsPerSpill)){
        return false;
      }
    }

    else if(fEventsPerSpill > 0){
      if(fSpillEvents < fEventsPerSpill) 
        return false;
    }
    else{
      if( ( fFluxType.compare("ntuple")      == 0 || 
            fFluxType.compare("simple_flux") == 0    ) && 
          fSpillExposure < fPOTPerSpill) return false;
      else if(fFluxType.compare("histogram") == 0){
        if(fSpillEvents < fHistEventsPerSpill) return false;
        else fSpillExposure = fPOTPerSpill;
      }
    }

    // made it to here, means need to reset the counters

    if(fFluxType.compare("atmo_FLUKA") == 0 || fFluxType.compare("atmo_BARTOL") == 0){
      //the exposure for atmo is in SECONDS. In order to get seconds, it needs to 
      //be normalized by 1e4 to take into account the units discrepency between 
      //AtmoFluxDriver(/m2) and Generate(/cm2) and it need to be normalized by 
      //the generation surface area since it's not taken into accoutn in the flux driver
      fTotalExposure = (1e4 * (dynamic_cast<genie::flux::GAtmoFlux *>(fFluxD)->NFluxNeutrinos())) / (TMath::Pi() * fAtmoRt*fAtmoRt);
      
      LOG_DEBUG("GENIEHelper") << "===> Atmo EXPOSURE = " << fTotalExposure << " seconds";
    }

    else{
      fTotalExposure += fSpillExposure;
    }

    fSpillEvents   = 0;
    fSpillExposure = 0.;
    fHistEventsPerSpill = gRandom->Poisson(fXSecMassPOT*fTotalHistFlux);
    return true;
  }

  //--------------------------------------------------
  bool GENIEHelper::Sample(simb::MCTruth &truth, simb::MCFlux  &flux, simb::GTruth &gtruth)
  {
    // set the top volume for the geometry
    art::ServiceHandle<geo::Geometry> geo;
    geo->ROOTGeoManager()->SetTopVolume(geo->ROOTGeoManager()->FindVolumeFast(fTopVolume.c_str()));
    
    if ( fGenieEventRecord ) delete fGenieEventRecord;
    fGenieEventRecord = fDriver->GenerateEvent();

    // now check if we produced a viable event record
    bool viableInteraction = true;
    if ( ! fGenieEventRecord ) viableInteraction = false;

    // update the spill total information, then check to see 
    // if we got an event record that was valid

    // pack the flux information
    if(fFluxType.compare("ntuple") == 0){
      fSpillExposure = (dynamic_cast<genie::flux::GNuMIFlux *>(fFluxD)->UsedPOTs()/fDriver->GlobProbScale() - fTotalExposure);
      flux.fFluxType = simb::kNtuple;
      PackNuMIFlux(flux);
    }
    else if ( fFluxType.compare("simple_flux")==0 ) {
 
#ifdef MISSING_GSIMPLENTPFLUX
      mf::LogError("GENIEHelper") << "Not built with GSimpleNtpFlux enabled";
      assert(0);
#else
      // pack the flux information
      fSpillExposure = (dynamic_cast<genie::flux::GSimpleNtpFlux *>(fFluxD)->UsedPOTs()/fDriver->GlobProbScale() - fTotalExposure);
#endif
      flux.fFluxType = simb::kSimple_Flux;
      PackSimpleFlux(flux);
    }

    // if no interaction generated return false
    if(!viableInteraction) return false;
    
    // fill the MC truth information as we have a good interaction
    PackMCTruth(fGenieEventRecord,truth); 
    // fill the Generator (genie) truth information
    PackGTruth(fGenieEventRecord, gtruth);
    
    // check to see if we are using flux ntuples but want to 
    // make n events per spill
    if(fEventsPerSpill > 0 &&
       (fFluxType.compare("ntuple") == 0 ||
        fFluxType.compare("simple_flux") == 0)
       ) ++fSpillEvents;

    // now check if using either histogram or mono fluxes, using
    // either n events per spill or basing events on POT per spill for the
    // histogram case
    if(fFluxType.compare("histogram") == 0){
      // set the flag in the parent object that says the 
      // fluxes came from histograms and fill related values
      flux.fFluxType = simb::kHistPlusFocus;

      // save the fluxes - fluxes were added to the vector in the same 
      // order that the flavors appear in fGenFlavors
      int ctr = 0;
      int bin = fFluxHistograms[0]->FindBin(truth.GetNeutrino().Nu().E());
      std::vector<double> fluxes(6, 0.);
      for(std::set<int>::iterator i = fGenFlavors.begin(); i != fGenFlavors.end(); i++){
        if(*i ==  12) fluxes[kNue]      = fFluxHistograms[ctr]->GetBinContent(bin);
        if(*i == -12) fluxes[kNueBar]   = fFluxHistograms[ctr]->GetBinContent(bin);
        if(*i ==  14) fluxes[kNuMu]     = fFluxHistograms[ctr]->GetBinContent(bin);
        if(*i == -14) fluxes[kNuMuBar]  = fFluxHistograms[ctr]->GetBinContent(bin);
        if(*i ==  16) fluxes[kNuTau]    = fFluxHistograms[ctr]->GetBinContent(bin);
        if(*i == -16) fluxes[kNuTauBar] = fFluxHistograms[ctr]->GetBinContent(bin);
        ++ctr;
      }

      // get the flux for each neutrino flavor of this energy
      flux.SetFluxGen(fluxes[kNue],   fluxes[kNueBar],
                      fluxes[kNuMu],  fluxes[kNuMuBar],
                      fluxes[kNuTau], fluxes[kNuTauBar]);
    
      ++fSpillEvents;
    }
    else if(fFluxType.compare("mono") == 0){
      ++fSpillEvents;
    }

    else if(fFluxType.compare("atmo_FLUKA") == 0 || fFluxType.compare("atmo_BARTOL") == 0){
      if(fEventsPerSpill > 0) ++fSpillEvents;
      flux.fFluxType = simb::kHistPlusFocus;
    }


    // fill these after the Pack[NuMI|Simple]Flux because those
    // will Reset() the values at the start
    TLorentzVector *vertex = fGenieEventRecord->Vertex();
    TLorentzVector nuray_pos = fFluxD->Position();
    TVector3 ray2vtx = nuray_pos.Vect() - vertex->Vect();
    flux.fgenx    = nuray_pos.X();
    flux.fgeny    = nuray_pos.Y();
    flux.fgenz    = nuray_pos.Z();
    flux.fgen2vtx = ray2vtx.Mag();

    genie::flux::GFluxBlender* blender = 
      dynamic_cast<genie::flux::GFluxBlender*>(fFluxD2GMCJD);
    if ( blender ) { 
      flux.fdk2gen = blender->TravelDist();
      // / if mixing flavors print the state of the blender
      if ( fDebugFlags & 0x02 ) blender->PrintState();
    }

    if ( fDebugFlags & 0x04 ) {
      mf::LogInfo("GENIEHelper") << "vertex loc " << vertex->X() << "," 
                                 << vertex->Y() << "," << vertex->Z() << std::endl 
                                 << " flux ray start " << nuray_pos.X() << ","
                                 << nuray_pos.Y() << "," << nuray_pos.Z() << std::endl
                                 << " ray2vtx = " << flux.fgen2vtx
                                 << " dk2ray = " << flux.fdk2gen;
    }

    // set the top volume of the geometry back to the world volume
    geo->ROOTGeoManager()->SetTopVolume(geo->ROOTGeoManager()->FindVolumeFast(fWorldVolume.c_str()));

    return true;
  }

  //--------------------------------------------------
  void GENIEHelper::PackNuMIFlux(simb::MCFlux &flux)
  {
    flux.Reset();

    // cast the fFluxD pointer to be of the right type
    genie::flux::GNuMIFlux *gnf = dynamic_cast<genie::flux::GNuMIFlux *>(fFluxD);
    const genie::flux::GNuMIFluxPassThroughInfo& nflux = gnf->PassThroughInfo();

    // check the particle codes and the units passed through
    //  nflux.pcodes: 0=original GEANT particle codes, 1=converted to PDG
    //  nflux.units:  0=original GEANT cm, 1=meters
    if(nflux.pcodes != 1 && nflux.units != 0)
      mf::LogWarning("GENIEHelper") << "either wrong particle codes or units "
                                    << "from flux object - beware!!";

    // maintained variable names from gnumi ntuples
    // see http://www.hep.utexas.edu/~zarko/wwwgnumi/v19/[/v19/output_gnumi.html]

    flux.frun      = nflux.run;
    flux.fevtno    = nflux.evtno;
    flux.fndxdz    = nflux.ndxdz;
    flux.fndydz    = nflux.ndydz;
    flux.fnpz      = nflux.npz;
    flux.fnenergy  = nflux.nenergy;
    flux.fndxdznea = nflux.ndxdznea;
    flux.fndydznea = nflux.ndydznea;
    flux.fnenergyn = nflux.nenergyn;
    flux.fnwtnear  = nflux.nwtnear;
    flux.fndxdzfar = nflux.ndxdzfar;
    flux.fndydzfar = nflux.ndydzfar;
    flux.fnenergyf = nflux.nenergyf;
    flux.fnwtfar   = nflux.nwtfar;
    flux.fnorig    = nflux.norig;
    flux.fndecay   = nflux.ndecay;
    flux.fntype    = nflux.ntype;
    flux.fvx       = nflux.vx;
    flux.fvy       = nflux.vy;
    flux.fvz       = nflux.vz;
    flux.fpdpx     = nflux.pdpx;
    flux.fpdpy     = nflux.pdpy;
    flux.fpdpz     = nflux.pdpz;
    flux.fppdxdz   = nflux.ppdxdz;
    flux.fppdydz   = nflux.ppdydz;
    flux.fpppz     = nflux.pppz;
    flux.fppenergy = nflux.ppenergy;
    flux.fppmedium = nflux.ppmedium;
    flux.fptype    = nflux.ptype;     // converted to PDG
    flux.fppvx     = nflux.ppvx;
    flux.fppvy     = nflux.ppvy;
    flux.fppvz     = nflux.ppvz;
    flux.fmuparpx  = nflux.muparpx;
    flux.fmuparpy  = nflux.muparpy;
    flux.fmuparpz  = nflux.muparpz;
    flux.fmupare   = nflux.mupare;
    flux.fnecm     = nflux.necm;
    flux.fnimpwt   = nflux.nimpwt;
    flux.fxpoint   = nflux.xpoint;
    flux.fypoint   = nflux.ypoint;
    flux.fzpoint   = nflux.zpoint;
    flux.ftvx      = nflux.tvx;
    flux.ftvy      = nflux.tvy;
    flux.ftvz      = nflux.tvz;
    flux.ftpx      = nflux.tpx;
    flux.ftpy      = nflux.tpy;
    flux.ftpz      = nflux.tpz;
    flux.ftptype   = nflux.tptype;   // converted to PDG
    flux.ftgen     = nflux.tgen;
    flux.ftgptype  = nflux.tgptype;  // converted to PDG
    flux.ftgppx    = nflux.tgppx;
    flux.ftgppy    = nflux.tgppy;
    flux.ftgppz    = nflux.tgppz;
    flux.ftprivx   = nflux.tprivx;
    flux.ftprivy   = nflux.tprivy;
    flux.ftprivz   = nflux.tprivz;
    flux.fbeamx    = nflux.beamx;
    flux.fbeamy    = nflux.beamy;
    flux.fbeamz    = nflux.beamz;
    flux.fbeampx   = nflux.beampx;
    flux.fbeampy   = nflux.beampy;
    flux.fbeampz   = nflux.beampz;    

    flux.fdk2gen   = gnf->GetDecayDist();

    return;
  }

  //--------------------------------------------------
  void GENIEHelper::PackMCTruth(genie::EventRecord *record,
				simb::MCTruth &truth)
  {
    
    TLorentzVector *vertex = record->Vertex();

    // get the Interaction object from the record - this is the object
    // that talks to the event information objects and is in m
    genie::Interaction *inter = record->Summary();
  
    // get the different components making up the interaction
    const genie::InitialState &initState  = inter->InitState();
    const genie::ProcessInfo  &procInfo   = inter->ProcInfo();
    //const genie::Kinematics   &kine       = inter->Kine();
    //const genie::XclsTag      &exclTag    = inter->ExclTag();
    //const genie::KPhaseSpace  &phaseSpace = inter->PhaseSpace();

    //choose a spill time (ns) to shift the vertex times by:

    double spillTime = fGlobalTimeOffset + gRandom->Uniform()*fRandomTimeOffset;

    // add the particles from the interaction
    TIter partitr(record);
    genie::GHepParticle *part = 0;
    // GHepParticles return units of GeV/c for p.  the V_i are all in fermis
    // and are relative to the center of the struck nucleus.
    // add the vertex X/Y/Z to the V_i for status codes 0 and 1
    int trackid = 0;
    std::string primary("primary");

    while( (part = dynamic_cast<genie::GHepParticle *>(partitr.Next())) ){
    
      --trackid;
      simb::MCParticle tpart(trackid, 
			     part->Pdg(), 
			     primary, 
			     part->FirstMother(), 
			     part->Mass(), 
			     part->Status());


      double vtx[4] = {part->Vx(), part->Vy(), part->Vz(), part->Vt()};
      tpart.SetGvtx(vtx);
      tpart.SetRescatter(part->RescatterCode());
      //std::cerr << "Nate's Modification to particle loop done" << std::endl;
      // set the vertex location for the neutrino, nucleus and everything
      // that is to be tracked.  vertex returns values in meters.
      if(part->Status() == 0 || part->Status() == 1){
	vtx[0] = 100.*(part->Vx()*1.e-15 + vertex->X());
	vtx[1] = 100.*(part->Vy()*1.e-15 + vertex->Y());
	vtx[2] = 100.*(part->Vz()*1.e-15 + vertex->Z());
	vtx[3] = part->Vt() + spillTime;
      }
      TLorentzVector pos(vtx[0], vtx[1], vtx[2], vtx[3]);
      TLorentzVector mom(part->Px(), part->Py(), part->Pz(), part->E());
      tpart.AddTrajectoryPoint(pos,mom);
      if(part->PolzIsSet()) {
	TVector3 polz; 
	part->GetPolarization(polz);
	tpart.SetPolarization(polz);
      }
      truth.Add(tpart);
        
    }// end loop to convert GHepParticles to MCParticles

    // is the interaction NC or CC
    int CCNC = simb::kCC;
    if(procInfo.IsWeakNC()) CCNC = simb::kNC;

    // what is the interaction type
    int mode = simb::kQE;
    if(procInfo.IsDeepInelastic()) mode = simb::kDIS;
    else if(procInfo.IsResonant()) mode = simb::kRes;
    else if(procInfo.IsCoherent()) mode = simb::kCoh;
    
    int itype = simb::kNuanceOffset + genie::utils::ghep::NuanceReactionCode(record);

   
    // set the neutrino information in MCTruth
    truth.SetOrigin(simb::kBeamNeutrino);
    
    // The genie event kinematics are subtle different from the event 
    // kinematics that a experimentalist would calculate
    // Instead of retriving the genie values for these kinematic variables 
    // calcuate them from the the final state particles 
    // while ingnoring the fermi momentum and the off-shellness of the bound nucleon.
    genie::GHepParticle * hitnucl = record->HitNucleon();
    TLorentzVector pdummy(0, 0, 0, 0);
    const TLorentzVector & k1 = *((record->Probe())->P4());
    const TLorentzVector & k2 = *((record->FinalStatePrimaryLepton())->P4());
    //const TLorentzVector & p1 = (hitnucl) ? *(hitnucl->P4()) : pdummy;

    double M  = genie::constants::kNucleonMass; 
    TLorentzVector q  = k1-k2;                     // q=k1-k2, 4-p transfer
    double Q2 = -1 * q.M2();                       // momemtum transfer
    double v  = (hitnucl) ? q.Energy()       : -1; // v (E transfer to the nucleus)
    double x  = (hitnucl) ? 0.5*Q2/(M*v)     : -1; // Bjorken x
    double y  = (hitnucl) ? v/k1.Energy()    : -1; // Inelasticity, y = q*P1/k1*P1
    double W2 = (hitnucl) ? M*M + 2*M*v - Q2 : -1; // Hadronic Invariant mass ^ 2
    double W  = (hitnucl) ? TMath::Sqrt(W2)  : -1; 
    
    truth.SetNeutrino(CCNC, mode, itype,
		      initState.Tgt().Pdg(), 
		      initState.Tgt().HitNucPdg(), 
		      initState.Tgt().HitQrkPdg(),
		      W, x, y, Q2);
    return;
  }
  
  //--------------------------------------------------
  void GENIEHelper::PackGTruth(genie::EventRecord *record, 
			       simb::GTruth &truth) {
    
    //interactions info
    genie::Interaction *inter = record->Summary();
    const genie::ProcessInfo  &procInfo = inter->ProcInfo();
    truth.fGint = (int)procInfo.InteractionTypeId(); 
    truth.fGscatter = (int)procInfo.ScatteringTypeId(); 
     
    //Event info
    truth.fweight = record->Weight(); 
    truth.fprobability = record->Probability(); 
    truth.fXsec = record->XSec(); 
    truth.fDiffXsec = record->DiffXSec(); 

    TLorentzVector vtx;
    TLorentzVector *erVtx = record->Vertex();
    vtx.SetXYZT(erVtx->X(), erVtx->Y(), erVtx->Z(), erVtx->T() );
    truth.fVertex = vtx;
    
    //genie::XclsTag info
    const genie::XclsTag &exclTag = inter->ExclTag();
    truth.fNumPiPlus = exclTag.NPiPlus(); 
    truth.fNumPiMinus = exclTag.NPiMinus();
    truth.fNumPi0 = exclTag.NPi0();    
    truth.fNumProton = exclTag.NProtons(); 
    truth.fNumNeutron = exclTag.NNucleons();
    truth.fIsCharm = exclTag.IsCharmEvent();   
    truth.fResNum = (int)exclTag.Resonance();

    //kinematics info 
    const genie::Kinematics &kine = inter->Kine();
    
    truth.fgQ2 = kine.Q2(true);
    truth.fgq2 = kine.q2(true);
    truth.fgW = kine.W(true);
    if ( kine.KVSet(genie::kKVSelt) ) {
      // only get this if it is set in the Kinematics class
      // to avoid a warning message
      truth.fgT = kine.t(true);
    }
    truth.fgX = kine.x(true);
    truth.fgY = kine.y(true);
    
    /*
    truth.fgQ2 = kine.Q2(false); 
    truth.fgW = kine.W(false);
    truth.fgT = kine.t(false);
    truth.fgX = kine.x(false);
    truth.fgY = kine.y(false);
    */
    truth.fFShadSystP4 = kine.HadSystP4();
    
    //Initial State info
    const genie::InitialState &initState  = inter->InitState();
    truth.fProbePDG = initState.ProbePdg();
    truth.fProbeP4 = *initState.GetProbeP4();
    
    //Target info
    const genie::Target &tgt = initState.Tgt();
    truth.fIsSeaQuark = tgt.HitSeaQrk();
    truth.fHitNucP4 = tgt.HitNucP4();
    truth.ftgtZ = tgt.Z();
    truth.ftgtA = tgt.A();
    truth.ftgtPDG = tgt.Pdg();

    return;

  }

  //----------------------------------------------------------------------
  void GENIEHelper::PackSimpleFlux(simb::MCFlux &flux)
  {
#ifdef MISSING_GSIMPLENTPFLUX
    mf::LogError("GENIEHelper") << "Not built with GSimpleNtpFlux enabled";
    assert(0);
#else
    flux.Reset();

    // cast the fFluxD pointer to be of the right type
    genie::flux::GSimpleNtpFlux *gsf = dynamic_cast<genie::flux::GSimpleNtpFlux *>(fFluxD);
    
    // maintained variable names from gnumi ntuples
    // see http://www.hep.utexas.edu/~zarko/wwwgnumi/v19/[/v19/output_gnumi.html]
    
    const genie::flux::GSimpleNtpEntry* nflux_entry = gsf->GetCurrentEntry();
    const genie::flux::GSimpleNtpNuMI*  nflux_numi  = gsf->GetCurrentNuMI();
    //const genie::flux::GSimpleNtpMeta*  nflux_meta  = gsf->GetCurrentMeta();
  
    flux.fntype  = nflux_entry->pdg;
    flux.fnimpwt = nflux_entry->wgt;

    if ( nflux_numi ) {
      flux.frun      = nflux_numi->run;
      flux.fevtno    = nflux_numi->evtno;
      flux.ftpx      = nflux_numi->tpx;
      flux.ftpy      = nflux_numi->tpy;
      flux.ftpz      = nflux_numi->tpz;
      flux.ftptype   = nflux_numi->tptype;   // converted to PDG
#ifndef GSIMPLE_NUMI_V1
      flux.fvx       = nflux_numi->vx;
      flux.fvy       = nflux_numi->vy;
      flux.fvz       = nflux_numi->vz;
      flux.fndecay   = nflux_numi->ndecay;
      flux.fppmedium = nflux_numi->ppmedium;
#endif
    }
    //   flux.fndxdz    = nflux.ndxdz;
    //   flux.fndydz    = nflux.ndydz;
    //   flux.fnpz      = nflux.npz;
    //   flux.fnenergy  = nflux.nenergy;
    //   flux.fndxdznea = nflux.ndxdznea;
    //   flux.fndydznea = nflux.ndydznea;
    //   flux.fnenergyn = nflux.nenergyn;
    //   flux.fnwtnear  = nflux.nwtnear;
    //   flux.fndxdzfar = nflux.ndxdzfar;
    //   flux.fndydzfar = nflux.ndydzfar;
    //   flux.fnenergyf = nflux.nenergyf;
    //   flux.fnwtfar   = nflux.nwtfar;
    //   flux.fnorig    = nflux.norig;
    //   flux.fndecay   = nflux.ndecay;
    //   flux.fntype    = nflux.ntype;
    //   flux.fvx       = nflux.vx;
    //   flux.fvy       = nflux.vy;
    //   flux.fvz       = nflux.vz;
    //   flux.fpdpx     = nflux.pdpx;
    //   flux.fpdpy     = nflux.pdpy;
    //   flux.fpdpz     = nflux.pdpz;
    //   flux.fppdxdz   = nflux.ppdxdz;
    //   flux.fppdydz   = nflux.ppdydz;
    //   flux.fpppz     = nflux.pppz;
    //   flux.fppenergy = nflux.ppenergy;
    //   flux.fppmedium = nflux.ppmedium;
    //   flux.fptype    = nflux.ptype;     // converted to PDG
    //   flux.fppvx     = nflux.ppvx;
    //   flux.fppvy     = nflux.ppvy;
    //   flux.fppvz     = nflux.ppvz;
    //   flux.fmuparpx  = nflux.muparpx;
    //   flux.fmuparpy  = nflux.muparpy;
    //   flux.fmuparpz  = nflux.muparpz;
    //   flux.fmupare   = nflux.mupare;
    //   flux.fnecm     = nflux.necm;
    //   flux.fnimpwt   = nflux.nimpwt;
    //   flux.fxpoint   = nflux.xpoint;
    //   flux.fypoint   = nflux.ypoint;
    //   flux.fzpoint   = nflux.zpoint;
    //   flux.ftvx      = nflux.tvx;
    //   flux.ftvy      = nflux.tvy;
    //   flux.ftvz      = nflux.tvz;
    //   flux.ftgen     = nflux.tgen;
    //   flux.ftgptype  = nflux.tgptype;  // converted to PDG
    //   flux.ftgppx    = nflux.tgppx;
    //   flux.ftgppy    = nflux.tgppy;
    //   flux.ftgppz    = nflux.tgppz;
    //   flux.ftprivx   = nflux.tprivx;
    //   flux.ftprivy   = nflux.tprivy;
    //   flux.ftprivz   = nflux.tprivz;
    //   flux.fbeamx    = nflux.beamx;
    //   flux.fbeamy    = nflux.beamy;
    //   flux.fbeamz    = nflux.beamz;
    //   flux.fbeampx   = nflux.beampx;
    //   flux.fbeampy   = nflux.beampy;
    //   flux.fbeampz   = nflux.beampz; 
#endif
    
    flux.fdk2gen   = gsf->GetDecayDist();

    return;
  }

  //---------------------------------------------------------
  void GENIEHelper::FindFluxPath(std::string userpattern)
  {
    // Using the the FW_SEARCH_PATH list of directories, apply the
    // user supplied pattern as a suffix to find the flux files.
    // The userpattern might include simple wildcard globs (in contrast 
    // to proper regexp patterns) for the filename part, but not any
    // part of the directory path.  If files are found in more than
    // one FW_SEARCH_PATH alternative take the one with the most files.

    /* was (but only works for single files):
       cet::search_path sp("FW_SEARCH_PATH");
       sp.find_file(pset.get< std::string>("FluxFile"), fFluxFile);
    */

    std::vector<std::string> dirs;
    cet::split_path(cet::getenv("FW_SEARCH_PATH"),dirs);
    if ( dirs.empty() ) dirs.push_back(std::string()); // at least null string 

    // count the number files in each of the distinct alternative paths
    std::map<std::string,size_t> path2n;

    std::vector<std::string>::const_iterator ditr = dirs.begin();
    for ( ; ditr != dirs.end(); ++ditr ) {
      std::string dalt = *ditr;
      // if non-null, does it end with a "/"?  if not add one
      size_t len = dalt.size();
      if ( len > 0 && dalt.rfind('/') != len-1 ) dalt.append("/");

      // GENIE uses 'glob' style wildcards not true regex, i.e. "*" vs ".*"

      std::string filepatt = dalt + userpattern;

      // !WILDCARD only works for file name ... NOT directory                       
      string dirname = gSystem->UnixPathName(gSystem->WorkingDirectory());
      size_t slashpos = filepatt.find_last_of("/");
      size_t fbegin;
      if ( slashpos != std::string::npos ) {
        dirname = filepatt.substr(0,slashpos);
        fbegin = slashpos + 1;
      } else { fbegin = 0; }
      
      const char* epath = gSystem->ExpandPathName(dirname.c_str());
      void* dirp = gSystem->OpenDirectory(epath);
      delete [] epath;
      if ( dirp ) {
        std::string basename = filepatt.substr(fbegin,filepatt.size()-fbegin);
        TRegexp re(basename.c_str(),kTRUE);
        const char* onefile;
        while ( ( onefile = gSystem->GetDirEntry(dirp) ) ) {
          TString afile = onefile;
          if ( afile=="." || afile==".." ) continue;
          if ( basename!=afile && afile.Index(re) == kNPOS ) continue;
          //std::string fullname = dirname + "/" + afile.Data();
          path2n[filepatt]++;  // found one in this directory
        }
        gSystem->FreeDirectory(dirp);
      } // open directory
    } // loop over alternative prefixes 

    // find the path with the maximum # of files in it
    std::map<std::string,size_t>::const_iterator mitr = path2n.begin();
    size_t nfmax = 0, nftot = 0;
    std::string pathmax;
    for ( ; mitr != path2n.end(); ++mitr) {
      nftot += mitr->second;
      if ( mitr->second > nfmax ) {
        pathmax = mitr->first;
        nfmax = mitr->second;
      }
    }

    // no null path allowed for at least these
    if ( fFluxType.compare("ntuple")      == 0 ||
         fFluxType.compare("simple_flux") == 0    ) {
      if ( pathmax == "" || nftot == 0 ) {
        mf::LogError("GENIEHelper") 
          << "For \"ntuple\" or \"simple_flux\" specification must resolve to at least one file" 
          << "\n none were found for \"" << userpattern << "\" using FW_SERARCH_PATH of \"" << cet::getenv("FW_SEARCH_PATH");
        assert( pathmax != "" && nftot > 0 );  
      }
    }

    // print something out about what we found
    size_t npath = path2n.size();
    if ( npath > 1 ) {
      mf::LogInfo("GENIEHelper") 
        << " found " << nftot << " files in " << npath << " distinct paths";
      mitr = path2n.begin();
      for ( ; mitr != path2n.end(); ++mitr) {
        mf::LogInfo("GENIEHelper") << mitr->second << " files at: "
                                   << mitr->first;
      }
    }
      
    fFluxFiles.insert(pathmax);

    return;
  } // FindFluxPath

} // namespace evgb

