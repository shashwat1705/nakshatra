#include "board.h"
#include "common.h"
#include "egtb_gen.h"
#include "eval_suicide.h"
#include "move.h"
#include "move_array.h"
#include "movegen.h"
#include "movegen_suicide.h"
#include "piece.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>

#define MAX 10000

using std::string;
using std::vector;

EGTBElement* EGTBStore::Get(string fen) {
  if (store_.find(fen) == store_.end()) {
    return NULL;
  }
  return &store_[fen];
}

void EGTBStore::Put(string fen, int moves_to_end,
                    Move next_move, Side winner) {
  EGTBElement e;
  e.fen = fen;
  e.moves_to_end = moves_to_end;
  e.next_move = next_move;
  e.winner = winner;
  store_[fen] = e;
}

void EGTBStore::MergeFrom(EGTBStore store) {
  const std::map<string, EGTBElement>& egtb_map = store.GetMap();
  for (const auto& elem : egtb_map) {
    store_[elem.first] = elem.second;
  }
}

void EGTBStore::Write(std::ofstream& ofs) {
  for (const auto& elem : store_) {
    string s = elem.second.next_move.str();
    if (s == "--") {
      s = "LOST";
    }
    ofs << elem.second.fen << '|' << s << '|' << elem.second.moves_to_end << '|';
    if (elem.second.winner == Side::WHITE) {
      ofs << 'W';
    } else if (elem.second.winner == Side::BLACK) {
      ofs << 'B';
    } else {
      ofs << 'N';
    }
    ofs << std::endl;
  }
}

void EGTBGenerator::Generate(vector<string> all_pos_list,
                             Side winning_side,
                             EGTBStore* store) {
  int superbest = 0;
  int i = 0;
  while (true) {
    unsigned all_pos_list_size = all_pos_list.size(), progress = 0;
    printf("Size: %d, %u, %d\n", i, all_pos_list_size, superbest);
    EGTBStore temp_store;
    bool deleted = false;
    double last_percent = 0.0;
    for (vector<string>::iterator iter = all_pos_list.begin();
        iter != all_pos_list.end();) {
      double percent = (static_cast<double>(progress) / all_pos_list_size) * 100;
      if (percent >= last_percent + 1 || percent + 0.1 >= 100) {
        printf("%5.2lf %%\r", percent);
        fflush(stdout);
        last_percent = percent;
      }
      fflush(stdout);
      ++progress;
      Board board(SUICIDE, *iter);
      movegen::MoveGeneratorSuicide movegen(board);
      MoveArray movelist;
      movegen.GenerateMoves(&movelist);
      if (board.SideToMove() == winning_side) {
        int count = 0;
        int best = 10000;
        Move m;
        for (int i = 0; i < movelist.size(); ++i) {
          const Move& move = movelist.get(i);
          board.MakeMove(move);
          string fen = board.ParseIntoFEN();
          EGTBElement* e = store->Get(fen);
          if (e != NULL &&
              e->winner == winning_side &&
              e->moves_to_end + 1 < best) {
            best = e->moves_to_end + 1;
            m = move;
            ++count;
          }
          board.UnmakeLastMove();
        }
        if (count >= 1) {
          if (best > superbest) superbest = best;
          temp_store.Put(*iter, best, m, winning_side);
          iter = all_pos_list.erase(iter);
          deleted = true;
        } else {
          ++iter;
        }
      } else {
        int count = 0;
        int best = -1;
        Move m;
        for (int i = 0; i < movelist.size(); ++i) {
          const Move& move = movelist.get(i);
          board.MakeMove(move);
          string fen = board.ParseIntoFEN();
          EGTBElement* e = store->Get(fen);
          if (e != NULL &&
              e->winner == winning_side) {
            if (e->moves_to_end + 1 > best) {
              best = e->moves_to_end + 1;
              m = move;
            }
            ++count;
          }
          board.UnmakeLastMove();
        }
        if (count == movelist.size()) {
          if (best > superbest) superbest = best;
          temp_store.Put(*iter, best, m, winning_side);
          iter = all_pos_list.erase(iter);
          deleted = true;
        } else {
          ++iter;
        }
      }
    }
    printf("\n");
    if (!deleted) {
      break;
    }
    store->MergeFrom(temp_store);
    ++i;
  }
  printf("\n");
}

void EGTBGenerator::Generate(vector<string> final_pos_list,
                             vector<string> all_pos_list,
                             Side winning_side,
                             EGTBStore* store) {
  // Put lost(0) in Store.
  for (const string& pos : final_pos_list) {
    store->Put(pos, 0, Move(), winning_side);
  }
  for (vector<string>::iterator iter = all_pos_list.begin();
       iter != all_pos_list.end();) {
    Board board(SUICIDE, *iter);
    movegen::MoveGeneratorSuicide movegen(board);
    eval::EvalSuicide eval(&board, &movegen, nullptr);
    int result = eval.Result();
    if (result == WIN) {
      store->Put(*iter, 0, Move(), board.SideToMove());
      iter = all_pos_list.erase(iter);
    } else if (result == -WIN) {
      store->Put(*iter, 0, Move(), OppositeSide(board.SideToMove()));
      iter = all_pos_list.erase(iter);
    } else if (result == DRAW) {
      iter = all_pos_list.erase(iter);
    } else {
      ++iter;
    }
  }
  Generate(all_pos_list, winning_side, store);
}