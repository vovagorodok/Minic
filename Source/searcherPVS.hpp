#pragma once

#include "definition.hpp"
#include "distributed.h"
#include "dynamicConfig.hpp"
#include "egt.hpp"
#include "evalConfig.hpp"
#include "moveGen.hpp"
#include "moveSort.hpp"
#include "positionTools.hpp"
#include "searchConfig.hpp"
#include "timeMan.hpp"
#include "tools.hpp"
#include "transposition.hpp"

#define PERIODICCHECK uint64_t(1024)

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"

inline void evalDanger(const Position & p,
                       BitBoard         (&attFromPiece)[2][6],
                       BitBoard         (&att)[2],
                       BitBoard         (&att2)[2],
                       BitBoard         (&checkers)[2][6],
                       ScoreType        (&kdanger)[2]){

   const bool kingIsMandatory = DynamicConfig::isKingMandatory();
   const BitBoard pawns[2] = {p.whitePawn(), p.blackPawn()};
   const BitBoard nonPawnMat[2] = {p.allPieces[Co_White] & ~pawns[Co_White], 
                                   p.allPieces[Co_Black] & ~pawns[Co_Black]};
   const BitBoard kingZone[2]   = {isValidSquare(p.king[Co_White]) ? BBTools::mask[p.king[Co_White]].kingZone : emptyBitBoard, 
                                   isValidSquare(p.king[Co_Black]) ? BBTools::mask[p.king[Co_Black]].kingZone : emptyBitBoard};
   const BitBoard occupancy = p.occupancy();

   for (Color c = Co_White ; c <= Co_Black ; ++c){
      for (Piece pp = P_wp ; pp <= P_wk ; ++pp){
         BitBoard pieceBBiterator = p.pieces_const(c,pp);
         while (pieceBBiterator) {
            const Square k  = BB::popBit(pieceBBiterator);
            // aligned threats removing own piece (not pawn) in occupancy
            const BitBoard shadowTarget = BBTools::pfCoverage[pp - 1](k, occupancy ^ nonPawnMat[c], c);
            if (shadowTarget) {
               kdanger[~c] += BB::countBit(shadowTarget & kingZone[~c]) * EvalConfig::kingAttWeight[EvalConfig::katt_attack][pp - 1];
               const BitBoard target = BBTools::pfCoverage[pp - 1](k, occupancy, c); // real targets
               if (target) {
                  attFromPiece[c][pp - 1] |= target;
                  att2[c] |= att[c] & target;
                  att[c] |= target;
                  if (target & p.pieces_const<P_wk>(~c)) checkers[c][pp - 1] |= SquareToBitboard(k);
                  kdanger[c] -= BB::countBit(target & kingZone[c]) * EvalConfig::kingAttWeight[EvalConfig::katt_defence][pp - 1];
               }
            }
         }
      }
   }

   if(kingIsMandatory){
     const File wkf = (File)SQFILE(p.king[Co_White]);
     const File bkf = (File)SQFILE(p.king[Co_Black]);
 
     const BitBoard semiOpenFiles[2] = {BBTools::fillFile(pawns[Co_White]) & ~BBTools::fillFile(pawns[Co_Black]),
                                        BBTools::fillFile(pawns[Co_Black]) & ~BBTools::fillFile(pawns[Co_White])};
     const BitBoard openFiles        = BBTools::openFiles(pawns[Co_White], pawns[Co_Black]);
 
     kdanger[Co_White] += EvalConfig::kingAttOpenfile * BB::countBit(BB::kingFlank[wkf] & openFiles) / 8;
     kdanger[Co_White] += EvalConfig::kingAttSemiOpenfileOpp * BB::countBit(BB::kingFlank[wkf] & semiOpenFiles[Co_White]) / 8;
     kdanger[Co_White] += EvalConfig::kingAttSemiOpenfileOur * BB::countBit(BB::kingFlank[wkf] & semiOpenFiles[Co_Black]) / 8;
     kdanger[Co_Black] += EvalConfig::kingAttOpenfile * BB::countBit(BB::kingFlank[bkf] & openFiles) / 8;
     kdanger[Co_Black] += EvalConfig::kingAttSemiOpenfileOpp * BB::countBit(BB::kingFlank[bkf] & semiOpenFiles[Co_Black]) / 8;
     kdanger[Co_Black] += EvalConfig::kingAttSemiOpenfileOur * BB::countBit(BB::kingFlank[bkf] & semiOpenFiles[Co_White]) / 8;
   }

   const BitBoard weakSquare[2] = { att[Co_Black] & ~att2[Co_White] & (~att[Co_White] | attFromPiece[Co_White][P_wk - 1] | attFromPiece[Co_White][P_wq - 1]),
                                    att[Co_White] & ~att2[Co_Black] & (~att[Co_Black] | attFromPiece[Co_Black][P_wk - 1] | attFromPiece[Co_Black][P_wq - 1])};

   const BitBoard safeSquare[2] = { ~att[Co_Black] | (weakSquare[Co_Black] & att2[Co_White]),
                                    ~att[Co_White] | (weakSquare[Co_White] & att2[Co_Black])};

   // reward safe checks
   kdanger[Co_White] += EvalConfig::kingAttSafeCheck[0] * BB::countBit(checkers[Co_Black][0] & att[Co_Black]);
   kdanger[Co_Black] += EvalConfig::kingAttSafeCheck[0] * BB::countBit(checkers[Co_White][0] & att[Co_White]);
   for (Piece pp = P_wn; pp < P_wk; ++pp) {
      kdanger[Co_White] += EvalConfig::kingAttSafeCheck[pp - 1] * BB::countBit(checkers[Co_Black][pp - 1] & safeSquare[Co_Black]);
      kdanger[Co_Black] += EvalConfig::kingAttSafeCheck[pp - 1] * BB::countBit(checkers[Co_White][pp - 1] & safeSquare[Co_White]);
   }
}

#pragma GCC diagnostic pop

// this function shall be used on the initial position state before move is applied
[[nodiscard]] inline bool isNoisy(const Position & /*p*/, const Move & m){
   return Move2Type(m) != T_std;
   /*
   if ( Move2Type(m) != T_std ) return true;
   const Square from = Move2From(m);
   const Square to = Move2To(m);
   const Piece pp = p.board_const(from);
   if (pp == P_wp){
      const BitBoard pAtt = p.c == Co_White ? BBTools::pawnAttacks<Co_White>(SquareToBitboard(to)) & p.allPieces[~p.c]:
                                              BBTools::pawnAttacks<Co_Black>(SquareToBitboard(to)) & p.allPieces[~p.c];
      if ( BB::countBit(pAtt) > 1 ) return true; ///@todo verify if the pawn is protected and/or not attacked ?
   }
   else if (pp == P_wn){
      const BitBoard nAtt = BBTools::coverage<P_wn>(to, p.occupancy(), p.c) & p.allPieces[~p.c];
      if ( BB::countBit(nAtt) > 1 ) return true; ///@todo verify if the knight is protected and/or not attacked ?
   }
   return false;
   */
}

