#pragma once

#include "definition.hpp"
#include "dynamicConfig.hpp"
#include "score.hpp"

// from Stockfish implementation
namespace Skill {
[[nodiscard]] inline unsigned int convertElo2Level() { 
   const float a = 2.1586e-7f;
   const float b = -0.001097f;
   const float c = 2.79303f;
   const float d = -978.848f;
   const int intcorrectedElo = int(a * pow(DynamicConfig::strength,3) + b * pow(DynamicConfig::strength,2) + c * DynamicConfig::strength + d);
   return std::max(0, (intcorrectedElo - 500) / 29 - 10); 
} ///@todo to be tuned
[[nodiscard]] inline unsigned int convertLevel2Elo() { return 29 * (DynamicConfig::level + 10) + 500; }                 ///@todo to be tuned
[[nodiscard]] inline bool         enabled() { return DynamicConfig::level > 0 && DynamicConfig::level < 100; }          // 0 is random mover !
[[nodiscard]] inline DepthType    limitedDepth() {
   return DepthType(1 + 2 * std::sqrt(std::max(0, int(DynamicConfig::level - 20))));
} ///@todo to be tuned
[[nodiscard]] inline uint64_t limitedNodes() { return uint64_t(std::exp((convertLevel2Elo() + 840.) / 240.)); } ///@todo to be tuned
[[nodiscard]] Move            pick(std::vector<RootScores>& multiPVMoves);
} // namespace Skill