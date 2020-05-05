#ifndef EVAL_H
#define EVAL_H

#include "common.h"
#include "eval.h"

class Board;
class EGTB;
class MoveArray;
class MoveGenerator;

class Evaluator {
public:
  virtual ~Evaluator() {}

  // Returns the score for current position of the board.
  virtual int Evaluate() = 0;

  // Returns WIN, -WIN or DRAW if game is over; else returns -1.
  virtual int Result() const = 0;
};

template <Variant variant>
class Eval : public Evaluator {
public:
  Eval(Board* board, MoveGenerator* movegen, EGTB* egtb)
      : board_(board), movegen_(movegen), egtb_(egtb) {}

  int Evaluate() override;

  int Result() const override;

private:
  Board* board_;
  MoveGenerator* movegen_;
  EGTB* egtb_;
};

#endif
