// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.
//
// ========================
//
// This code loops over v0 photons and makes pairs for photon HBT analysis.
//    Please write to: daiki.sekihata@cern.ch

#include <cstring>
#include <iterator>

#include "TString.h"
#include "Math/Vector4D.h"
#include "Math/Vector3D.h"
#include "Math/LorentzRotation.h"
#include "Math/Rotation3D.h"
#include "Math/AxisAngle.h"
#include "Framework/runDataProcessing.h"
#include "Framework/AnalysisTask.h"
#include "Framework/AnalysisDataModel.h"
#include "Framework/ASoAHelpers.h"
#include "ReconstructionDataFormats/Track.h"
#include "Common/Core/trackUtilities.h"
#include "Common/Core/TrackSelection.h"
#include "Common/DataModel/TrackSelectionTables.h"
#include "Common/DataModel/EventSelection.h"
#include "Common/DataModel/Centrality.h"
#include "Common/DataModel/PIDResponse.h"
#include "Common/Core/RecoDecay.h"
#include "PWGEM/PhotonMeson/Utils/PCMUtilities.h"
#include "PWGEM/PhotonMeson/DataModel/gammaTables.h"
#include "PWGEM/PhotonMeson/Core/V0PhotonCut.h"
#include "PWGEM/PhotonMeson/Core/PHOSPhotonCut.h"
#include "PWGEM/PhotonMeson/Core/CutsLibrary.h"
#include "PWGEM/PhotonMeson/Core/HistogramsLibrary.h"

using namespace o2;
using namespace o2::aod;
using namespace o2::framework;
using namespace o2::framework::expressions;
using namespace o2::soa;

using MyV0Photons = soa::Join<aod::V0Photons, aod::V0RecalculationAndKF>;
using MyV0Photon = MyV0Photons::iterator;

struct PhotonHBT {
  enum PairType {
    kPCMPCM = 0,
    kPHOSPHOS = 1,
    kEMCEMC = 2,
    kPCMPHOS = 3,
    kPCMEMC = 4,
    kPHOSEMC = 5,
  };

  Configurable<std::string> fConfigPCMCuts{"cfgPCMCuts", "analysis,qc,nocut", "Comma separated list of V0 photon cuts"};
  Configurable<std::string> fConfigPHOSCuts{"cfgPHOSCuts", "test02,test03", "Comma separated list of PHOS photon cuts"};

  OutputObj<THashList> fOutputEvent{"Event"};
  OutputObj<THashList> fOutputPair{"Pair"}; // 2-photon pair
  THashList* fMainList = new THashList();

  std::vector<V0PhotonCut> fPCMCuts;
  std::vector<PHOSPhotonCut> fPHOSCuts;

  std::vector<std::string> fPairNames;
  void init(InitContext& context)
  {
    if (context.mOptions.get<bool>("processPCMPCM")) {
      fPairNames.push_back("PCMPCM");
    }
    if (context.mOptions.get<bool>("processPHOSPHOS")) {
      fPairNames.push_back("PHOSPHOS");
    }
    if (context.mOptions.get<bool>("processPCMPHOS")) {
      fPairNames.push_back("PCMPHOS");
    }

    DefinePCMCuts();
    DefinePHOSCuts();
    addhistograms();

    fOutputEvent.setObject(reinterpret_cast<THashList*>(fMainList->FindObject("Event")));
    fOutputPair.setObject(reinterpret_cast<THashList*>(fMainList->FindObject("Pair")));
  }

