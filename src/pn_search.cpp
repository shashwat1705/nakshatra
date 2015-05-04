#include "board.h"
#include "common.h"
#include "egtb.h"
#include "eval.h"
#include "movegen.h"
#include "pn_search.h"
#include "stopwatch.h"
#include "timer.h"

#include <algorithm>
#include <cassert>
#include <cfloat>
#include <climits>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <unistd.h>
#include <utility>
#include <vector>

#define PNS_MAX_DEPTH 600

using std::string;

namespace search {

void PNSearch::Search(const PNSParams& pns_params,
                      PNSResult* pns_result) {
  PNSNode* pns_root = pns_tree_buffer_;
  *pns_root = PNSNode();
  next_ = 1;

  int search_nodes = 0;
  if (pns_params.pns_type == PNSParams::PN2 &&
      pns_params.pn2_tree_limit > 0) {
    assert(pns_params.pn2_tree_limit <= max_nodes_);
    search_nodes = pns_params.pn2_tree_limit;
  } else {
    search_nodes = max_nodes_;
  }

  Pns(search_nodes, pns_params, pns_root, &pns_result->num_nodes);

  for (PNSNode* pns_node = children_begin(pns_root);
       pns_node < children_end(pns_root); ++pns_node) {
    // This is from the current playing side perspective.
    double score;
    int result;
    if (pns_node->proof == 0) {
      score = DBL_MAX;
      result = -WIN;
    } else {
      score = static_cast<double>(pns_node->disproof) / pns_node->proof;
      if (pns_node->proof == INF_NODES && pns_node->disproof == 0) {
        result = WIN;
      } else if (pns_node->proof == INF_NODES &&
                 pns_node->disproof == INF_NODES) {
        result = DRAW;
      } else {
        result = UNKNOWN;
      }
    }
    pns_result->ordered_moves.push_back({
        pns_node->move,
        score,
        pns_node->tree_size,
        result});
  }
  sort(pns_result->ordered_moves.begin(), pns_result->ordered_moves.end(),
       [] (const PNSResult::MoveStat& a, const PNSResult::MoveStat& b) {
         return a.score < b.score;
       });
  // Print the ordered moves.
  std::cout << "# Move, score, tree_size:" << std::endl;
  for (const auto& move_stat : pns_result->ordered_moves) {
    static std::map<int, string> result_map = {
      {WIN, "WIN"}, {-WIN, "LOSS"}, {DRAW, "DRAW"}, {UNKNOWN, "UNKNOWN"}
    };
    std::cout << "# " << move_stat.move.str() << ", " << move_stat.score << ", "
         << move_stat.tree_size << ", " << result_map.at(move_stat.result)
         << std::endl;
  }
}

void PNSearch::Pns(const int search_nodes,
                   const PNSParams& pns_params,
                   PNSNode* pns_root,
                   int* num_nodes) {
  *num_nodes = 0;
  PNSNode* cur_node = pns_root;
  Board board_at_root = *board_;

  StopWatch stop_watch;
  stop_watch.Start();

  int depth = 0;
  int save_progress_nodes = pns_params.save_progress;
  int log_progress_secs = pns_params.log_progress;
  while (*num_nodes < search_nodes &&
         (pns_root->proof != 0 && pns_root->disproof != 0) &&
         (!timer_ || !timer_->timer_expired())) {
    if (pns_params.save_progress > 0 && *num_nodes > save_progress_nodes) {
      assert(pns_params.pns_type == PNSParams::PN2);  // allow only for PN2.
      SaveTree(pns_root, *num_nodes, &board_at_root);
      save_progress_nodes += pns_params.save_progress;
    }
    if (pns_params.log_progress > 0 &&
        stop_watch.ElapsedTime() / 100 > log_progress_secs) {
      assert(pns_params.pns_type == PNSParams::PN2);  // allow only for PN2.
      std::cout << "# Progress: "
           << (100.0 * *num_nodes) / search_nodes
           << "% ("
           << *num_nodes << " / " << search_nodes
           << ")" << std::endl;
      log_progress_secs += pns_params.log_progress;
    }
    PNSNode* mpn = FindMpn(cur_node, &depth);
    Expand(pns_params, *num_nodes, depth, mpn);
    *num_nodes += mpn->children_size;

    // In case of PN2 search, update ancestors from the parent node of mpn
    // because mpn might have unevaluated children as we use delayed evaluation
    // in PN2.
    if (pns_params.pns_type == PNSParams::PN2 && mpn->parent) {
      assert(board_->UnmakeLastMove());
      --depth;
      mpn = mpn->parent;
    }
    cur_node = UpdateAncestors(mpn, pns_root, &depth);
  }
  while (cur_node != pns_root) {
    cur_node = cur_node->parent;
    --depth;
    assert (board_->UnmakeLastMove());
    UpdateTreeSize(cur_node);
  }
  assert(depth == 0);
  if (pns_params.save_progress > 0) {
    assert(pns_params.pns_type == PNSParams::PN2);  // allow only for PN2.
    SaveTree(pns_root, *num_nodes, &board_at_root);
  }
}

bool PNSearch::RedundantMoves(PNSNode* pns_node) {
  if (pns_node &&
      pns_node->parent &&
      pns_node->parent->parent &&
      pns_node->parent->parent->parent) {
    const Move& m1 = pns_node->move;
    const Move& m2 = pns_node->parent->move;
    const Move& m3 = pns_node->parent->parent->move;
    const Move& m4 = pns_node->parent->parent->parent->move;
    if (m1.from_index() == m3.to_index() &&
        m1.to_index() == m3.from_index() &&
        m2.from_index() == m4.to_index() &&
        m2.to_index() == m4.from_index()) {
      return true;
    }
  }
  return false;
}

PNSNode* PNSearch::FindMpn(PNSNode* root, int* depth) {
  PNSNode* mpn = root;
  while (mpn->children) {
    // If proof number of parent node is INF_NODES, all it's children will have
    // disproof number of INF_NODES. So, select the child node that has a proof
    // number that is not 0 (i.e, not yet proved). Otherwise, we may end up
    // reaching a leaf node that is proved/disproved/drawn with no scope for
    // expansion.
    if (mpn->proof == INF_NODES) {
      for (PNSNode* pns_node = children_begin(mpn);
           pns_node < children_end(mpn); ++pns_node) {
        if (pns_node->proof) {
          mpn = pns_node;
          break;
        }
      }
    } else {
      for (PNSNode* pns_node = children_begin(mpn);
           pns_node < children_end(mpn); ++pns_node) {
        if (mpn->proof == pns_node->disproof) {
          mpn = pns_node;
          break;
        }
      }
    }
    ++*depth;
    board_->MakeMove(mpn->move);
  }
  assert(!mpn->children);
  return mpn;
}

PNSNode* PNSearch::UpdateAncestors(PNSNode* pns_node,
                                   PNSNode* pns_root,
                                   int* depth) {
  while (true) {
    if (pns_node->children) {
      int proof = INF_NODES;
      int disproof = 0;
      pns_node->tree_size = 1ULL;
      for (PNSNode* child = children_begin(pns_node);
           child < children_end(pns_node); ++child) {
        if (child->disproof < proof) {
          proof = child->disproof;
        }
        if (child->proof == INF_NODES) {
          disproof = INF_NODES;
        } else if (disproof != INF_NODES) {
          disproof += child->proof;
        }
        pns_node->tree_size += child->tree_size;
      }
      if (pns_node->proof == proof &&
          pns_node->disproof == disproof) {
        return pns_node;
      }
      pns_node->proof = proof;
      pns_node->disproof = disproof;
    }
    if (pns_node == pns_root) {
      return pns_node;
    }
    pns_node = pns_node->parent;
    --*depth;
    assert(board_->UnmakeLastMove());
  }
  assert(false);
}

void PNSearch::UpdateTreeSize(PNSNode* pns_node) {
  if (pns_node->children) {
    pns_node->tree_size = 1ULL;
    for (PNSNode* child = children_begin(pns_node);
         child < children_end(pns_node); ++child) {
      pns_node->tree_size += child->tree_size;
    }
  }
}

void PNSearch::Expand(const PNSParams& pns_params,
                      const int num_nodes,
                      const int pns_node_depth,
                      PNSNode* pns_node) {
  if (RedundantMoves(pns_node) || pns_node_depth >= PNS_MAX_DEPTH) {
    pns_node->proof = INF_NODES;
    pns_node->disproof = INF_NODES;
    assert(!pns_node->children);
    assert(pns_node->children_size == 0);
  } else if (pns_params.pns_type == PNSParams::PN2) {
    PNSParams pn2_params;
    pn2_params.pns_type = PNSParams::PN1;
    int pn2_nodes = 0;
    PNSNodeOffset pn2_next = next_;
    Pns(PnNodes(pns_params, num_nodes), pn2_params, pns_node, &pn2_nodes);

    // If the tree is solved, delete the entire Pn subtree under
    // the pns_node. Else, retain MPN's immediate children only.
    if (pns_node->proof == 0 || pns_node->disproof == 0) {
      pns_node->children = nullptr;
      pns_node->children_size = 0;
      next_ = pn2_next;
    } else {
      for (PNSNode* child = children_begin(pns_node);
           child < children_end(pns_node); ++child) {
        child->children = nullptr;
        child->children_size = 0;
      }
      next_ = pn2_next + pns_node->children_size;
    }
  } else {
    MoveArray move_array;
    movegen_->GenerateMoves(&move_array);
    if (move_array.size()) {
      pns_node->children = get_pns_node(next_);
      pns_node->children_size = move_array.size();
      pns_node->tree_size = 1 + pns_node->children_size;
    }
    for (int i = 0; i < move_array.size(); ++i) {
      PNSNode* child = get_clean_pns_node(next_ + i);
      child->move = move_array.get(i);
      child->parent = pns_node;
      board_->MakeMove(child->move);
      int result = evaluator_->Result();
      if (result == UNKNOWN &&
          egtb_ &&
          OnlyOneBitSet(board_->BitBoard(Side::WHITE)) &&
          OnlyOneBitSet(board_->BitBoard(Side::BLACK))) {
        const EGTBIndexEntry* egtb_entry = egtb_->Lookup();
        if (egtb_entry) {
          result = EGTBResult(*egtb_entry);
        }
      }
      if (result == DRAW) {
        child->proof = INF_NODES;
        child->disproof = INF_NODES;
      } else if (result == -WIN) {
        child->proof = INF_NODES;
        child->disproof = 0;
      } else if (result == WIN) {
        child->proof = 0;
        child->disproof = INF_NODES;
      } else {
        child->proof = 1;
        child->disproof = movegen::CountMoves(board_->SideToMove(), *board_);
      }
      board_->UnmakeLastMove();
    }
    next_ += move_array.size();
  }
}

int PNSearch::PnNodes(const PNSParams& pns_params,
                      const int num_nodes) {
  if (pns_params.pn2_full_search) {
    return max_nodes_ - num_nodes;
  }
  const double a = pns_params.pn2_max_nodes_fraction_a * max_nodes_;
  const double b = pns_params.pn2_max_nodes_fraction_b * max_nodes_;
  const double f_x = 1.0 / (1.0 + exp((a - num_nodes) / b));
  return static_cast<int>(std::min(ceil(std::max(num_nodes, 1) * f_x),
                              static_cast<double>(max_nodes_ - num_nodes)));
}

void PNSearch::SaveTree(const PNSNode* pns_node, const int num_nodes,
                        Board* board) {
  std::cout << "# Saving tree..." << std::endl;
  const string filename = "pns_progress_" + LongToString(getpid()) + "_" +
      LongToString(long(num_nodes));
  std::ofstream ofs(filename.c_str(), std::ios::out);
  SaveTreeHelper(pns_node, board, ofs);
  std::cout << "# Done saving tree." << std::endl;
}

void PNSearch::SaveTreeHelper(const PNSNode* pns_node, Board* board,
                              std::ofstream& ofs) {
  if (!pns_node->children) {
    return;
  }
  const string fen = board->ParseIntoFEN();
  ofs << "# " << fen << std::endl;
  for (PNSNode* child = children_begin(pns_node);
       child < children_end(pns_node); ++child) {
    board->MakeMove(child->move);
    double ratio;
    if (child->proof == 0) ratio = DBL_MAX;
    else ratio = double(child->disproof) / child->proof;
    ofs << fen << "|" << child->move.str() << "|" << ratio << "|"
        << child->proof << "|" << child->disproof << "|" << child->tree_size
        << std::endl;
    board->UnmakeLastMove();
  }
  for (PNSNode* child = children_begin(pns_node);
       child < children_end(pns_node); ++child) {
    board->MakeMove(child->move);
    SaveTreeHelper(child, board, ofs);
    board->UnmakeLastMove();
  }
}

}  // namespace search