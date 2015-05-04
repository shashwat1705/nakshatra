#ifndef BOARD_H
#define BOARD_H

#include "common.h"
#include "move.h"
#include "piece.h"

#include <string>

// A Chess board that supports multiple variants.
class Board {
 public:
  // Construct with the standard initial board position for the variant.
  Board(const Variant variant);

  // Construct with given FEN for the variant.
  Board(const Variant variant, const std::string& fen);

  // Moves piece on the board. Does not check for validity of move.
  void MakeMove(const Move& move);

  // Undoes the last move (if any). Returns true if the move was undone.
  // If no move is present in move stack, returns false.
  bool UnmakeLastMove();

  // Next side to move.
  Side SideToMove() const {
    return side_to_move_;
  }

  // If pawn was advanced by 2 squares from its starting position in the last
  // move, this function returns the en-passant target square. If no such move
  // was made, returns -1.
  int EnpassantTarget() const {
    return move_stack_.Top()->ep_index;
  }

  // Returns true if the current player can castle on the given ('piece' can be
  // KING or QUEEN) side.
  bool CanCastle(const Piece piece) const;

  // Returns the piece at given row and column or given index on the board.
  Piece PieceAt(const int row, const int col) const {
    return board_array_[INDX(row, col)];
  }
  Piece PieceAt(const int index) const {
    return board_array_[index];
  }

  // Returns complete bitboard or side/piece specific bitboards.
  U64 BitBoard() const {
    return BitBoard(Side::BLACK) | BitBoard(Side::WHITE);
  }
  U64 BitBoard(const Side side) const {
    return bitboard_sides_[SideIndex(side)];
  }
  U64 BitBoard(const Piece piece) const {
    return bitboard_pieces_[PieceIndex(piece)];
  }

  // Returns the number of pieces of given side on the board.
  int NumPieces(const Side side) const {
    return PopCount(BitBoard(side));
  }

  // The zobrist key for current board position.
  U64 ZobristKey() const {
    return move_stack_.Top()->zobrist_key;
  }

  // Returns the board as an FEN (Forsyth-Edwards Notation) string.
  std::string ParseIntoFEN() const;

  // Returns number of plies played on the board so far.
  int Ply() const {
    return move_stack_.Size();
  }

  void DebugPrintBoard() const;

  // WARNING!
  // The below public methods are only useful for EGTB code for offline
  // processing. They do not handle Zobrist key updates correctly as it is not
  // required for the EGTB code. For other use cases, handle with care!

  void SetPiece(const int index, const Piece piece) {
    board_array_[index] = piece;
  }

  void SetPlayerColor(const Side side) {
    side_to_move_ = side;
  }

 private:
  // An entry in the move stack.
  struct MoveStackEntry {
    Move move;

    // The piece captured in this move. NULLPIECE if no piece captured.
    Piece captured_piece = NULLPIECE;

    // Determines if castling can be done. If set, castling is available.
    // Bits (0, 1, 2, 3) = (white king, white queen, black king, black queen).
    unsigned char castle;

    // Enpassant target position, only updated if last move was a pawn advanced
    // by two squares from its starting position. Else, set to -1.
    int ep_index = -1;

    // Zobrist key of the board position after this move is played.
    U64 zobrist_key;
  };

  // A thin wrapper around an array of MoveStackEntry elements that provides a
  // stack-like interface. Methods don't check array bounds.
  class MoveStack {
   public:
    void Push() {
      ++size_;
    }

    void Pop() {
      --size_;
    }

    int Size() const {
      return size_;
    }

    MoveStackEntry* Top() {
      return entries_ + size_;
    }
    const MoveStackEntry* Top() const {
      return entries_ + size_;
    }

    // Returns a pointer to an entry 'pos' elements down the stack.
    // Seek(0) == Top(). Client should ensure pos <= Size().
    const MoveStackEntry* Seek(int pos) const {
      return Top() - pos;
    }

   private:
    MoveStackEntry entries_[1000];
    int size_ = 0;
  };

  void FlipSideToMove();

  // Generates Zobrist key for the board. Call this only after the board array,
  // side to move, en-passant target (if any) have been set.
  U64 GenerateZobristKey();

  // Places piece on the board. Two versions - one updates zobrist key and
  // another doesn't. It's an error to call these methods if the square given by
  // index is not empty.
  void PlacePiece(const int index, const Piece piece);
  void PlacePieceNoZ(const int index, const Piece piece);

  // Removes piece from given index on the board. Two versions - one updates
  // zobrist key and another doesn't. It's an error to call these methods if the
  // square given by index is not empty.
  void RemovePiece(const int index);
  void RemovePieceNoZ(const int index);

  // Array representation of the board. Empty squares are represented by
  // NULLPIECE.
  Piece board_array_[BOARD_SIZE];

  // Bitboards for each side and each piece type for both sides.
  U64 bitboard_sides_[2];
  U64 bitboard_pieces_[12];

  // Side to move next.
  Side side_to_move_;

  // True if the variant allows castling.
  bool castling_allowed_;

  MoveStack move_stack_;
};

#endif