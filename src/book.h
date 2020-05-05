#ifndef BOOK_H
#define BOOK_H

#include "move.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

class Board;
class MoveGenerator;

class Book {
public:
  virtual ~Book() {}

  virtual Move GetBookMove(const Board& board) const = 0;
};

template <Variant variant>
class BookImpl : public Book {
public:
  BookImpl(const std::string& book_file);

  Move GetBookMove(const Board& board) const;

private:
  void LoadBook(Board* board, MoveGenerator* movegen);

  std::map<std::string, std::vector<Move>> book_;
  const std::string book_file_;
};

#endif