inline void Searcher::timeCheck(){
   static uint64_t periodicCheck = 0ull; ///@todo this is slow because of guard variable
   if (periodicCheck == 0ull) {
      periodicCheck = (TimeMan::maxNodes > 0) ? std::min(TimeMan::maxNodes, PERIODICCHECK) : PERIODICCHECK;
      const Counter nodeCount = ThreadPool::instance().counter(Stats::sid_nodes) + ThreadPool::instance().counter(Stats::sid_qnodes);
      if (TimeMan::maxNodes > 0 && nodeCount > TimeMan::maxNodes) {
         stopFlag = true;
         Logging::LogIt(Logging::logInfo) << "stopFlag triggered (nodes limits) in thread " << id();
      }
      if (getTimeDiff(startTime) > getCurrentMoveMs()) {
         stopFlag = true;
         Logging::LogIt(Logging::logInfo) << "stopFlag triggered in thread " << id();
      }
      Distributed::pollStat();

      if (!Distributed::isMainProcess()) {
         bool masterStopFlag;
         Distributed::get(&masterStopFlag, 1, Distributed::_winStopFromR0, 0, Distributed::_requestStopFromR0);
         Distributed::waitRequest(Distributed::_requestStopFromR0);
         ThreadPool::instance().main().stopFlag = masterStopFlag;
      }
   }
   --periodicCheck;
}