  template <typename TCuts1, typename TCuts2>
  void add_pair_histograms(THashList* list_pair, const std::string pairname, TCuts1 const& cuts1, TCuts2 const& cuts2)
  {
    for (auto& cut1 : cuts1) {
      for (auto& cut2 : cuts2) {
        std::string cutname1 = cut1.GetName();
        std::string cutname2 = cut2.GetName();

        if ((pairname == "PCMPCM" || pairname == "PHOSPHOS" || pairname == "EMCEMC") && (cutname1 != cutname2))
          continue;

        THashList* list_pair_subsys = reinterpret_cast<THashList*>(list_pair->FindObject(pairname.data()));
        std::string pair_cut_name = cutname1 + "_" + cutname2;
        o2::aod::emphotonhistograms::AddHistClass(list_pair_subsys, pair_cut_name.data());
        THashList* list_pair_subsys_cut = reinterpret_cast<THashList*>(list_pair_subsys->FindObject(pair_cut_name.data()));
        o2::aod::emphotonhistograms::DefineHistograms(list_pair_subsys_cut, "photon_hbt");
      } // end of cut2 loop
    }   // end of cut1 loop
  }

  static constexpr std::string_view pairnames[6] = {"PCMPCM", "PHOSPHOS", "EMCEMC", "PCMPHOS", "PCMEMC", "PHOSEMC"};
  void addhistograms()
  {
    fMainList->SetOwner(true);
    fMainList->SetName("fMainList");

    // create sub lists first.
    o2::aod::emphotonhistograms::AddHistClass(fMainList, "Event");
    THashList* list_ev = reinterpret_cast<THashList*>(fMainList->FindObject("Event"));

    o2::aod::emphotonhistograms::AddHistClass(fMainList, "Pair");
    THashList* list_pair = reinterpret_cast<THashList*>(fMainList->FindObject("Pair"));

    for (auto& pairname : fPairNames) {
      LOGF(info, "Enabled pairs = %s", pairname.data());

      o2::aod::emphotonhistograms::AddHistClass(list_ev, pairname.data());
      THashList* list_ev_pair = reinterpret_cast<THashList*>(list_ev->FindObject(pairname.data()));
      o2::aod::emphotonhistograms::DefineHistograms(list_ev_pair, "Event");

      o2::aod::emphotonhistograms::AddHistClass(list_pair, pairname.data());

      if (pairname == "PCMPCM") {
        add_pair_histograms(list_pair, pairname, fPCMCuts, fPCMCuts);
      }
      if (pairname == "PHOSPHOS") {
        add_pair_histograms(list_pair, pairname, fPHOSCuts, fPHOSCuts);
      }
      if (pairname == "PCMPHOS") {
        add_pair_histograms(list_pair, pairname, fPCMCuts, fPHOSCuts);
      }

    } // end of pair name loop
  }

  void DefinePCMCuts()
  {
    TString cutNamesStr = fConfigPCMCuts.value;
    if (!cutNamesStr.IsNull()) {
      std::unique_ptr<TObjArray> objArray(cutNamesStr.Tokenize(","));
      for (int icut = 0; icut < objArray->GetEntries(); ++icut) {
        const char* cutname = objArray->At(icut)->GetName();
        LOGF(info, "add cut : %s", cutname);
        fPCMCuts.push_back(*pcmcuts::GetCut(cutname));
      }
    }
    LOGF(info, "Number of PCM cuts = %d", fPCMCuts.size());
  }
  void DefinePHOSCuts()
  {
    TString cutNamesStr = fConfigPHOSCuts.value;
    if (!cutNamesStr.IsNull()) {
      std::unique_ptr<TObjArray> objArray(cutNamesStr.Tokenize(","));
      for (int icut = 0; icut < objArray->GetEntries(); ++icut) {
        const char* cutname = objArray->At(icut)->GetName();
        LOGF(info, "add cut : %s", cutname);
        fPHOSCuts.push_back(*phoscuts::GetCut(cutname));
      }
    }
    LOGF(info, "Number of PHOS cuts = %d", fPHOSCuts.size());
  }

