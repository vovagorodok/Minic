#include "kpk.hpp"

#include "attack.hpp"
#include "logging.hpp"
#include "position.hpp"

namespace {
uint32_t           KPKBitbase[KPK::KPKmaxIndex / 32];               // force 32bit uint
inline unsigned    KPKindex(Color us, Square bksq, Square wksq, Square psq) {
   return wksq | (bksq << 6) | (us << 12) | (SQFILE(psq) << 13) | ((6 - SQRANK(psq)) << 15);
}
} // namespace

namespace KPK {

Square normalizeSquare(const Position& p, const Color strongSide, const Square sq) {
   assert(BB::countBit(p.pieces_const<P_wp>(strongSide)) == 1); // only for KPK !
   return strongSide == Co_White ? (SQFILE(BBTools::SquareFromBitBoard(p.pieces_const<P_wp>(strongSide))) >= File_e ? HFlip(sq) : sq) : VFlip(sq);
}

KPKPosition::KPKPosition(const unsigned idx) { // first init
   ksq[Co_White] = static_cast<Square>(idx & 0x3F);
   ksq[Co_Black] = static_cast<Square>((idx >> 6) & 0x3F);
   us            = static_cast<Color>((idx >> 12) & 0x01);
   psq           = MakeSquare(File((idx >> 13) & 0x3), Rank(6 - ((idx >> 15) & 0x7)));
   if (chebyshevDistance(ksq[Co_White], ksq[Co_Black]) <= 1 || ksq[Co_White] == psq || ksq[Co_Black] == psq ||
       (us == Co_White && (BBTools::mask[psq].pawnAttack[Co_White] & SquareToBitboard(ksq[Co_Black]))))
      result = kpk_invalid;
   else if (us == Co_White && SQRANK(psq) == 6 && ksq[us] != psq + 8 &&
            (chebyshevDistance(ksq[~us], psq + 8) > 1 || (BBTools::mask[ksq[us]].king & SquareToBitboard(psq + 8))))
      result = kpk_win;
   else if (us == Co_Black && (!(BBTools::mask[ksq[us]].king & ~(BBTools::mask[ksq[~us]].king | BBTools::mask[psq].pawnAttack[~us])) ||
                               (BBTools::mask[ksq[us]].king & SquareToBitboard(psq) & ~BBTools::mask[ksq[~us]].king)))
      result = kpk_draw;
   else
      result = kpk_unknown; // done later
}

kpk_result KPKPosition::preCompute(const std::array<KPKPosition, KPKmaxIndex>& db) {
   return us == Co_White ? preCompute<Co_White>(db) : preCompute<Co_Black>(db);
}

template<Color Us> kpk_result KPKPosition::preCompute(const std::array<KPKPosition, KPKmaxIndex>& db) {
   constexpr Color      Them = (Us == Co_White ? Co_Black : Co_White);
   constexpr kpk_result good = (Us == Co_White ? kpk_win : kpk_draw);
   constexpr kpk_result bad  = (Us == Co_White ? kpk_draw : kpk_win);
   kpk_result r = kpk_invalid;
   BitBoard b = BBTools::mask[ksq[us]].king;
   while (b) { r |= (Us == Co_White ? db[KPKindex(Them, ksq[Them], BB::popBit(b), psq)] : db[KPKindex(Them, BB::popBit(b), ksq[Them], psq)]); }
   if (Us == Co_White) {
      if (SQRANK(psq) < 6) r |= db[KPKindex(Them, ksq[Them], ksq[Us], psq + 8)];
      if (SQRANK(psq) == 1 && psq + 8 != ksq[Us] && psq + 8 != ksq[Them]) r |= db[KPKindex(Them, ksq[Them], ksq[Us], psq + 8 + 8)];
   }
   return result = r & good ? good : r & kpk_unknown ? kpk_unknown : bad;
}

bool probe(Square wksq, const Square wpsq, const Square bksq, const Color us) {
   assert(isValidSquare(wksq));
   assert(isValidSquare(wpsq));
   assert(isValidSquare(bksq));
   assert(SQFILE(wpsq) <= 4);
   const unsigned idx = KPKindex(us, bksq, wksq, wpsq);
   assert(idx < KPKmaxIndex);
   return KPKBitbase[idx / 32] & (1 << (idx & 0x1F));
}

void init() {
   Logging::LogIt(Logging::logInfo) << "KPK init";
   Logging::LogIt(Logging::logInfo) << "KPK table size : " << KPKmaxIndex / 32 * sizeof(uint32_t) / 1024 << "Kb";
   std::array<KPKPosition, KPKmaxIndex> db;
   unsigned idx, repeat = 1;
   for (idx = 0; idx < KPKmaxIndex; ++idx) db[idx] = KPKPosition(idx); // init
   while (repeat)
      for (repeat = idx = 0; idx < KPKmaxIndex; ++idx) repeat |= (db[idx] == kpk_unknown && db[idx].preCompute(db) != kpk_unknown); // loop
   for (idx = 0; idx < KPKmaxIndex; ++idx) {
      if (db[idx] == kpk_win) { KPKBitbase[idx / 32] |= 1 << (idx & 0x1F); }
   } // compress
}

} // namespace KPK