// pvs inspired by Xiphos
template<bool pvnode>
ScoreType Searcher::pvs(ScoreType                    alpha,
                        ScoreType                    beta,
                        const Position&              p,
                        DepthType                    depth,
                        DepthType                    height,
                        PVList&                      pv,
                        DepthType&                   seldepth,
                        DepthType                    extensions,
                        bool                         isInCheck,
                        bool                         cutNode,
                        const std::vector<MiniMove>* skipMoves) {

   // is updated recursively in pvs and qsearch calls but also affected to Searcher data in order to be available inside eval.
   _height = height;

   // stopFlag management and time check. Only on main thread and not at each node (see PERIODICCHECK)
   if (isMainThread()) timeCheck();
   if (stopFlag) return STOPSCORE;

   // we cannot search deeper than MAX_DEPTH, is so just return static evaluation
   EvalData data;
   if (height >= MAX_DEPTH - 1 || depth >= MAX_DEPTH - 1) return eval(p, data, *this);

   const bool rootnode = height == 0;

   // if not at root we check for draws (3rep, fifty and material)
   if (!rootnode){
      if (auto INRscore = interiorNodeRecognizer<pvnode>(p, height)) return INRscore.value();
   }

   // on pvs leaf node, call a quiet search
   if (depth <= 0) {
      // don't enter qsearch when in check
      //if (isInCheck) depth = 1;
      //else
         return qsearch(alpha, beta, p, height, seldepth, 0, true, pvnode, isInCheck);
   }

   // update selective depth
   seldepth = std::max(seldepth,height);

   // update nodes count as soon as we enter a node
   ++stats.counters[Stats::sid_nodes];
   //std::cout << GetFEN(p) << std::endl;

   debug_king_cap(p);

   if (rootnode){
      // all threads clear rootScore, this is usefull for helper like in genfen or rescore.
      rootScores.clear();
   }

   // mate distance pruning
   if(!rootnode){
      alpha = std::max(alpha, matedScore(height));
      beta  = std::min(beta, matingScore(height + 1));
      if (alpha >= beta) return alpha;
   }

   //std::cout << "=========================" << std::endl;
   //std::cout << ToString(p) << std::endl;
   CMHPtrArray cmhPtr;
   getCMHPtr(p.halfmoves, cmhPtr);

   const bool withoutSkipMove = skipMoves == nullptr;
   Hash pHash = computeHash(p);

   // consider skipmove(s) in position hash
   if (skipMoves)
      for (auto it = skipMoves->begin(); it != skipMoves->end(); ++it) { 
          assert( isValidMove(*it) );
          pHash ^= Zobrist::ZTMove[static_cast<uint32_t>(*it) + std::abs(std::numeric_limits<int16_t>::min())]; 
      }

   // probe TT
   const bool previousMoveIsNullMove = p.lastMove == NULLMOVE;
   TT::Entry  e;
   const bool ttDepthOk = TT::getEntry(*this, p, pHash, depth, e);
   TT::Bound  bound     = TT::Bound(e.b & ~TT::B_allFlags);
   // if depth of TT entry is enough
   if (ttDepthOk) {
      if (!rootnode && ((bound == TT::B_alpha && e.s <= alpha) || (bound == TT::B_beta && e.s >= beta) || (bound == TT::B_exact))) {
         if (!pvnode) {
            // increase history bonus of this move
            if (/*!isInCheck &&*/ e.m != INVALIDMINIMOVE){
               if (Move2Type(e.m) == T_std) // quiet move history
                  updateTables(*this, p, depth, height, e.m, bound, cmhPtr);
               else if ( isCapture(e.m) ) // capture history
                  historyT.updateCap<1>(depth, e.m, p);
            }
            if (p.fifty < SearchConfig::ttMaxFiftyValideDepth) return adjustHashScore(e.s, height);
         }
         ///@todo try returning also at pv node (this cuts pv ...)
         /*
            else{ // in "good" condition, also return a score at pvnode
               if ( bound == TT::B_exact && e.d > 3*depth/2 && p.fifty < SearchConfig::ttMaxFiftyValideDepth) return adjustHashScore(e.s, height);
            }
         */
      }
   }

   // if entry hash is not null and entry move is valid, this is a valid TT move (we don't care about depth here !)
   bool ttHit       = e.h != nullHash;
   bool validTTmove = ttHit && e.m != INVALIDMINIMOVE;
   bool ttPV        = pvnode || (validTTmove && (e.b & TT::B_ttPVFlag)); ///@todo store more things in TT bound ...
   bool ttIsCheck   = validTTmove && (e.b & TT::B_isCheckFlag);
   bool formerPV    = ttPV && !pvnode;

#ifdef WITH_SYZYGY
   // probe TB
   ScoreType tbScore = 0;
   if (!rootnode && withoutSkipMove && (BB::countBit(p.allPieces[Co_White] | p.allPieces[Co_Black])) <= SyzygyTb::MAX_TB_MEN &&
       SyzygyTb::probe_wdl(p, tbScore, false) > 0) {
      ++stats.counters[Stats::sid_tbHit1];
      // if this is a winning/losing EGT position, we add up static evaluation
      // this allow to go for mate better or to defend longer
      if (abs(tbScore) == SyzygyTb::TB_WIN_SCORE) {
         tbScore += eval(p, data, *this);
         tbScore = clampScore(tbScore);
      }
      else if (abs(tbScore) == SyzygyTb::TB_CURSED_SCORE) {
         tbScore = drawScore(p, height);
      }

      // store TB hits into TT (without associated move, but with max depth)
      TT::setEntry(*this, pHash, INVALIDMOVE, createHashScore(tbScore, height), createHashScore(tbScore, height), TT::B_none, DepthType(MAX_DEPTH));
      return tbScore;
   }
#endif

   assert(p.halfmoves - 1 < MAX_PLY && p.halfmoves - 1 >= 0);

   // get a static score for the position.
   ScoreType evalScore;
   // do not eval position when in check, we won't use it (won't forward prune)
   if (isInCheck) evalScore = matedScore(height);
   // skip eval if nullmove just applied we can hack
   // Won't work with asymetric HalfKA NNUE
   else if (!DynamicConfig::useNNUE && previousMoveIsNullMove && height > 0){
      evalScore = 2 * EvalConfig::tempo - stack[p.halfmoves - 1].eval;
#ifdef DEBUG_STATICEVAL
      checkEval(p,evalScore,*this,"null move trick (pvs)");
#endif
   }
   else {
      // if we had a TT hit (with or without associated move), we can use its eval instead of calling eval()
      if (ttHit) {
         stats.incr(Stats::sid_ttschits);
         evalScore = e.e;
         // look for a material match (to get game phase)
         const Hash matHash = MaterialHash::getMaterialHash(p.mat);
         if (matHash != nullHash) {
            stats.incr(Stats::sid_materialTableHitsSearch);
            const MaterialHash::MaterialHashEntry& MEntry = MaterialHash::materialHashTable[matHash];
            data.gp = MEntry.gamePhase();
         }
         else { // if no match, compute game phase (this does not happend very often ...)
            ScoreType matScoreW = 0;
            ScoreType matScoreB = 0;
            data.gp = gamePhase(p.mat, matScoreW, matScoreB);
            stats.incr(Stats::sid_materialTableMissSearch);
         }
#ifdef DEBUG_STATICEVAL
         checkEval(p,evalScore,*this,"from TT (pvs)");
#endif
      }
      else { // if no TT hit call evaluation !
         stats.incr(Stats::sid_ttscmiss);
         evalScore = eval(p, data, *this);
#ifdef DEBUG_STATICEVAL
         checkEval(p,evalScore,*this,"from eval (pvs)");
 #endif
      }
   }
   stack[p.halfmoves].eval = evalScore; // insert only static eval, never hash score !
   //stack[p.halfmoves].data = data;

   // take **initial** position situation into account ///@todo use this
   const bool isEmergencyDefence = false; //moveDifficulty == MoveDifficultyUtil::MD_hardDefense;
   const bool isEmergencyAttack  = false; //moveDifficulty == MoveDifficultyUtil::MD_hardAttack;

   // take **current** position danger level into account
   BitBoard attFromPiece[2][6] = {{emptyBitBoard}};
   BitBoard att[2] = {emptyBitBoard,emptyBitBoard};
   BitBoard att2[2] = {emptyBitBoard,emptyBitBoard};
   BitBoard checkers[2][6] = {{emptyBitBoard}};
   if (!data.evalDone){
      // no eval has been done, we need to work a little to get danger data
      evalDanger(p,attFromPiece,att,att2,checkers,data.danger);
      ///@todo mobility ?

      data.haveThreats[Co_White] = (att[Co_White] & p.allPieces[Co_Black]) != emptyBitBoard;
      data.haveThreats[Co_Black] = (att[Co_Black] & p.allPieces[Co_White]) != emptyBitBoard;

      data.goodThreats[Co_White] = ((attFromPiece[Co_White][P_wp-1] & p.allPieces[Co_Black] & ~p.blackPawn())
                                  | (attFromPiece[Co_White][P_wn-1] & (p.blackQueen() | p.blackRook()))
                                  | (attFromPiece[Co_White][P_wb-1] & (p.blackQueen() | p.blackRook()))
                                  | (attFromPiece[Co_White][P_wr-1] & p.blackQueen())) != emptyBitBoard;
      data.goodThreats[Co_Black] = ((attFromPiece[Co_Black][P_wp-1] & p.allPieces[Co_White] & ~p.whitePawn())
                                  | (attFromPiece[Co_Black][P_wn-1] & (p.whiteQueen() | p.whiteRook()))
                                  | (attFromPiece[Co_Black][P_wb-1] & (p.whiteQueen() | p.whiteRook()))
                                  | (attFromPiece[Co_Black][P_wr-1] & p.whiteQueen())) != emptyBitBoard;
   }
   const int dangerFactor          = (data.danger[Co_White] + data.danger[Co_Black]) / SearchConfig::dangerDivisor;
   const int dangerDiff            = (data.danger[~p.c] - data.danger[p.c]) / SearchConfig::dangerDivisor;
   const bool isDangerPrune        = dangerFactor >= SearchConfig::dangerLimitPruning;
   const bool isDangerForwardPrune = dangerFactor >= SearchConfig::dangerLimitForwardPruning;
   const bool isDangerRed          = dangerFactor >= SearchConfig::dangerLimitReduction;
   if (isDangerPrune)        stats.incr(Stats::sid_dangerPrune);
   if (isDangerForwardPrune) stats.incr(Stats::sid_dangerPrune);
   if (isDangerRed)          stats.incr(Stats::sid_dangerReduce);

   bool evalScoreIsHashScore = false;
   const ScoreType staticScore = evalScore;
   // if no TT hit yet, we insert an eval without a move here in case of forward pruning (depth is negative, bound is none) ...
   // Be carefull here, _data2 in Entry is always (INVALIDMOVE,B_none,-2) here, so that collisions are a lot more likely
   if (!ttHit) TT::setEntry(*this, pHash, INVALIDMOVE, createHashScore(evalScore, height), createHashScore(staticScore, height), TT::B_none, -2);

   // if TT hit, we can use its score as a best draft (but we set evalScoreIsHashScore to be aware of that !)
   // note that e.d >= -1 here is quite redondant with bound != TT::B_None, but anyway ...
   if (ttHit && !isInCheck && /*e.d >= -1 && */
      ((bound == TT::B_alpha && e.s < evalScore) || (bound == TT::B_beta && e.s > evalScore) || (bound == TT::B_exact))){
     evalScore = adjustHashScore(e.s, height);
     evalScoreIsHashScore = true;
     stats.incr(Stats::sid_staticScoreIsFromSearch);
   }

   ScoreType       bestScore = matedScore(height);
   MoveList        moves;
   bool            moveGenerated    = false;
   bool            capMoveGenerated = false;
   bool            futility = false, lmp = false, /*mateThreat = false,*/ historyPruning = false, capHistoryPruning = false, CMHPruning = false;
   MiniMove        refutation       = INVALIDMINIMOVE;
   DepthType       marginDepth      = std::max(1, depth - (evalScoreIsHashScore ? e.d : 0)); // a depth that take TT depth into account
   const bool      isNotPawnEndGame = p.mat[p.c][M_t] > 0;
   const bool      improving        = (!isInCheck && height > 1 && stack[p.halfmoves].eval >= stack[p.halfmoves - 2].eval);
   const ScoreType alphaInit        = alpha;

   // forward prunings
   if (!DynamicConfig::mateFinder && !rootnode && !isInCheck && !pvnode /*&& !isMateScore(beta)*/) { // removing the !isMateScore(beta) is not losing that much elo and allow for better check mate finding ...

      // static null move
      if (SearchConfig::doStaticNullMove && !isMateScore(evalScore) && isNotPawnEndGame && SearchConfig::staticNullMoveCoeff.isActive(depth, evalScoreIsHashScore) ) {
         const ScoreType margin = SearchConfig::staticNullMoveCoeff.threshold(marginDepth, data.gp, evalScoreIsHashScore, improving);
         if (evalScore >= beta + margin) return stats.incr(Stats::sid_staticNullMove), evalScore - margin;
      }

      // (absence of) Opponent threats pruning (idea origin from Koivisto)
      if ( SearchConfig::doThreatsPruning && !isMateScore(evalScore) && isNotPawnEndGame && SearchConfig::threatCoeff.isActive(depth, evalScoreIsHashScore) &&
           !data.goodThreats[~p.c] && evalScore > beta + SearchConfig::threatCoeff.threshold(depth, data.gp, evalScoreIsHashScore, improving) ){
         return stats.incr(Stats::sid_threatsPruning), beta;
      }

/*
      // Own threats pruning
      if ( SearchConfig::doThreatsPruning && !isMateScore(evalScore) && isNotPawnEndGame && SearchConfig::ownThreatCoeff.isActive(depth, evalScoreIsHashScore) &&
           data.goodThreats[p.c] && !data.goodThreats[~p.c] && evalScore > beta + SearchConfig::ownThreatCoeff.threshold(depth, data.gp, evalScoreIsHashScore, improving) ){
         return stats.incr(Stats::sid_threatsPruning), beta;
      }
*/
      // razoring
      const ScoreType rAlpha = alpha - SearchConfig::razoringCoeff.threshold(marginDepth, data.gp, evalScoreIsHashScore, improving);
      if (SearchConfig::doRazoring && SearchConfig::razoringCoeff.isActive(depth, evalScoreIsHashScore) && evalScore <= rAlpha) {
         stats.incr(Stats::sid_razoringTry);
         const ScoreType qScore = qsearch(alpha, beta, p, height, seldepth, 0, true, pvnode, isInCheck);
         if (stopFlag) return STOPSCORE;
         if (depth < 2 && evalScoreIsHashScore) return stats.incr(Stats::sid_razoringNoQ), qScore;
         if (qScore <= alpha) return stats.incr(Stats::sid_razoring), qScore;
      }

      // null move (warning, mobility info is only available if no TT hit)
      if (SearchConfig::doNullMove && !subSearch && 
          depth >= SearchConfig::nullMoveMinDepth &&
          (isNotPawnEndGame || data.mobility[p.c] > 4) && 
          withoutSkipMove &&
          evalScore >= beta + SearchConfig::nullMoveMargin && 
          evalScore >= stack[p.halfmoves].eval &&
          stack[p.halfmoves].p.lastMove != NULLMOVE && 
          (height >= nullMoveMinPly || nullMoveMinPly != p.c)) {
         PVList nullPV;
         stats.incr(Stats::sid_nullMoveTry);
         const DepthType R = depth / 4 + 3 + std::min((evalScore - beta) / SearchConfig::nullMoveDynamicDivisor, 5); // adaptative
         ///@todo try to minimize sid_nullMoveTry2 versus sid_nullMove
         const ScoreType nullIIDScore =
             evalScore; // pvs<false, false>(beta - 1, beta, p, std::max(depth/4,1), evaluator, height, nullPV, seldepth, isInCheck, !cutNode);
         if (nullIIDScore >= beta + SearchConfig::nullMoveMargin2) {
            TT::Entry       nullE;
            const DepthType nullDepth = depth - R;
            TT::getEntry(*this, p, pHash, nullDepth, nullE);
            if (nullE.h == nullHash ||
                nullE.s >= beta) { // avoid null move search if TT gives a score < beta for the same depth ///@todo check this again !
               stats.incr(Stats::sid_nullMoveTry2);
               Position pN = p;
               applyNull(*this, pN);
               assert(pN.halfmoves < MAX_PLY && pN.halfmoves >= 0);
               stack[pN.halfmoves].p = pN; ///@todo another expensive copy !!!!
               stack[pN.halfmoves].h = pN.h;
               ScoreType nullscore   = -pvs<false>(-beta, -beta + 1, pN, nullDepth, height + 1, nullPV, seldepth, extensions, isInCheck, !cutNode);
               if (stopFlag) return STOPSCORE;
               TT::Entry nullEThreat;
               TT::getEntry(*this, pN, computeHash(pN), 0, nullEThreat);
               if (nullEThreat.h != nullHash && nullEThreat.m != INVALIDMINIMOVE) refutation = nullEThreat.m;
               //if (isMatedScore(nullscore)) mateThreat = true;
               if (nullscore >= beta) { // verification search
                  if ( (!isNotPawnEndGame || depth > SearchConfig::nullMoveVerifDepth) && nullMoveMinPly == 0){
                     stats.incr(Stats::sid_nullMoveTry3);
                     nullMoveMinPly = height + 3*nullDepth/4;
                     nullMoveVerifColor = p.c;
                     nullscore = pvs<false>(beta - 1, beta, p, nullDepth, height+1, nullPV, seldepth, extensions, isInCheck, false);
                     nullMoveMinPly = 0;
                     nullMoveVerifColor = Co_None;
                     if (stopFlag) return STOPSCORE;
                     if (nullscore >= beta ) return stats.incr(Stats::sid_nullMove2), nullscore;
                  }
                  else{
                     return stats.incr(Stats::sid_nullMove), (isMateScore(nullscore) ? beta : nullscore);
                  }
               }
            }
         }
      }

      // ProbCut
      if (SearchConfig::doProbcut && depth >= SearchConfig::probCutMinDepth && !isMateScore(beta) && data.haveThreats[p.c]) {
         stats.incr(Stats::sid_probcutTry);
         int probCutCount = 0;
         const ScoreType betaPC = beta + SearchConfig::probCutMargin;
         capMoveGenerated = true;
         MoveGen::generate<MoveGen::GP_cap>(p, moves);
#ifdef USE_PARTIAL_SORT
         MoveSorter::score(*this, moves, p, data.gp, height, cmhPtr, true, isInCheck, validTTmove ? &e : NULL);
         size_t offset = 0;
         const Move* it = nullptr;
         while ((it = MoveSorter::pickNext(moves, offset)) && probCutCount < SearchConfig::probCutMaxMoves /*+ 2*cutNode*/) {
#else
         MoveSorter::scoreAndSort(*this, moves, p, data.gp, height, cmhPtr, true, isInCheck, validTTmove ? &e : NULL);
         for (auto it = moves.begin(); it != moves.end() && probCutCount < SearchConfig::probCutMaxMoves /*+ 2*cutNode*/; ++it) {
#endif
            if ((validTTmove && sameMove(e.m, *it)) || isBadCap(*it)) continue; // skip TT move if quiet or bad captures
            Position p2 = p;
#ifdef WITH_NNUE
            NNUEEvaluator newEvaluator = p.evaluator();
            p2.associateEvaluator(newEvaluator);
#endif
            if (!applyMove(p2, *it)) continue;
            ++probCutCount;
            ScoreType scorePC = -qsearch(-betaPC, -betaPC + 1, p2, height + 1, seldepth, 0, true, pvnode);
            PVList pcPV;
            if (stopFlag) return STOPSCORE;
            if (scorePC >= betaPC) {
               stats.incr(Stats::sid_probcutTry2);
               scorePC = -pvs<false>(-betaPC, -betaPC + 1, p2, depth - SearchConfig::probCutMinDepth + 1, height + 1, pcPV, seldepth, extensions, 
                                     isAttacked(p2, kingSquare(p2)), !cutNode);
            }
            if (stopFlag) return STOPSCORE;
            if (scorePC >= betaPC) return stats.incr(Stats::sid_probcut), scorePC;
         }
      }
   }

   // Ed style IIR
   if ( SearchConfig::doIIR && depth >= 4 && !validTTmove) depth -= 1;

   // Classic IID
   if (SearchConfig::doIID && !validTTmove /*|| e.d < depth-4*/) {
      if (((pvnode && depth >= SearchConfig::iidMinDepth) || (cutNode && depth >= SearchConfig::iidMinDepth2))) { ///@todo try with cutNode only ?
         stats.incr(Stats::sid_iid);
         PVList iidPV;
         DISCARD pvs<pvnode>(alpha, beta, p, depth / 2, height, iidPV, seldepth, extensions, isInCheck, cutNode, skipMoves);
         if (stopFlag) return STOPSCORE;
         TT::getEntry(*this, p, pHash, 0, e);
         ttHit       = e.h != nullHash;
         validTTmove = ttHit && e.m != INVALIDMINIMOVE;
         bound       = TT::Bound(e.b & ~TT::B_allFlags);
         ttPV        = pvnode || (ttHit && (e.b & TT::B_ttPVFlag));
         ttIsCheck   = validTTmove && (e.b & TT::B_isCheckFlag);
         formerPV    = ttPV && !pvnode;
         // note that e.d >= -1 here is quite redondant with bound != TT::B_None, but anyway ...
         if (ttHit && !isInCheck && /*e.d >= -1 &&*/
             ((bound == TT::B_alpha && e.s < evalScore) || (bound == TT::B_beta && e.s > evalScore) || (bound == TT::B_exact))) {
            evalScore            = adjustHashScore(e.s, height);
            evalScoreIsHashScore = true;
            marginDepth          = std::max(1, depth - (evalScoreIsHashScore ? e.d : 0)); // a depth that take TT depth into account
         }
      }
   }

   // reset killer
   killerT.killers[height + 1][0] = killerT.killers[height + 1][1] = 0; ///@todo just use INVALIDMOVE

   if (!rootnode) {
      // LMP
      lmp = SearchConfig::doLMP && depth <= SearchConfig::lmpMaxDepth;
      // futility
      const ScoreType futilityScore =
          alpha - SearchConfig::futilityPruningCoeff.threshold(marginDepth, data.gp, evalScoreIsHashScore, improving);
      futility = SearchConfig::doFutility && SearchConfig::futilityPruningCoeff.isActive(depth,evalScoreIsHashScore) && evalScore <= futilityScore;
      // history pruning
      historyPruning = SearchConfig::doHistoryPruning && isNotPawnEndGame && SearchConfig::historyPruningCoeff.isActive(depth, improving);
      // CMH pruning
      CMHPruning = SearchConfig::doCMHPruning && isNotPawnEndGame && depth < SearchConfig::CMHMaxDepth[improving];
      // capture history pruning
      capHistoryPruning = SearchConfig::doCapHistoryPruning && isNotPawnEndGame && SearchConfig::captureHistoryPruningCoeff.isActive(depth, improving);
   }

   int       validMoveCount      = 0;
   int       validQuietMoveCount = 0;
   int       validNonPrunedCount = 0;
   Move      bestMove            = INVALIDMOVE;
   TT::Bound hashBound           = TT::B_alpha;
   bool      ttMoveIsCapture     = false;
   //bool ttMoveSingularExt = false;

   stack[p.halfmoves].threat = refutation;

   bool bestMoveIsCheck = false;

   const bool BMextension = DynamicConfig::useNNUE && height > 1 && 
                            isValidMove(stack[p.halfmoves].threat) &&
                            isValidMove(stack[p.halfmoves - 2].threat) &&
                            (sameMove(stack[p.halfmoves].threat, stack[p.halfmoves - 2].threat) ||
                             (correctedMove2ToKingDest(stack[p.halfmoves].threat) == correctedMove2ToKingDest(stack[p.halfmoves - 2].threat) && isCapture(stack[p.halfmoves].threat)));

   // try the tt move before move generation (if not skipped move)
   if (validTTmove && !isSkipMove(e.m, skipMoves)) { // should be the case thanks to iid at pvnode
      bestMove = e.m;                                // in order to preserve tt move for alpha bound entry
#ifdef DEBUG_APPLY
      // to debug race condition in entry affectation !
      if (!isPseudoLegal(p, e.m)) { Logging::LogIt(Logging::logFatal) << "invalide TT move !"; }
#endif
      Position p2 = p;
#ifdef WITH_NNUE
      NNUEEvaluator newEvaluator = p.evaluator();
      p2.associateEvaluator(newEvaluator);
#endif
      if (applyMove(p2, e.m)) {
         TT::prefetch(computeHash(p2));
         //const Square to = Move2To(e.m);
         ++validMoveCount;
         ++validNonPrunedCount;
         const bool isQuiet = Move2Type(e.m) == T_std && !isNoisy(p,e.m);
         if (isQuiet) ++validQuietMoveCount;
         PVList childPV;
         assert(p2.halfmoves < MAX_PLY && p2.halfmoves >= 0);
         stack[p2.halfmoves].p = p2; ///@todo another expensive copy !!!!
         stack[p2.halfmoves].h = p2.h;
         const bool isCheck    = ttIsCheck || isAttacked(p2, kingSquare(p2));
#ifdef DEBUG_TT_CHECK
         if (ttIsCheck && !isAttacked(p2, kingSquare(p2))){
            std::cout << "Error ttIsCheck" << std::endl;
            std::cout << ToString(p2) << std::endl;
         }
#endif
         if (isCapture(e.m)) ttMoveIsCapture = true;
         //const bool isAdvancedPawnPush = PieceTools::getPieceType(p,Move2From(e.m)) == P_wp && (SQRANK(to) > 5 || SQRANK(to) < 2);
         // extensions
         DepthType extension = 0;
         if (DynamicConfig::level > 80) {
            // singular move extension
            if (EXTENDMORE && withoutSkipMove && depth >= SearchConfig::singularExtensionDepth && !rootnode && !isMateScore(e.s) &&
                (bound == TT::B_exact || bound == TT::B_beta) && e.d >= depth - 3) {
               const ScoreType betaC = e.s - 2 * depth;
               PVList          sePV;
               DepthType       seSeldetph = 0;
               std::vector<MiniMove> skip({e.m});
               const ScoreType       score = pvs<false>(betaC - 1, betaC, p, depth / 2, height, sePV, seSeldetph, extensions, isInCheck, cutNode, &skip);
               if (stopFlag) return STOPSCORE;
               if (score < betaC) { // TT move is singular
                  stats.incr(Stats::sid_singularExtension), /*ttMoveSingularExt=!ttMoveIsCapture,*/ ++extension;
                  // TT move is "very singular" : kind of single reply extension
                  if (score < betaC - 4 * depth) {
                     stats.incr(Stats::sid_singularExtension2);
                     ++extension;
                  }
               }
               // multi cut (at least the TT move and another move are above beta)
               else if (betaC >= beta) {
                  return stats.incr(Stats::sid_singularExtension3), betaC; // fail-soft
               }
               // if TT move is above beta, we try a reduce search early to see if another move is above beta (from SF)
               else if (e.s >= beta) {
                  const ScoreType score2 = pvs<false>(beta - 1, beta, p, depth - 4, height, sePV, seSeldetph, extensions, isInCheck, cutNode, &skip);
                  if (score2 > beta) return stats.incr(Stats::sid_singularExtension4), beta; // fail-hard
               }
            }
            // is in check extension if pvnode
            //if (EXTENDMORE && pvnode && isInCheck && depth < 8) stats.incr(Stats::sid_checkExtension),++extension;
            // castling extension
            //if (EXTENDMORE && isCastling(e.m) ) stats.incr(Stats::sid_castlingExtension),++extension;
            // Botvinnik-Markoff Extension
            if (EXTENDMORE && BMextension) stats.incr(Stats::sid_BMExtension), ++extension;
            // mate threat extension (from null move)
            //if (EXTENDMORE && mateThreat) stats.incr(Stats::sid_mateThreatExtension),++extension;
            // simple recapture extension
            //if (EXTENDMORE && pvnode && isValidMove(p.lastMove) && Move2Type(p.lastMove) == T_capture && to == Move2To(p.lastMove)) stats.incr(Stats::sid_recaptureExtension),++extension; // recapture
            // gives check extension
            //if (EXTENDMORE && isCheck ) stats.incr(Stats::sid_checkExtension2),++extension; // we give check with a non risky move
            /*
            // CMH extension
            if (EXTENDMORE && isQuiet) {
                if (isCMHGood(p, Move2From(e.m), to, cmhPtr, HISTORY_MAX / 2) stats.incr(Stats::sid_CMHExtension), ++extension;
            }
            */
            // advanced pawn push extension
            /*
            if (EXTENDMORE && isAdvancedPawnPush ) {
                const BitBoard pawns[2] = { p2.pieces_const<P_wp>(Co_White), p2.pieces_const<P_wp>(Co_Black) };
                const BitBoard passed[2] = { BBTools::pawnPassed<Co_White>(pawns[Co_White], pawns[Co_Black]), BBTools::pawnPassed<Co_Black>(pawns[Co_Black], pawns[Co_White]) };
                if ( SquareToBitboard(to) & passed[p.c] ) stats.incr(Stats::sid_pawnPushExtension), ++extension;
            }
            */
            // threat on queen extension
            //if (EXTENDMORE && pvnode && (p.pieces_const<P_wq>(p.c) && isQuiet && PieceTools::getPieceType(p, Move2From(e.m)) == P_wq && isAttacked(p, BBTools::SquareFromBitBoard(p.pieces_const<P_wq>(p.c)))) && SEE_GE(p, e.m, 0)) stats.incr(Stats::sid_queenThreatExtension), ++extension;
         }
         ScoreType ttScore = -pvs<pvnode>(-beta, -alpha, p2, depth - 1 + extension, height + 1, childPV, seldepth, static_cast<DepthType>(extensions + extension), isCheck, !cutNode);
         if (stopFlag) return STOPSCORE;
         if (rootnode) { rootScores.push_back({e.m, ttScore}); }
         if (ttScore > bestScore) {
            if (rootnode) previousBest = e.m;
            bestScore = ttScore;
            bestMove  = e.m;
            bestMoveIsCheck = isCheck;
            if (ttScore > alpha) {
               hashBound = TT::B_exact;
               if (pvnode) updatePV(pv, bestMove, childPV);
               if (ttScore >= beta) {
                  stats.incr(Stats::sid_ttbeta);

                  // increase history bonus of this move
                  if (!isInCheck){
                     if (isQuiet /*&& depth > 1*/) // quiet move history
                        updateTables(*this, p, depth + (ttScore > (beta + SearchConfig::betaMarginDynamicHistory)), height, bestMove, TT::B_beta, cmhPtr);
                     else if (isCapture(bestMove)) // capture history
                        historyT.updateCap<1>(depth + (ttScore > (beta + SearchConfig::betaMarginDynamicHistory)), bestMove, p);
                  }

                  TT::setEntry(*this, pHash, bestMove, createHashScore(ttScore, height), createHashScore(evalScore, height),
                                      TT::Bound(TT::B_beta |
                                      (ttPV ? TT::B_ttPVFlag : TT::B_none) |
                                      (bestMoveIsCheck ? TT::B_isCheckFlag : TT::B_none) |
                                      (isInCheck ? TT::B_isInCheckFlag : TT::B_none)),
                                      depth);
                  return ttScore;
               }
               stats.incr(Stats::sid_ttalpha);
               alpha = ttScore;
            }
         }
         else if (rootnode && !isInCheck && ttScore < alpha - SearchConfig::failLowRootMargin /*&& !isMateScore(ttScore)*/) {
            return alpha - SearchConfig::failLowRootMargin;
         }
      }
   }

#ifdef WITH_SYZYGY
   if (rootnode && withoutSkipMove && (BB::countBit(p.allPieces[Co_White] | p.allPieces[Co_Black])) <= SyzygyTb::MAX_TB_MEN) {
      tbScore = 0;
      if (SyzygyTb::probe_root(*this, p, tbScore, moves) < 0) { // only good moves if TB success
         if (capMoveGenerated) MoveGen::generate<MoveGen::GP_quiet>(p, moves, true);
         else
            if ( isInCheck ) MoveGen::generate<MoveGen::GP_evasion>(p, moves, false);
            else MoveGen::generate<MoveGen::GP_all>(p, moves, false);
      }
      else
         ++stats.counters[Stats::sid_tbHit2];
      moveGenerated = true;
   }
#endif

   ScoreType score = matedScore(height);
   bool skipQuiet = false;
   bool skipCap = false;

   // depending if probecut already generates capture or not, generate all moves or only quiets
   if (!moveGenerated) {
      if (capMoveGenerated) MoveGen::generate<MoveGen::GP_quiet>(p, moves, true);
      else
         if ( isInCheck ) MoveGen::generate<MoveGen::GP_evasion>(p, moves, false);
         else MoveGen::generate<MoveGen::GP_all>(p, moves, false);
   }
   if (moves.empty()) return isInCheck ? matedScore(height) : drawScore(p, height);

#ifdef USE_PARTIAL_SORT
   MoveSorter::score(*this, moves, p, data.gp, height, cmhPtr, true, isInCheck, validTTmove ? &e : NULL,
                     refutation != INVALIDMINIMOVE && isCapture(Move2Type(refutation)) ? refutation : INVALIDMINIMOVE);
   size_t offset = 0;
   const Move* it = nullptr;
   while ((it = MoveSorter::pickNext(moves, offset)) && !stopFlag) {
#else
   MoveSorter::scoreAndSort(*this, moves, p, data.gp, height, cmhPtr, true, isInCheck, validTTmove ? &e : NULL,
                            refutation != INVALIDMINIMOVE && isCapture(Move2Type(refutation)) ? refutation : INVALIDMINIMOVE);
   for (auto it = moves.begin(); it != moves.end() && !stopFlag; ++it) {
#endif
      const bool isQuiet = Move2Type(*it) == T_std && !isNoisy(p,*it);
      // skip quiet if LMP was triggered (!!even if move gives check now!!)
      if (skipQuiet && isQuiet && !isInCheck){
         stats.incr(Stats::sid_lmp);
         continue;
      }
      // skip other bad capture (!!even if move gives check now!!)
      if (skipCap && Move2Type(*it) == T_capture && !isInCheck){
         stats.incr(Stats::sid_see2);
         continue;
      }
      if (isSkipMove(*it, skipMoves)) continue;        // skipmoves
      if (validTTmove && sameMove(e.m, *it)) continue; // already tried
      Position p2 = p;
#ifdef WITH_NNUE
      NNUEEvaluator newEvaluator = p.evaluator();
      p2.associateEvaluator(newEvaluator);
#endif
      if (!applyMove(p2, *it)) continue;
      TT::prefetch(computeHash(p2));
      const Square to = Move2To(*it);
#ifdef DEBUG_KING_CAP
      if (p.c == Co_White && to == p.king[Co_Black]) return matingScore(height - 1);
      if (p.c == Co_Black && to == p.king[Co_White]) return matingScore(height - 1);
#endif
      ++validMoveCount;
      if (isQuiet) ++validQuietMoveCount;
      const bool firstMove = validMoveCount == 1;
      PVList     childPV;
      stack[p2.halfmoves].p = p2; ///@todo another expensive copy !!!!
      stack[p2.halfmoves].h = p2.h;
      const bool isCheck    = isAttacked(p2, kingSquare(p2));
      bool       isAdvancedPawnPush = PieceTools::getPieceType(p, Move2From(*it)) == P_wp && (SQRANK(to) > 5 || SQRANK(to) < 2);
      // extensions
      DepthType extension = 0;
      if (DynamicConfig::level > 80) {
         // is in check extension if pvnode
         //if (EXTENDMORE && pvnode && isInCheck && depth < 8) stats.incr(Stats::sid_checkExtension),++extension; // we are in check (extension)
         // castling extension
         //if (EXTENDMORE && isCastling(*it) ) stats.incr(Stats::sid_castlingExtension),++extension;
         // Botvinnik-Markoff Extension
         //if (EXTENDMORE && BMextension) stats.incr(Stats::sid_BMExtension), ++extension;
         // mate threat extension (from null move)
         //if (EXTENDMORE && mateThreat && depth <= 4) stats.incr(Stats::sid_mateThreatExtension),++extension;
         // simple recapture extension
         //if (EXTENDMORE && pvnode && isValidMove(p.lastMove) && Move2Type(p.lastMove) == T_capture && !isBadCap(*it) && to == Move2To(p.lastMove)) stats.incr(Stats::sid_recaptureExtension),++extension; //recapture
         // gives check extension
         //if (EXTENDMORE && isCheck ) stats.incr(Stats::sid_checkExtension2),++extension; // we give check
         // CMH extension
         /*
         if (EXTENDMORE && isQuiet) {
             if (isCMHGood(p, Move2From(*it), to, cmhPtr, HISTORY_MAX / 2)) stats.incr(Stats::sid_CMHExtension), ++extension;
         }
         */
         // advanced pawn push extension
         /*
         if (EXTENDMORE && isAdvancedPawnPush && killerT.isKiller(*it, height) ){
            const BitBoard pawns[2] = { p2.pieces_const<P_wp>(Co_White), p2.pieces_const<P_wp>(Co_Black) };
            const BitBoard passed[2] = { BBTools::pawnPassed<Co_White>(pawns[Co_White], pawns[Co_Black]), BBTools::pawnPassed<Co_Black>(pawns[Co_Black], pawns[Co_White]) };
            if ( (SquareToBitboard(to) & passed[p.c]) ) stats.incr(Stats::sid_pawnPushExtension), ++extension;
         }
         */
         // threat on queen extension
         //if (EXTENDMORE && pvnode && firstMove && (p.pieces_const<P_wq>(p.c) && isQuiet && Move2Type(*it) == T_std && PieceTools::getPieceType(p, Move2From(*it)) == P_wq && isAttacked(p, BBTools::SquareFromBitBoard(p.pieces_const<P_wq>(p.c)))) && SEE_GE(p, *it, 0)) stats.incr(Stats::sid_queenThreatExtension), ++extension;
         // move that lead to endgame
         /*
         if ( EXTENDMORE && isNotPawnEndGame && (p2.mat[p.c][M_t]+p2.mat[~p.c][M_t] == 0)){
            ++extension;
            stats.incr(Stats::sid_endGameExtension);
         }
         */
         // extend if quiet with good history
         /*
         if ( EXTENDMORE && isQuiet && Move2Score(*it) > SearchConfig::historyExtensionThreshold){
            ++extension;
            stats.incr(Stats::sid_goodHistoryExtension);
         }
         */
      }
      // pvs
      if (validMoveCount < (2 /*+2*rootnode*/) || !SearchConfig::doPVS){
         score = -pvs<pvnode>(-beta, -alpha, p2, depth - 1 + extension, height + 1, childPV, seldepth, static_cast<DepthType>(extensions + extension), isCheck, false);
         ++validNonPrunedCount;
      }
      else {
         // reductions & prunings
         const bool isPrunable           = /*isNotPawnEndGame &&*/ !DynamicConfig::mateFinder && !isAdvancedPawnPush && !isMateScore(alpha) && !killerT.isKiller(*it, height);
         const bool isReductible         = /*isNotPawnEndGame &&*/ !isAdvancedPawnPush && !DynamicConfig::mateFinder;
         const bool noCheck              = !isInCheck && !isCheck;
         const bool isPrunableStd        = isPrunable && isQuiet;
         const bool isPrunableStdNoCheck = isPrunableStd && noCheck;
         const bool isPrunableCap        = isPrunable && Move2Type(*it) == T_capture && !isInCheck;
         const bool isPrunableBadCap     = isPrunableCap && isBadCap(*it);

         // futility
         if (futility && isPrunableStdNoCheck) {
            stats.incr(Stats::sid_futility);
            continue;
         }

         const DepthType pruningDepthCorrection = 0;//DepthType(float(dangerFactor)/SearchConfig::dangerLimitPruning + (isEmergencyDefence||isEmergencyAttack) - 0.5);

         // LMP
         if (lmp && isPrunableStdNoCheck && validMoveCount > SearchConfig::lmpLimit[improving][depth + pruningDepthCorrection]) {
            stats.incr(Stats::sid_lmp);
            skipQuiet = true;
            continue;
         }

         // History pruning (including CMH)
         if (historyPruning && isPrunableStdNoCheck &&
             Move2Score(*it) < SearchConfig::historyPruningCoeff.threshold(depth + pruningDepthCorrection, data.gp, improving, cutNode)) {
            stats.incr(Stats::sid_historyPruning);
            continue;
         }

         // CMH pruning alone
         if (CMHPruning && isPrunableStdNoCheck) {
            if (isCMHBad(p, Move2From(*it), Move2To(*it), cmhPtr, -HISTORY_MAX/4)) {
               stats.incr(Stats::sid_CMHPruning);
               continue;
            }
         }

         // capture history pruning
         if ( capHistoryPruning && isPrunableCap &&
              historyT.historyCap[PieceIdx(p.board_const(Move2From(*it)))][to][Abs(p.board_const(to))-1] < SearchConfig::captureHistoryPruningCoeff.threshold(depth + pruningDepthCorrection, data.gp, improving, cutNode)){
            stats.incr(Stats::sid_capHistPruning);
            continue;
         }

         // SEE (capture)
         if (isPrunableBadCap) {
            if (futility) {
               stats.incr(Stats::sid_see);
               skipCap = true;
               continue;
            }
            else if (!rootnode && badCapScore(*it) < - 1 * SearchConfig::seeCaptureFactor * (depth + pruningDepthCorrection + dangerDiff/SearchConfig::seeCapDangerDivisor)) {
               stats.incr(Stats::sid_see2);
               skipCap = true;
               continue;
            }
         }

         // LMR
         DepthType reduction = 0;
         if (SearchConfig::doLMR && depth >= SearchConfig::lmrMinDepth && validMoveCount > 1 + 2 * rootnode && isReductible) {

            if (Move2Type(*it) == T_std){
               stats.incr(Stats::sid_lmr);
               // base reduction
               reduction += SearchConfig::lmrReduction[std::min(static_cast<int>(depth), MAX_DEPTH - 1)][std::min(validMoveCount, MAX_MOVE - 1)];

               // more reduction
               reduction += !improving;
               reduction += ttMoveIsCapture;
               reduction += (cutNode && evalScore - SearchConfig::failHighReductionCoeff.threshold(marginDepth, data.gp, evalScoreIsHashScore, improving) > beta);
               //reduction += moveCountPruning && !formerPV;
               //if (!isInCheck) reduction += std::min(2,(data.mobility[p.c]-data.mobility[~p.c])/8);

               /*
               // aggressive random reduction (too expensive for multi-threading)
               if (randomInt<int,2909>(0,100) > 100 + SearchConfig::lmpLimit[improving][depth + pruningDepthCorrection] - SearchConfig::randomAggressiveReductionFactor * validQuietMoveCount) {
                  stats.incr(Stats::sid_lmrAR);
                  ++reduction;
               }
               */

               // history reduction/extension
               // beware killers and counter are scored above history max
               reduction -= std::min(3, HISTORY_DIV(2 * Move2Score(*it)));

               // less reduction
               //reduction -= !noCheck;
               //reduction -= isCheck;
               reduction -= formerPV || ttPV;
               //reduction -= isDangerRed /*|| ttMoveSingularExt*/ /*|| isEmergencyDefence*/);

            }
            else if (Move2Type(*it) == T_capture){
               stats.incr(Stats::sid_lmrcap);
               // base reduction
               reduction += SearchConfig::lmrReduction[std::min(static_cast<int>(depth), MAX_DEPTH - 1)][std::min(validMoveCount, MAX_MOVE - 1)];
               reduction -= improving;
               reduction -= pvnode;
               // capture history reduction
               reduction -= std::max(-2,std::min(2, HISTORY_DIV(SearchConfig::lmrCapHistoryFactor * historyT.historyCap[PieceIdx(p.board_const(Move2From(*it)))][to][Abs(p.board_const(to))-1])));
            }

            // never extend more than reduce (to avoid search explosion)
            if (extension - reduction > 0) reduction = extension;
            // clamp to depth = 0
            if (reduction >= depth - 1 + extension) reduction = depth - 1 + extension;
         }

         DepthType nextDepth = depth - 1 - reduction + extension;

         // SEE (quiet)
         ScoreType seeValue = 0;
         if (isPrunableStdNoCheck /* && !rootnode*/) {
            seeValue = SEE(p, *it);
            if (seeValue < -SearchConfig::seeQuietFactor * (nextDepth + isEmergencyDefence + isEmergencyAttack + dangerDiff/SearchConfig::seeQuietDangerDivisor) * nextDepth ){
               stats.incr(Stats::sid_seeQuiet);
               continue;
            }
            // reduce bad quiet more
            else if (seeValue < 0 && nextDepth > 1) --nextDepth;
         }

         ++validNonPrunedCount;

         // PVS
         score = -pvs<false>(-alpha - 1, -alpha, p2, nextDepth, height + 1, childPV, seldepth, static_cast<DepthType>(extensions + extension), isCheck, true);
         if (reduction > 0 && score > alpha) {
            stats.incr(Stats::sid_lmrFail);
            childPV.clear();
            score = -pvs<false>(-alpha - 1, -alpha, p2, depth - 1 + extension, height + 1, childPV, seldepth, static_cast<DepthType>(extensions + extension), isCheck, !cutNode);
         }
         if (pvnode && score > alpha && (rootnode || score < beta)) {
            stats.incr(Stats::sid_pvsFail);
            childPV.clear();
            // potential new pv node
            score = -pvs<true>(-beta, -alpha, p2, depth - 1 + extension, height + 1, childPV, seldepth, static_cast<DepthType>(extensions + extension), isCheck, false);
         }
      }
      if (stopFlag) return STOPSCORE;
      if (rootnode) { rootScores.push_back({*it, score}); }
      if (score > bestScore) {
         if (rootnode) previousBest = *it;
         bestScore = score;
         bestMove  = *it;
         bestMoveIsCheck = isCheck;
         //bestScoreUpdated = true;
         if (score > alpha) {
            if (pvnode) updatePV(pv, bestMove, childPV);
            //alphaUpdated = true;
            alpha = score;
            hashBound = TT::B_exact;
            if (score >= beta) {
               stats.incr(Stats::sid_beta);
               if (!isInCheck){
                  const DepthType bonus = depth + (score > beta + SearchConfig::betaMarginDynamicHistory);
                  if(isQuiet /*&& depth > 1*/ /*&& !( depth == 1 && validQuietMoveCount == 1 )*/) { // quiet move history
                     // increase history bonus of this move
                     updateTables(*this, p, bonus, height, bestMove, TT::B_beta, cmhPtr);
                     // reduce history bonus of all previous
                     for (auto it2 = moves.begin(); it2 != moves.end() && !sameMove(*it2, bestMove); ++it2) {
                        if (Move2Type(*it2) == T_std)
                           historyT.update<-1>(bonus, *it2, p, cmhPtr);
                     }
                  }
                  else if( isCapture(bestMove)){ // capture history
                     historyT.updateCap<1>(bonus, bestMove, p);
                     for (auto it2 = moves.begin(); it2 != moves.end() && !sameMove(*it2, bestMove); ++it2) {
                        if (isCapture(*it2))
                           historyT.updateCap<-1>(bonus, *it2, p);
                     }
                  }
               }
               hashBound = TT::B_beta;
               break;
            }
            stats.incr(Stats::sid_alpha);
         }
      }
      else if (rootnode && !isInCheck && firstMove && score < alpha - SearchConfig::failLowRootMargin) {
         return alpha - SearchConfig::failLowRootMargin;
      }
   }

   if (alphaInit == alpha ) stats.incr(Stats::sid_alphanoupdate);

   // check for draw and check mate
   if (validMoveCount == 0) return (isInCheck || !withoutSkipMove) ? matedScore(height) : drawScore(p, height);
   else if (is50moves(p,false)) return drawScore(p, height); // post move loop version

   // insert data in TT
   TT::setEntry(*this, pHash, bestMove, createHashScore(bestScore, height), createHashScore(evalScore, height),
                       TT::Bound(hashBound |
                       (ttPV ? TT::B_ttPVFlag : TT::B_none) |
                       (bestMoveIsCheck ? TT::B_isCheckFlag : TT::B_none) |
                       (isInCheck ? TT::B_isInCheckFlag : TT::B_none)),
                       depth);

   return bestScore;
}