  template <PairType pairtype, typename TG1, typename TG2, typename TCut1, typename TCut2>
  bool IsSelectedPair(TG1 const& g1, TG2 const& g2, TCut1 const& cut1, TCut2 const& cut2)
  {
    bool is_g1_passed = false;
    bool is_g2_passed = false;
    if constexpr (pairtype == PairType::kPCMPCM) {
      is_g1_passed = cut1.template IsSelected<aod::V0Legs>(g1);
      is_g2_passed = cut2.template IsSelected<aod::V0Legs>(g2);
    } else if constexpr (pairtype == PairType::kPHOSPHOS) {
      is_g1_passed = cut1.template IsSelected(g1);
      is_g2_passed = cut2.template IsSelected(g2);
    } else if constexpr (pairtype == PairType::kPCMPHOS) {
      is_g1_passed = cut1.template IsSelected<aod::V0Legs>(g1);
      is_g2_passed = cut2.template IsSelected(g2);
    } else {
      return true;
    }
    return (is_g1_passed & is_g2_passed);
  }

  // include fPCMCuts as an argument TCuts1, TCuts2 in template function
  template <PairType pairtype, typename TEvents, typename TPhotons1, typename TPhotons2, typename TPreslice1, typename TPreslice2, typename TCuts1, typename TCuts2, typename TLegs>
  void SameEventPairing(TEvents const& collisions, TPhotons1 const& photons1, TPhotons2 const& photons2, TPreslice1 const& perCollision1, TPreslice2 const& perCollision2, TCuts1 const& cuts1, TCuts2 const& cuts2, TLegs const& legs)
  {
    for (auto& collision : collisions) {

      if ((pairtype == kPHOSPHOS || pairtype == kPCMPHOS) && !collision.isPHOSCPVreadout()) {
        continue;
      }

      reinterpret_cast<TH1F*>(fMainList->FindObject("Event")->FindObject(pairnames[pairtype].data())->FindObject("hZvtx_before"))->Fill(collision.posZ());
      reinterpret_cast<TH1F*>(fMainList->FindObject("Event")->FindObject(pairnames[pairtype].data())->FindObject("hCollisionCounter"))->Fill(1.0); // all
      if (!collision.sel8()) {
        continue;
      }
      reinterpret_cast<TH1F*>(fMainList->FindObject("Event")->FindObject(pairnames[pairtype].data())->FindObject("hCollisionCounter"))->Fill(2.0); // FT0VX i.e. FT0and

      if (collision.numContrib() < 0.5) {
        continue;
      }
      reinterpret_cast<TH1F*>(fMainList->FindObject("Event")->FindObject(pairnames[pairtype].data())->FindObject("hCollisionCounter"))->Fill(3.0); // Ncontrib > 0

      if (abs(collision.posZ()) > 10.0) {
        continue;
      }
      reinterpret_cast<TH1F*>(fMainList->FindObject("Event")->FindObject(pairnames[pairtype].data())->FindObject("hZvtx_after"))->Fill(collision.posZ());
      reinterpret_cast<TH1F*>(fMainList->FindObject("Event")->FindObject(pairnames[pairtype].data())->FindObject("hCollisionCounter"))->Fill(4.0); // |Zvtx| < 10 cm

      auto photons1_coll = photons1.sliceBy(perCollision1, collision.collisionId());
      auto photons2_coll = photons2.sliceBy(perCollision2, collision.collisionId());

      if constexpr (pairtype == PairType::kPCMPCM || pairtype == PairType::kPHOSPHOS || pairtype == PairType::kEMCEMC) {
        for (auto& cut : cuts1) {
          for (auto& [g1, g2] : combinations(CombinationsStrictlyUpperIndexPolicy(photons1_coll, photons2_coll))) {
            if (!IsSelectedPair<pairtype>(g1, g2, cut, cut)) {
              continue;
            }
            // longitudinally co-moving system (LCMS)
            ROOT::Math::PtEtaPhiMVector v1(g1.pt(), g1.eta(), g1.phi(), 0.);
            ROOT::Math::PtEtaPhiMVector v2(g2.pt(), g2.eta(), g2.phi(), 0.);
            ROOT::Math::PtEtaPhiMVector q12 = v1 - v2;
            ROOT::Math::PtEtaPhiMVector k12 = 0.5 * (v1 + v2);
            float qinv = -q12.M();
            float kt = k12.Pt();
            ROOT::Math::XYZVector q_3d = q12.Vect();               // 3D q vector
            ROOT::Math::XYZVector uv_out = k12.Vect() / k12.P();   // unit vector for out
            ROOT::Math::XYZVector uv_long(0, 0, 1);                // unit vector for long, beam axis
            ROOT::Math::XYZVector uv_side = uv_out.Cross(uv_long); // unit vector for side
            float qout = q_3d.Dot(uv_out);
            float qlong = q_3d.Dot(uv_long);
            float qside = q_3d.Dot(uv_side);
            double values[5] = {qinv, qlong, qout, qside, kt};
            reinterpret_cast<THnSparseF*>(fMainList->FindObject("Pair")->FindObject(pairnames[pairtype].data())->FindObject(Form("%s_%s", cut.GetName(), cut.GetName()))->FindObject("hs_q_same"))->Fill(values);
          }    // end of combination
        }      // end of cut loop
      } else { // different subsystem pairs
        for (auto& cut1 : cuts1) {
          for (auto& cut2 : cuts2) {
            for (auto& [g1, g2] : combinations(CombinationsFullIndexPolicy(photons1_coll, photons2_coll))) {
              if (!IsSelectedPair<pairtype>(g1, g2, cut1, cut2)) {
                continue;
              }
              ROOT::Math::PtEtaPhiMVector v1(g1.pt(), g1.eta(), g1.phi(), 0.);
              ROOT::Math::PtEtaPhiMVector v2(g2.pt(), g2.eta(), g2.phi(), 0.);
              ROOT::Math::PtEtaPhiMVector q12 = v1 - v2;
              ROOT::Math::PtEtaPhiMVector k12 = 0.5 * (v1 + v2);
              float qinv = -q12.M();
              float kt = k12.Pt();
              ROOT::Math::XYZVector q_3d = q12.Vect();               // 3D q vector
              ROOT::Math::XYZVector uv_out = k12.Vect() / k12.P();   // unit vector for out
              ROOT::Math::XYZVector uv_long(0, 0, 1);                // unit vector for long, beam axis
              ROOT::Math::XYZVector uv_side = uv_out.Cross(uv_long); // unit vector for side
              float qout = q_3d.Dot(uv_out);
              float qlong = q_3d.Dot(uv_long);
              float qside = q_3d.Dot(uv_side);
              double values[5] = {qinv, qlong, qout, qside, kt};
              reinterpret_cast<THnSparseF*>(fMainList->FindObject("Pair")->FindObject(pairnames[pairtype].data())->FindObject(Form("%s_%s", cut1.GetName(), cut2.GetName()))->FindObject("hs_q_same"))->Fill(values);
            } // end of combination
          }   // end of cut2 loop
        }     // end of cut1 loop
      }
    } // end of collision loop
  }

