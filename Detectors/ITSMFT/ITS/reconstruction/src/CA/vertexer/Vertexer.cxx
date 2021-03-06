// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include <algorithm>
#include <iostream>
#include <cmath>
#include <limits>
#include <iomanip>
#include <chrono>

#include "ITSReconstruction/CA/Constants.h"
#include "ITSReconstruction/CA/Cluster.h"
#include "ITSReconstruction/CA/Event.h"
#include "ITSReconstruction/CA/Layer.h"
#include "ITSReconstruction/CA/vertexer/ClusterLines.h"
#include "ITSReconstruction/CA/vertexer/Vertexer.h"
#include "ITSReconstruction/CA/IndexTableUtils.h"

namespace o2
{
namespace ITS
{
namespace CA
{

using Constants::ITS::LayersRCoordinate;
using Constants::ITS::LayersZCoordinate;
using Constants::IndexTable::PhiBins;
using Constants::IndexTable::ZBins;
using Constants::Math::TwoPi;
using IndexTableUtils::getZBinIndex;

Vertexer::Vertexer(const Event& event) : mEvent{ event } // ,
// mAverageClustersRadii{ std::array<float, 3>{ 0.f, 0.f, 0.f } }
{
  for (int iLayer{ 0 }; iLayer < Constants::ITS::LayersNumberVertexer; ++iLayer) {
    const Layer& currentLayer{ event.getLayer(iLayer) };
    const int clustersNum{ currentLayer.getClustersSize() };
    mClusters[iLayer].clear();
    if (clustersNum > mClusters[iLayer].capacity()) {
      mClusters[iLayer].reserve(clustersNum);
    }
    for (int iCluster{ 0 }; iCluster < clustersNum; ++iCluster) {
      mClusters[iLayer].emplace_back(iLayer, currentLayer.getCluster(iCluster));
    }
    // const float inverseNumberOfClusters { 1.f/mClusters[iLayer].size() };
    // for ( auto& cluster : mClusters[iLayer] ) {
    //   mAverageClustersRadii[iLayer] += cluster.rCoordinate;
    // }
    // mAverageClustersRadii[iLayer] *= inverseNumberOfClusters;
  }
  // mDeltaRadii10 = mAverageClustersRadii[1] - mAverageClustersRadii[0];
  // mDeltaRadii21 = mAverageClustersRadii[2] - mAverageClustersRadii[1];
  mDeltaRadii10 = LayersRCoordinate()[1] - LayersRCoordinate()[0];
  mDeltaRadii21 = LayersRCoordinate()[2] - LayersRCoordinate()[1];
}

Vertexer::~Vertexer(){};

void Vertexer::initialise(const float zCut, const float phiCut, const float pairCut, const float clusterCut,
                          const int clusterContributorsCut)
{
  for (int iLayer{ 0 }; iLayer < Constants::ITS::LayersNumberVertexer; ++iLayer) {
    std::sort(mClusters[iLayer].begin(), mClusters[iLayer].end(), [](Cluster& cluster1, Cluster& cluster2) {
      return cluster1.indexTableBinIndex < cluster2.indexTableBinIndex;
    });
  }
  for (int iLayer{ 0 }; iLayer < Constants::ITS::LayersNumberVertexer; ++iLayer) {
    const int clustersNum = static_cast<int>(mClusters[iLayer].size());
    int previousBinIndex{ 0 };
    mIndexTables[iLayer][0] = 0;
    for (int iCluster{ 0 }; iCluster < clustersNum; ++iCluster) {
      const int currentBinIndex{ mClusters[iLayer][iCluster].indexTableBinIndex };
      if (currentBinIndex > previousBinIndex) {
        for (int iBin{ previousBinIndex + 1 }; iBin <= currentBinIndex; ++iBin) {
          mIndexTables[iLayer][iBin] = iCluster;
        }
        previousBinIndex = currentBinIndex;
      }
    }
    for (int iBin{ previousBinIndex + 1 }; iBin <= ZBins * PhiBins; iBin++) {
      mIndexTables[iLayer][iBin] = clustersNum;
    }
  }

  mZCut = zCut;
  mPhiCut = phiCut;
  mPairCut = pairCut;
  mClusterCut = clusterCut;
  mClusterContributorsCut = clusterContributorsCut;
  mPhiSpan = static_cast<int>(std::ceil(PhiBins * mPhiCut / TwoPi));
  mZSpan = static_cast<int>(std::ceil(mZCut * Constants::IndexTable::InverseZBinSize()[0]));
  mVertexerInitialised = true;
}

void Vertexer::printIndexTables()
{
  for (int iTables{ 0 }; iTables < Constants::ITS::LayersNumberVertexer; ++iTables) {
    std::cout << "Table " << iTables << std::endl;
    for (int iIndexPhi{ 0 }; iIndexPhi < PhiBins; ++iIndexPhi) {
      for (int iIndexZeta{ 0 }; iIndexZeta < ZBins; ++iIndexZeta) {
        std::cout << mIndexTables[iTables][iIndexZeta + ZBins * iIndexPhi] << "\t";
      }
      std::cout << std::endl;
    }
    std::cout << mIndexTables[iTables][ZBins * PhiBins] << "\t";
  }
}

const std::vector<std::pair<int, int>> Vertexer::selectClusters(const std::array<int, ZBins * PhiBins + 1>& indexTable,
                                                                const std::array<int, 4>& selectedBinsRect)
{
  std::vector<std::pair<int, int>> filteredBins{};
  int phiBinsNum{ selectedBinsRect[3] - selectedBinsRect[1] + 1 };
  if (phiBinsNum < 0)
    phiBinsNum += PhiBins;
  filteredBins.reserve(phiBinsNum);
  for (int iPhiBin{ selectedBinsRect[1] }, iPhiCount{ 0 }; iPhiCount < phiBinsNum;
       iPhiBin = ++iPhiBin == PhiBins ? 0 : iPhiBin, iPhiCount++) {
    const int firstBinIndex{ IndexTableUtils::getBinIndex(selectedBinsRect[0], iPhiBin) };
    filteredBins.emplace_back(
      indexTable[firstBinIndex],
      IndexTableUtils::countRowSelectedBins(indexTable, iPhiBin, selectedBinsRect[0], selectedBinsRect[2]));
  }
  return filteredBins;
}

void Vertexer::computeTriplets()
{
  if (mVertexerInitialised) {
    // std::chrono::time_point<std::chrono::system_clock> start, end;
    // start = std::chrono::system_clock::now();

    for (int iBinMiddleTable{ 0 }; iBinMiddleTable < ZBins * PhiBins; ++iBinMiddleTable) {

      int lowestZInnerBin{ std::max(
        0, ZBins - static_cast<int>(std::ceil((ZBins - iBinMiddleTable % ZBins + 1) * (mDeltaRadii21 + mDeltaRadii10) /
                                              mDeltaRadii10))) };
      int highestZInnerBin{ std::min(
        static_cast<int>(std::ceil((iBinMiddleTable % ZBins + 1) * (mDeltaRadii21 + mDeltaRadii10) / mDeltaRadii21)),
        ZBins - 1) };

      // int lowestZInnerBin { 0 };
      // int highestZInnerBin { ZBins -1 };

      int MiddlePhiBin{ static_cast<int>(iBinMiddleTable / ZBins) };

      mClustersToProcessInner = selectClusters(
        mIndexTables[0],
        std::array<int, 4>{ lowestZInnerBin, (MiddlePhiBin - mPhiSpan < 0) ? PhiBins + (MiddlePhiBin - mPhiSpan)
                                                                           : MiddlePhiBin - mPhiSpan,
                            highestZInnerBin, (MiddlePhiBin + mPhiSpan > PhiBins) ? MiddlePhiBin + mPhiSpan - PhiBins
                                                                                  : MiddlePhiBin + mPhiSpan });
      for (int iClusterMiddleLayer{ mIndexTables[1][iBinMiddleTable] };
           iClusterMiddleLayer < mIndexTables[1][iBinMiddleTable + 1]; ++iClusterMiddleLayer) {
        for (int iInnerClusterRow{ 0 }; iInnerClusterRow < mClustersToProcessInner.size(); ++iInnerClusterRow) {
          for (int iClusterInnerLayer{ std::get<0>(mClustersToProcessInner[iInnerClusterRow]) };
               iClusterInnerLayer < std::get<0>(mClustersToProcessInner[iInnerClusterRow]) +
                                      std::get<1>(mClustersToProcessInner[iInnerClusterRow]);
               ++iClusterInnerLayer) {

            if (std::abs(mClusters[0][iClusterInnerLayer].phiCoordinate -
                         mClusters[1][iClusterMiddleLayer].phiCoordinate) < mPhiCut) {

              float zetaProjection{ (mClusters[1][iClusterMiddleLayer].zCoordinate -
                                     mClusters[0][iClusterInnerLayer].zCoordinate) *
                                      (mDeltaRadii21 / mDeltaRadii10 + 1) +
                                    mClusters[0][iClusterInnerLayer].zCoordinate };

              if (std::abs(zetaProjection) > (LayersZCoordinate()[0] + mZCut))
                continue;

              int binZOuterProjection{ (zetaProjection < -LayersZCoordinate()[0])
                                         ? 0
                                         : (zetaProjection > LayersZCoordinate()[0])
                                             ? ZBins - 1
                                             : getZBinIndex(2, zetaProjection) };

              int lowestZOuterBin{ (binZOuterProjection - mZSpan < 0) ? 0 : binZOuterProjection - mZSpan };
              int highestZOuterBin{ (binZOuterProjection + mZSpan > ZBins - 1) ? ZBins - 1
                                                                               : binZOuterProjection + mZSpan };
              // int lowestZOuterBin  { 0 };
              // int highestZOuterBin  { ZBins - 1 };

              int lowestPhiOuterBin{ (MiddlePhiBin - mPhiSpan < 0) ? PhiBins + MiddlePhiBin - mPhiSpan
                                                                   : MiddlePhiBin - mPhiSpan };
              int highestPhiOuterBin{ (MiddlePhiBin + mPhiSpan > PhiBins - 1) ? MiddlePhiBin + mPhiSpan - PhiBins
                                                                              : MiddlePhiBin + mPhiSpan };

              mClustersToProcessOuter = selectClusters(
                mIndexTables[2],
                std::array<int, 4>{ lowestZOuterBin, lowestPhiOuterBin, highestZOuterBin, highestPhiOuterBin });

              for (int iOuterClusterRow{ 0 }; iOuterClusterRow < mClustersToProcessOuter.size(); ++iOuterClusterRow) {
                for (int iClusterOuterLayer{ std::get<0>(mClustersToProcessOuter[iOuterClusterRow]) };
                     iClusterOuterLayer < std::get<0>(mClustersToProcessOuter[iOuterClusterRow]) +
                                            std::get<1>(mClustersToProcessOuter[iOuterClusterRow]);
                     ++iClusterOuterLayer) {
                  if ((std::abs(std::abs(mClusters[2][iClusterOuterLayer].zCoordinate) - std::abs(zetaProjection)) <
                       mZCut) &&
                      std::abs(std::abs(mClusters[2][iClusterOuterLayer].phiCoordinate) -
                               std::abs(mClusters[1][iClusterMiddleLayer].phiCoordinate)) < mPhiCut)
                    mTriplets.emplace_back(
                      std::array<int, 3>{ iClusterInnerLayer, iClusterMiddleLayer, iClusterOuterLayer });
                }
              }
            }
          }
        }
      }
    }

    // end = std::chrono::system_clock::now();
    // int elapsed_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    // std::time_t end_time = std::chrono::system_clock::to_time_t(end);
    // std::cout << "Finished computation at " << std::ctime(&end_time) << "elapsed time: " << elapsed_milliseconds
    //           << "ms\n";
  }
}

void Vertexer::findTracklets()
{
  if (mVertexerInitialised) {
    // std::chrono::time_point<std::chrono::system_clock> start, end;
    // start = std::chrono::system_clock::now();

    for (int iBinMiddleTable{ 0 }; iBinMiddleTable < ZBins * PhiBins; ++iBinMiddleTable) {

      int lowestZInnerBin{ std::max(
        0, ZBins - static_cast<int>(std::ceil((ZBins - iBinMiddleTable % ZBins + 1) * (mDeltaRadii21 + mDeltaRadii10) /
                                              mDeltaRadii10))) };
      int highestZInnerBin{ std::min(
        static_cast<int>(std::ceil((iBinMiddleTable % ZBins + 1) * (mDeltaRadii21 + mDeltaRadii10) / mDeltaRadii21)),
        ZBins - 1) };

      // int lowestZInnerBin { 0 };
      // int highestZInnerBin { ZBins -1 };

      int MiddlePhiBin{ static_cast<int>(iBinMiddleTable / ZBins) };

      mClustersToProcessInner = selectClusters(
        mIndexTables[0],
        std::array<int, 4>{ lowestZInnerBin, (MiddlePhiBin - mPhiSpan < 0) ? PhiBins + (MiddlePhiBin - mPhiSpan)
                                                                           : MiddlePhiBin - mPhiSpan,
                            highestZInnerBin, (MiddlePhiBin + mPhiSpan > PhiBins) ? MiddlePhiBin + mPhiSpan - PhiBins
                                                                                  : MiddlePhiBin + mPhiSpan });

      for (int iClusterMiddleLayer{ mIndexTables[1][iBinMiddleTable] };
           iClusterMiddleLayer < mIndexTables[1][iBinMiddleTable + 1]; ++iClusterMiddleLayer) {
        for (int iInnerClusterRow{ 0 }; iInnerClusterRow < mClustersToProcessInner.size(); ++iInnerClusterRow) {
          for (int iClusterInnerLayer{ std::get<0>(mClustersToProcessInner[iInnerClusterRow]) };
               iClusterInnerLayer < std::get<0>(mClustersToProcessInner[iInnerClusterRow]) +
                                      std::get<1>(mClustersToProcessInner[iInnerClusterRow]);
               ++iClusterInnerLayer) {

            if (std::abs(mClusters[0][iClusterInnerLayer].phiCoordinate -
                         mClusters[1][iClusterMiddleLayer].phiCoordinate) < mPhiCut) {

              float zetaProjection{ (mClusters[1][iClusterMiddleLayer].zCoordinate -
                                     mClusters[0][iClusterInnerLayer].zCoordinate) *
                                      (mDeltaRadii21 / mDeltaRadii10 + 1) +
                                    mClusters[0][iClusterInnerLayer].zCoordinate };

              if (std::abs(zetaProjection) > (LayersZCoordinate()[0] + mZCut))
                continue;

              int binZOuterProjection{ (zetaProjection < -LayersZCoordinate()[0])
                                         ? 0
                                         : (zetaProjection > LayersZCoordinate()[0])
                                             ? ZBins - 1
                                             : getZBinIndex(2, zetaProjection) };

              int lowestZOuterBin{ (binZOuterProjection - mZSpan < 0) ? 0 : binZOuterProjection - mZSpan };
              int highestZOuterBin{ (binZOuterProjection + mZSpan > ZBins - 1) ? ZBins - 1
                                                                               : binZOuterProjection + mZSpan };
              // int lowestZOuterBin  { 0 };
              // int highestZOuterBin  { ZBins - 1 };

              int lowestPhiOuterBin{ (MiddlePhiBin - mPhiSpan < 0) ? PhiBins + MiddlePhiBin - mPhiSpan
                                                                   : MiddlePhiBin - mPhiSpan };
              int highestPhiOuterBin{ (MiddlePhiBin + mPhiSpan > PhiBins - 1) ? MiddlePhiBin + mPhiSpan - PhiBins
                                                                              : MiddlePhiBin + mPhiSpan };

              mClustersToProcessOuter = selectClusters(
                mIndexTables[2],
                std::array<int, 4>{ lowestZOuterBin, lowestPhiOuterBin, highestZOuterBin, highestPhiOuterBin });

              for (int iOuterClusterRow{ 0 }; iOuterClusterRow < mClustersToProcessOuter.size(); ++iOuterClusterRow) {
                for (int iClusterOuterLayer{ std::get<0>(mClustersToProcessOuter[iOuterClusterRow]) };
                     iClusterOuterLayer < std::get<0>(mClustersToProcessOuter[iOuterClusterRow]) +
                                            std::get<1>(mClustersToProcessOuter[iOuterClusterRow]);
                     ++iClusterOuterLayer) {
                  if ((std::abs(std::abs(mClusters[2][iClusterOuterLayer].zCoordinate) - std::abs(zetaProjection)) <
                       mZCut) &&
                      std::abs(std::abs(mClusters[2][iClusterOuterLayer].phiCoordinate) -
                               std::abs(mClusters[1][iClusterMiddleLayer].phiCoordinate)) < mPhiCut) {
                    mTracklets.emplace_back(
                      Line{ std::array<float, 3>{ mClusters[0][iClusterInnerLayer].xCoordinate,
                                                  mClusters[0][iClusterInnerLayer].yCoordinate,
                                                  mClusters[0][iClusterInnerLayer].zCoordinate },
                            std::array<float, 3>{ mClusters[1][iClusterMiddleLayer].xCoordinate,
                                                  mClusters[1][iClusterMiddleLayer].yCoordinate,
                                                  mClusters[1][iClusterMiddleLayer].zCoordinate } });
                  }
                }
              }
            }
          }
        }
      }
    }

    // end = std::chrono::system_clock::now();
    // int elapsed_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    // std::time_t end_time = std::chrono::system_clock::to_time_t(end);
    // std::cout << "Finished computation at " << std::ctime(&end_time) << "elapsed time: " << elapsed_milliseconds
    //           << "ms\n";
    mTrackletsFound = true;
  }
}

void Vertexer::checkTriplets()
{
  std::cout << "Triplets found: " << mTriplets.size() << std::endl;
  std::cout << "Tracklets found: " << mTracklets.size() << std::endl;
  int good{ 0 };
  int bad{ 0 };

  for (auto& triplet : mTriplets) {
    if ((mEvent.getLayer(0).getClusterLabel(mClusters[0][triplet[0]].clusterId).getTrackID() ==
         mEvent.getLayer(1).getClusterLabel(mClusters[1][triplet[1]].clusterId).getTrackID()) &&
        (mEvent.getLayer(2).getClusterLabel(mClusters[2][triplet[2]].clusterId).getTrackID() ==
         mEvent.getLayer(1).getClusterLabel(mClusters[1][triplet[1]].clusterId).getTrackID())) {
      ++good;
    } else {
      ++bad;
    }
  }

  std::cout << "good: " << good << "\tbad: " << bad << "\tratio: " << std::setprecision(4)
            << 100 * (float)good / (good + bad) << "%" << std::endl;
}

void Vertexer::findVertices()
{
  if (mTrackletsFound) {
    const int numTracklets{ static_cast<int>(mTracklets.size()) };
    mUsedTracklets.resize(numTracklets, false);

    for (int tracklet1{ 0 }; tracklet1 < numTracklets; ++tracklet1) {
      if (mUsedTracklets[tracklet1])
        continue;
      for (int tracklet2{ tracklet1 + 1 }; tracklet2 < numTracklets; ++tracklet2) {
        if (mUsedTracklets[tracklet2])
          continue;
        if (Line::getDCA(mTracklets[tracklet1], mTracklets[tracklet2]) <= mPairCut) {
          mTrackletClusters.emplace_back(tracklet1, mTracklets[tracklet1], tracklet2, mTracklets[tracklet2]);
          std::array<float, 3> tmpVertex{ mTrackletClusters.back().getVertex() };
          if (tmpVertex[0] * tmpVertex[0] + tmpVertex[1] * tmpVertex[1] > 4.f) {
            mTrackletClusters.pop_back();
            break;
          }
          mUsedTracklets[tracklet1] = true;
          mUsedTracklets[tracklet2] = true;
          for (int tracklet3{ 0 }; tracklet3 < numTracklets; ++tracklet3) {
            if (mUsedTracklets[tracklet3])
              continue;
            if (Line::getDistanceFromPoint(mTracklets[tracklet3], tmpVertex) < mPairCut) {
              mTrackletClusters.back().add(tracklet3, mTracklets[tracklet3]);
              mUsedTracklets[tracklet3] = true;
              tmpVertex = mTrackletClusters.back().getVertex();
            }
          }
          break;
        }
      }
    }
    std::sort(mTrackletClusters.begin(), mTrackletClusters.end(),
              [](ClusterLines& cluster1, ClusterLines& cluster2) { return cluster1.getSize() > cluster2.getSize(); });
    for (int iCluster1{ 0 }; iCluster1 < mTrackletClusters.size(); ++iCluster1) {
      std::array<float, 3> vertex1{ mTrackletClusters[iCluster1].getVertex() };
      std::array<float, 3> vertex2{};
      for (int iCluster2{ iCluster1 + 1 }; iCluster2 < mTrackletClusters.size(); ++iCluster2) {
        vertex2 = mTrackletClusters[iCluster2].getVertex();
        if (std::abs(vertex1[2] - vertex2[2]) < mClusterCut) {
          float distance{ (vertex1[0] - vertex2[0]) * (vertex1[0] - vertex2[0]) +
                          (vertex1[1] - vertex2[1]) * (vertex1[1] - vertex2[1]) +
                          (vertex1[2] - vertex2[2]) * (vertex1[2] - vertex2[2]) };
          if (distance <= mPairCut * mPairCut) {
            for (auto label : mTrackletClusters[iCluster2].getLabels()) {
              mTrackletClusters[iCluster1].add(label, mTracklets[label]);
              vertex1 = mTrackletClusters[iCluster1].getVertex();
            }
            mTrackletClusters.erase(mTrackletClusters.begin() + iCluster2);
            --iCluster2;
          }
        }
      }
      for (int iCluster{ 0 }; iCluster < mTrackletClusters.size(); ++iCluster) {
        if (mTrackletClusters[iCluster].getSize() < mClusterContributorsCut && mTrackletClusters.size() > 1) {
          mTrackletClusters.erase(mTrackletClusters.begin() + iCluster);
          continue;
        }

        mVertices.emplace_back(mTrackletClusters[iCluster].getVertex());
      }
    }
  }
}

void Vertexer::printVertices()
{
  std::cout << "Number of found vertices: " << mVertices.size() << std::endl;
  for (auto& vertex : mVertices) {
    for (int i{ 0 }; i < 3; ++i) {
      std::cout << "coord: " << i << " -> " << vertex[i] << std::endl;
    }
  }
}

} // namespace CA
} // namespace ITS
} // namespace o2