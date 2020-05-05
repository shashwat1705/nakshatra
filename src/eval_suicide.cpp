#include "board.h"
#include "common.h"
#include "egtb.h"
#include "eval.h"
#include "movegen.h"
#include "piece.h"
#include "stopwatch.h"

#include <cstdlib>

namespace {
// Piece values.
namespace pv {
constexpr int KING = 10;
constexpr int QUEEN = 6;
constexpr int ROOK = 7;
constexpr int BISHOP = 3;
constexpr int KNIGHT = 3;
constexpr int PAWN = 2;
} // namespace pv

// Weight for mobility.
constexpr int MOBILITY_FACTOR = 25;

constexpr int PIECE_COUNT_FACTOR = -50;

constexpr int TEMPO = 250;

int PieceValDifference(Board* board) {
  const int white_val = PopCount(board->BitBoard(KING)) * pv::KING +
                        PopCount(board->BitBoard(QUEEN)) * pv::QUEEN +
                        PopCount(board->BitBoard(PAWN)) * pv::PAWN +
                        PopCount(board->BitBoard(BISHOP)) * pv::BISHOP +
                        PopCount(board->BitBoard(KNIGHT)) * pv::KNIGHT +
                        PopCount(board->BitBoard(ROOK)) * pv::ROOK;
  const int black_val = PopCount(board->BitBoard(-KING)) * pv::KING +
                        PopCount(board->BitBoard(-QUEEN)) * pv::QUEEN +
                        PopCount(board->BitBoard(-PAWN)) * pv::PAWN +
                        PopCount(board->BitBoard(-BISHOP)) * pv::BISHOP +
                        PopCount(board->BitBoard(-KNIGHT)) * pv::KNIGHT +
                        PopCount(board->BitBoard(-ROOK)) * pv::ROOK;

  return (board->SideToMove() == Side::WHITE) ? (white_val - black_val)
                                              : (black_val - white_val);
}

int PieceCountDiff(Board* board) {
  const int white_count = PopCount(board->BitBoard(Side::WHITE));
  const int black_count = PopCount(board->BitBoard(Side::BLACK));
  return (board->SideToMove() == Side::WHITE) ? (white_count - black_count)
                                              : (black_count - white_count);
}

bool RivalBishopsOnOppositeColoredSquares(Board* board) {
  static const U64 WHITE_SQUARES = 0xAA55AA55AA55AA55ULL;
  static const U64 BLACK_SQUARES = 0x55AA55AA55AA55AAULL;

  const U64 white_bishop = board->BitBoard(BISHOP);
  const U64 black_bishop = board->BitBoard(-BISHOP);

  return ((white_bishop && black_bishop) &&
          (((white_bishop & WHITE_SQUARES) && (black_bishop & BLACK_SQUARES)) ||
           ((white_bishop & BLACK_SQUARES) && (black_bishop & WHITE_SQUARES))));
}

} // namespace

template <>
int Eval<Variant::SUICIDE>::Evaluate() {
  const Side side = board_->SideToMove();
  const int self_pieces = board_->NumPieces(side);
  const int opp_pieces = board_->NumPieces(OppositeSide(side));

  if (self_pieces == 1 && opp_pieces == 1) {
    if (egtb_) {
      const EGTBIndexEntry* egtb_entry = egtb_->Lookup();
      if (egtb_entry) {
        return EGTBResult(*egtb_entry);
      }
    }
    if (RivalBishopsOnOppositeColoredSquares(board_)) {
      return DRAW;
    }
  }

  const int self_moves = movegen_->CountMoves();
  if (self_moves == 0) {
    return self_pieces < opp_pieces ? WIN
                                    : (self_pieces == opp_pieces ? DRAW : -WIN);
  }

  if (self_moves == 1) {
    MoveArray move_array;
    movegen_->GenerateMoves(&move_array);
    board_->MakeMove(move_array.get(0));
    const int eval_val = -Evaluate();
    board_->UnmakeLastMove();
    return eval_val;
  }

  board_->FlipSideToMove();
  const int opp_moves = movegen_->CountMoves();
  board_->FlipSideToMove();
  if (opp_moves == 0) {
    MoveArray move_array;
    movegen_->GenerateMoves(&move_array);
    int max_eval = -INF;
    for (size_t i = 0; i < move_array.size(); ++i) {
      const Move& move = move_array.get(i);
      board_->MakeMove(move);
      const int eval_val = -Evaluate();
      board_->UnmakeLastMove();
      if (max_eval < eval_val) {
        max_eval = eval_val;
      }
    }
    return max_eval;
  }

  return (self_moves - opp_moves) * MOBILITY_FACTOR +
         PieceValDifference(board_) + TEMPO +
         PIECE_COUNT_FACTOR * PieceCountDiff(board_);
}

template <>
int Eval<Variant::SUICIDE>::Result() const {
  const Side side = board_->SideToMove();
  const int self_pieces = board_->NumPieces(side);
  const int opp_pieces = board_->NumPieces(OppositeSide(side));

  if (self_pieces == 1 && opp_pieces == 1 &&
      RivalBishopsOnOppositeColoredSquares(board_)) {
    return DRAW;
  }

  const int self_moves = movegen_->CountMoves();
  if (self_moves == 0) {
    return self_pieces < opp_pieces ? WIN
                                    : (self_pieces == opp_pieces ? DRAW : -WIN);
  }

  return UNKNOWN;
}

template class Eval<Variant::SUICIDE>;