  Configurable<int> ndepth{"ndepth", 10, "depth for event mixing"};
  ConfigurableAxis ConfVtxBins{"ConfVtxBins", {VARIABLE_WIDTH, -10.0f, -8.f, -6.f, -4.f, -2.f, 0.f, 2.f, 4.f, 6.f, 8.f, 10.f}, "Mixing bins - z-vertex"};
  ConfigurableAxis ConfMultBins{"ConfMultBins", {VARIABLE_WIDTH, 0.0f, 10.f, 20.0f, 40.0f, 60.0f, 80.0f, 100.0f, 200.0f, 1e+10f}, "Mixing bins - multiplicity"};
  using BinningType = ColumnBinningPolicy<aod::collision::PosZ, aod::mult::MultNTracksPV>;
  BinningType colBinning{{ConfVtxBins, ConfMultBins}, true};

  template <PairType pairtype, typename TEvents, typename TPhotons1, typename TPhotons2, typename TPreslice1, typename TPreslice2, typename TCuts1, typename TCuts2, typename TLegs>
  void MixedEventPairing(TEvents const& collisions, TPhotons1 const& photons1, TPhotons2 const& photons2, TPreslice1 const& perCollision1, TPreslice2 const& perCollision2, TCuts1 const& cuts1, TCuts2 const& cuts2, TLegs const& legs)
  {
    // LOGF(info, "Number of collisions after filtering: %d", collisions.size());
    int nev = 0; // event counter for collision1
    int index_coll1 = -999;
    for (auto& [collision1, collision2] : soa::selfCombinations(colBinning, 1e+3, -1, collisions, collisions)) { // internally, CombinationsStrictlyUpperIndexPolicy(collisions, collisions) is called.

      if (index_coll1 != collision1.collisionId()) {
        index_coll1 = collision1.collisionId();
        nev = 0; // reset event counter for mixing, when collision index of collision1 changes.
      }

      if (nev > ndepth) {
        continue;
      }

      if (pairtype == PairType::kPCMPCM && (collision1.ngpcm() < 2 || collision2.ngpcm() < 2)) {
        continue;
      } else if (pairtype == PairType::kPHOSPHOS && (collision1.ngphos() < 2 || collision2.ngphos() < 2)) {
        continue;
      } else if (pairtype == PairType::kPCMPHOS && ((collision1.ngpcm() < 1 || collision1.ngphos() < 1) || (collision2.ngpcm() < 1 || collision2.ngphos() < 1))) {
        continue;
      }

      // LOGF(info, "Mixed event collisionId: (%d, %d) , counter = %d, ngpcm: (%d, %d), ngphos: (%d, %d), ngemc: (%d, %d)",
      //     collision1.collisionId(), collision2.collisionId(), nev, collision1.ngpcm(), collision2.ngpcm(), collision1.ngphos(), collision2.ngphos(), collision1.ngemc(), collision2.ngemc());

      auto photons_coll1 = photons1.sliceBy(perCollision1, collision1.collisionId());
      auto photons_coll2 = photons2.sliceBy(perCollision2, collision2.collisionId());
      // LOGF(info, "collision1: posZ = %f, numContrib = %d , sel8 = %d | collision2: posZ = %f, numContrib = %d , sel8 = %d",
      //     collision1.posZ(), collision1.numContrib(), collision1.sel8(), collision2.posZ(), collision2.numContrib(), collision2.sel8());

      for (auto& cut1 : cuts1) {
        for (auto& cut2 : cuts2) {
          for (auto& [g1, g2] : combinations(soa::CombinationsFullIndexPolicy(photons_coll1, photons_coll2))) {
            // LOGF(info, "Mixed event photon pair: (%d, %d) from events (%d, %d), photon event: (%d, %d)", g1.index(), g2.index(), collision1.index(), collision2.index(), g1.collisionId(), g2.collisionId());

            if ((pairtype == PairType::kPCMPCM || pairtype == PairType::kPHOSPHOS || pairtype == PairType::kEMCEMC) && (TString(cut1.GetName()) != TString(cut2.GetName()))) {
              continue;
            }
            if (!IsSelectedPair<pairtype>(g1, g2, cut1, cut2)) {
              continue;
            }
            ROOT::Math::PtEtaPhiMVector v1(g1.pt(), g1.eta(), g1.phi(), 0.);
            ROOT::Math::PtEtaPhiMVector v2(g2.pt(), g2.eta(), g2.phi(), 0.);
            ROOT::Math::PtEtaPhiMVector q12 = v1 - v2;
            ROOT::Math::PtEtaPhiMVector k12 = 0.5 * (v1 + v2);
            float qinv = -q12.M();
            float kt = k12.Pt();
            ROOT::Math::XYZVector q_3d = q12.Vect();               // 3D q vector
            ROOT::Math::XYZVector uv_out = k12.Vect() / k12.P();   // unit vector for out
            ROOT::Math::XYZVector uv_long(0, 0, 1);                // unit vector for long, beam axis
            ROOT::Math::XYZVector uv_side = uv_out.Cross(uv_long); // unit vector for side
            float qout = q_3d.Dot(uv_out);
            float qlong = q_3d.Dot(uv_long);
            float qside = q_3d.Dot(uv_side);
            double values[5] = {qinv, qlong, qout, qside, kt};
            reinterpret_cast<THnSparseF*>(fMainList->FindObject("Pair")->FindObject(pairnames[pairtype].data())->FindObject(Form("%s_%s", cut1.GetName(), cut2.GetName()))->FindObject("hs_q_mix"))->Fill(values);

          } // end of different photon combinations
        }   // end of cut2 loop
      }     // end of cut1 loop
      nev++;
    } // end of different collision combinations
  }

  Preslice<MyV0Photons> perCollision_pcm = aod::v0photon::collisionId;
  Preslice<aod::PHOSClusters> perCollision_phos = aod::skimmedcluster::collisionId;

  Filter collisionFilter_common = nabs(o2::aod::collision::posZ) < 10.f && o2::aod::collision::numContrib > (uint16_t)0 && o2::aod::evsel::sel8 == true;
  Filter collisionFilter_subsys = (o2::aod::emreducedevent::ngpcm >= 2) || (o2::aod::emreducedevent::ngphos >= 2) || (o2::aod::emreducedevent::ngpcm >= 1 && o2::aod::emreducedevent::ngphos >= 1);
  using MyFilteredCollisions = soa::Filtered<aod::EMReducedEvents>;

  void processPCMPCM(aod::EMReducedEvents const& collisions, MyFilteredCollisions const& filtered_collisions, MyV0Photons const& v0photons, aod::V0Legs const& legs)
  {
    SameEventPairing<PairType::kPCMPCM>(collisions, v0photons, v0photons, perCollision_pcm, perCollision_pcm, fPCMCuts, fPCMCuts, legs);
    MixedEventPairing<PairType::kPCMPCM>(filtered_collisions, v0photons, v0photons, perCollision_pcm, perCollision_pcm, fPCMCuts, fPCMCuts, legs);
  }

  void processPHOSPHOS(aod::EMReducedEvents const& collisions, MyFilteredCollisions const& filtered_collisions, aod::PHOSClusters const& phosclusters)
  {
    SameEventPairing<PairType::kPHOSPHOS>(collisions, phosclusters, phosclusters, perCollision_phos, perCollision_phos, fPHOSCuts, fPHOSCuts, nullptr);
    MixedEventPairing<PairType::kPHOSPHOS>(filtered_collisions, phosclusters, phosclusters, perCollision_phos, perCollision_phos, fPHOSCuts, fPHOSCuts, nullptr);
  }

  void processPCMPHOS(aod::EMReducedEvents const& collisions, MyFilteredCollisions const& filtered_collisions, MyV0Photons const& v0photons, aod::PHOSClusters const& phosclusters, aod::V0Legs const& legs)
  {
    SameEventPairing<PairType::kPCMPHOS>(collisions, v0photons, phosclusters, perCollision_pcm, perCollision_phos, fPCMCuts, fPHOSCuts, legs);
    MixedEventPairing<PairType::kPCMPHOS>(filtered_collisions, v0photons, phosclusters, perCollision_pcm, perCollision_phos, fPCMCuts, fPHOSCuts, legs);
  }

  void processDummy(aod::EMReducedEvents::iterator const& collision) {}

  PROCESS_SWITCH(PhotonHBT, processPCMPCM, "pairing PCM-PCM", false);
  PROCESS_SWITCH(PhotonHBT, processPHOSPHOS, "pairing PHOS-PHOS", false);
  PROCESS_SWITCH(PhotonHBT, processPCMPHOS, "pairing PCM-PHOS", false);
  PROCESS_SWITCH(PhotonHBT, processDummy, "Dummy function", true);
};

WorkflowSpec defineDataProcessing(ConfigContext const& cfgc)
{
  return WorkflowSpec{
    adaptAnalysisTask<PhotonHBT>(cfgc, TaskName{"photon-hbt"})};
}
