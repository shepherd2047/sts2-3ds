// Port of MegaCrit.Sts2.Core.Map.StandardActMap (+ MapPathPruning,
// MapPostProcessing, MapPoint, MapCoord, MapPointType, MapPointTypeCounts)
// and MegaCrit.Sts2.Core.Models.Acts.Overgrowth.GetMapPointTypes /
// MegaCrit.Sts2.Core.Models.ActModel.GetNumberOfRooms.
//
// Specialized to single player (isMultiplayer=false), shouldReplaceTreasureWithElites
// = false and no mapPointTypeCountsOverride (NumOfElites is a parameter: 8 with
// SwarmingElites). The act index picks BaseNumberOfRooms (Overgrowth 15, Hive 14,
// Glory 13) and GetMapPointTypes. hasSecondBoss (DoubleBoss, ascension 10, last act)
// adds SecondBossMapPoint one row past the boss, the boss's only child; it takes no Rng
// and path pruning stops at the first boss (FindAllPaths), so the rest is unchanged.
//
// Indexing: the C# grid is Grid[col, row] with col in [0,7) and row in
// [0, mapLength) where mapLength = BaseNumberOfRooms + 1 (16 in act 1). Row 0 is never
// populated (the StartingMapPoint lives there but isn't stored in the grid);
// real rooms occupy rows 1..15. The boss lives at row mapLength = 16 (out of
// grid bounds), col 3, and is also not stored in the grid. Our output puts
// the starting point first (row -1, RoomType::Ancient), renumbers row-1..row-15
// as our rows 0..14 and appends the boss (row 15, col 3), then the second boss if
// any (row 16, col 3).
//
// MapPoint.Children / MapPoint.parents (and ActMap.startMapPoints) are C# HashSet<MapPoint>s; NodeSet
// below reproduces .NET's enumeration order: entries in slot order, a removed entry's slot reused
// by the next Add (LIFO free list), so the map shape follows the C# for the same Rng draws.
//
// PORT NOTE (n/a: equivalent): MapPathPruning.EnsureRowsContainsPointType / AssignPointTypesToRandomRows are dead code in the C# (never called), so not ported.

#include <algorithm>
#include <climits>
#include <cmath>
#include <deque>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "game.h"

namespace sts {
namespace {

// ---------------------------------------------------------------- map point

enum class MPType { Unassigned, Unknown, Shop, Treasure, RestSite, Monster, Elite, Boss, Ancient };

struct Node;

// HashSet<MapPoint> with .NET's enumeration order (System.Collections.Generic.HashSet: an entries
// array enumerated by index; Remove frees the slot onto a free list, Add takes the most recently
// freed slot first, else appends). MapPoint has no Equals/GetHashCode override: reference identity.
struct NodeSet {
  std::vector<Node*> slots;  // nullptr = a freed slot
  std::vector<size_t> freeList;
  size_t count = 0;
  bool contains(Node* n) const { return n && std::find(slots.begin(), slots.end(), n) != slots.end(); }
  void insert(Node* n) {
    if (contains(n)) return;
    if (!freeList.empty()) { slots[freeList.back()] = n; freeList.pop_back(); }
    else slots.push_back(n);
    ++count;
  }
  void erase(Node* n) {
    auto it = std::find(slots.begin(), slots.end(), n);
    if (!n || it == slots.end()) return;
    *it = nullptr;
    freeList.push_back((size_t)(it - slots.begin()));
    --count;
  }
  size_t size() const { return count; }
  bool empty() const { return count == 0; }
  struct iterator {  // skips freed slots
    Node* const* p;
    Node* const* e;
    void skip() { while (p != e && !*p) ++p; }
    Node* operator*() const { return *p; }
    iterator& operator++() { ++p; skip(); return *this; }
    bool operator==(const iterator& o) const { return p == o.p; }
    bool operator!=(const iterator& o) const { return p != o.p; }
    using difference_type = std::ptrdiff_t;
    using value_type = Node*;
    using pointer = Node* const*;
    using reference = Node*;
    using iterator_category = std::forward_iterator_tag;
  };
  iterator begin() const {
    iterator it{slots.data(), slots.data() + slots.size()};
    it.skip();
    return it;
  }
  iterator end() const { return {slots.data() + slots.size(), slots.data() + slots.size()}; }
};

struct Node {
  int col = 0, row = 0;
  MPType type = MPType::Unassigned;
  bool canBeModified = true;
  NodeSet parents;
  NodeSet children;

  void addChild(Node* child) {
    children.insert(child);
    child->parents.insert(this);
  }
  void removeChild(Node* child) {
    children.erase(child);
    child->parents.erase(this);
  }
};

// ---------------------------------------------------------------- constants

constexpr int kMapWidth = 7;               // StandardActMap._mapWidth
constexpr int kNumOfShops = 3;              // MapPointTypeCounts.NumOfShops

bool inSet(MPType t, std::initializer_list<MPType> set) {
  for (MPType s : set) if (s == t) return true;
  return false;
}

// StandardActMap's restriction sets.
bool isLowerRestricted(MPType t) { return inSet(t, {MPType::RestSite, MPType::Elite}); }
bool isUpperRestricted(MPType t) { return inSet(t, {MPType::RestSite}); }
bool isParentRestricted(MPType t) { return inSet(t, {MPType::Elite, MPType::RestSite, MPType::Treasure, MPType::Shop}); }
bool isChildRestricted(MPType t) { return inSet(t, {MPType::Elite, MPType::RestSite, MPType::Treasure, MPType::Shop}); }
bool isSiblingRestricted(MPType t) { return inSet(t, {MPType::RestSite, MPType::Monster, MPType::Unknown, MPType::Elite, MPType::Shop}); }

// ---------------------------------------------------------------- rng helpers
// Rng::nextGaussianInt doesn't exist on the shared Rng (rng.h); implement it
// here using only the public Rng API, matching Rng.NextGaussianInt exactly
// (Box-Muller, rejecting outside [min,max], Math.Round == round-half-to-even).
int nextGaussianInt(Rng& rng, int mean, int stdDev, int min, int max) {
  constexpr double kPi = 3.14159265358979323846;
  int result;
  do {
    double d = 1.0 - rng.nextDouble();
    double u = 1.0 - rng.nextDouble();
    double mag = std::sqrt(-2.0 * std::log(d));
    double z = mag * std::sin(2.0 * kPi * u);
    double a = (double)mean + (double)stdDev * z;
    result = (int)std::nearbyint(a);  // round-half-to-even, like C# Math.Round's default
  } while (result < min || result > max);
  return result;
}

// List<T>.StableShuffle: sort (by the given comparator), then UnstableShuffle.
// List<T>.UnstableShuffle == Rng::shuffle (Fisher-Yates from the back) already.
template <class Cmp>
void stableShuffle(std::vector<Node*>& v, Rng& rng, Cmp cmp) {
  std::sort(v.begin(), v.end(), cmp);
  rng.shuffle(v);
}

// MapCoord.CompareTo: (col, row) tuple compare. Every node has a unique coord
// so sort stability doesn't matter here.
bool coordLess(Node* a, Node* b) {
  if (a->col != b->col) return a->col < b->col;
  return a->row < b->row;
}

// ---------------------------------------------------------------- generator

struct MapBuilder {
  Rng& rng;
  int mapLength;  // ActModel.GetNumberOfRooms(false) + 1 (16 / 15 / 14 for acts 1-3)
  std::vector<std::unique_ptr<Node>> pool;
  std::vector<std::vector<Node*>> grid;  // grid[col][row], col in [0,7), row in [0,16)
  NodeSet startMapPoints;
  Node* startingPoint = nullptr;
  Node* bossPoint = nullptr;
  int numOfRests = 0;
  int numOfElites = 5;
  int numOfUnknowns = 0;
  bool elitesIgnoreRules = false;  // MapPointTypeCounts.PointTypesThatIgnoreRules = {Elite} (BigGameHunter)

  MapBuilder(Rng& r, int len) : rng(r), mapLength(len), grid(kMapWidth, std::vector<Node*>(len, nullptr)) {}

  Node* makeNode(int col, int row) {
    pool.push_back(std::make_unique<Node>());
    Node* n = pool.back().get();
    n->col = col;
    n->row = row;
    return n;
  }

  Node* getOrCreatePoint(int col, int row) {
    if (grid[col][row]) return grid[col][row];
    Node* n = makeNode(col, row);
    grid[col][row] = n;
    return n;
  }

  static void forEachInRow(std::vector<std::vector<Node*>>& grid, int row, const std::function<void(Node*)>& fn) {
    for (int c = 0; c < kMapWidth; c++) {
      Node* n = grid[c][row];
      if (n) fn(n);
    }
  }

  std::vector<Node*> getAllMapPoints() const {
    std::vector<Node*> out;
    for (int c = 0; c < kMapWidth; c++)
      for (int r = 0; r < mapLength; r++)
        if (grid[c][r]) out.push_back(grid[c][r]);
    return out;
  }

  // ---- GenerateMap ----

  bool hasInvalidCrossover(Node* current, int targetCol) {
    int diff = targetCol - current->col;
    if (diff == 0 || diff == 7) return false;
    Node* n = grid[targetCol][current->row];
    if (!n) return false;
    for (Node* child : n->children) {
      int diff2 = child->col - n->col;
      if (diff2 == -diff) return true;
    }
    return false;
  }

  // Returns the next node's row via out-param col; throws if none valid
  // (mirrors StandardActMap.GenerateNextCoord).
  void generateNextCoord(Node* current, int& outCol, int& outRow) {
    int col = current->col;
    int leftCol = std::max(0, col - 1);
    int rightCol = std::min(col + 1, 6);
    std::vector<int> opts = {-1, 0, 1};  // already sorted -> StableShuffle's sort is a no-op
    rng.shuffle(opts);
    for (int item : opts) {
      int row = current->row + 1;
      int target = (item == -1) ? leftCol : (item == 0 ? col : rightCol);
      if (!hasInvalidCrossover(current, target)) {
        outCol = target;
        outRow = row;
        return;
      }
    }
    // C# throws here ("Cannot find next node"); it cannot happen (going straight never
    // crosses), and the 3DS build has no exceptions: go straight.
    outCol = col;
    outRow = current->row + 1;
  }

  void pathGenerate(Node* startingNode) {
    Node* cur = startingNode;
    while (cur->row < mapLength - 1) {
      int col, row;
      generateNextCoord(cur, col, row);
      Node* next = getOrCreatePoint(col, row);
      cur->addChild(next);
      cur = next;
    }
  }

  void generateMap() {
    for (int i = 0; i < kMapWidth; i++) {
      Node* p = getOrCreatePoint(rng.nextInt(0, 7), 1);
      if (i == 1) {
        while (startMapPoints.contains(p)) {
          p = getOrCreatePoint(rng.nextInt(0, 7), 1);
        }
      }
      startMapPoints.insert(p);
      pathGenerate(p);
    }
    forEachInRow(grid, mapLength - 1, [&](Node* x) { x->addChild(bossPoint); });
    forEachInRow(grid, 1, [&](Node* x) { startingPoint->addChild(x); });
  }

  // ---- AssignPointTypes ----

  bool isValidForLower(MPType t, Node* n) {
    if (n->row < 6) return !isLowerRestricted(t);
    return true;
  }
  bool isValidForUpper(MPType t, Node* n) {
    if (n->row >= mapLength - 3) return !isUpperRestricted(t);
    return true;
  }
  bool isValidWithParents(MPType t, Node* n) {
    if (!isParentRestricted(t)) return true;
    for (Node* p : n->parents) if (p->type == t) return false;
    for (Node* c : n->children) if (c->type == t) return false;
    return true;
  }
  bool isValidWithChildren(MPType t, Node* n) {
    if (!isChildRestricted(t)) return true;
    for (Node* c : n->children) if (c->type == t) return false;
    return true;
  }
  std::vector<Node*> getSiblings(Node* n) {
    std::vector<Node*> out;
    for (Node* p : n->parents)
      for (Node* c : p->children)
        if (c != n) out.push_back(c);
    return out;
  }
  bool isValidWithSiblings(MPType t, Node* n) {
    if (!isSiblingRestricted(t)) return true;
    for (Node* s : getSiblings(n)) if (s->type == t) return false;
    return true;
  }
  bool isValidPointType(MPType t, Node* n) {
    if (!isValidForUpper(t, n)) return false;
    if (!isValidForLower(t, n)) return false;
    if (!isValidWithParents(t, n)) return false;
    if (!isValidWithChildren(t, n)) return false;
    if (!isValidWithSiblings(t, n)) return false;
    return true;
  }

  MPType getNextValidPointType(std::deque<MPType>& queue, Node* n) {
    int count = (int)queue.size();
    for (int i = 0; i < count; i++) {
      MPType t = queue.front();
      queue.pop_front();
      // PointTypesThatIgnoreRules: only Elite, with BigGameHunter's override (M11).
      if (t == MPType::Elite && elitesIgnoreRules) return t;
      if (isValidPointType(t, n)) return t;
      queue.push_back(t);
    }
    return MPType::Unassigned;
  }

  void assignRemainingTypesToRandomPoints(std::deque<MPType>& queue) {
    for (int i = 0; i < 3; i++) {
      if (queue.empty()) break;
      std::vector<Node*> list;
      for (Node* p : getAllMapPoints()) if (p->type == MPType::Unassigned) list.push_back(p);
      stableShuffle(list, rng, coordLess);
      for (Node* item : list) {
        if (queue.empty()) break;
        item->type = getNextValidPointType(queue, item);
      }
    }
  }

  void assignPointTypes() {
    forEachInRow(grid, mapLength - 1, [&](Node* p) { p->type = MPType::RestSite; p->canBeModified = false; });
    // shouldReplaceTreasureWithElites == false for this port.
    forEachInRow(grid, mapLength - 7, [&](Node* p) { p->type = MPType::Treasure; p->canBeModified = false; });
    forEachInRow(grid, 1, [&](Node* p) { p->type = MPType::Monster; p->canBeModified = false; });

    std::deque<MPType> toAssign;
    for (int i = 0; i < numOfRests; i++) toAssign.push_back(MPType::RestSite);
    for (int i = 0; i < kNumOfShops; i++) toAssign.push_back(MPType::Shop);
    for (int i = 0; i < numOfElites; i++) toAssign.push_back(MPType::Elite);
    for (int i = 0; i < numOfUnknowns; i++) toAssign.push_back(MPType::Unknown);
    assignRemainingTypesToRandomPoints(toAssign);

    for (Node* p : getAllMapPoints()) if (p->type == MPType::Unassigned) p->type = MPType::Monster;
    bossPoint->type = MPType::Boss;
    startingPoint->type = MPType::Ancient;
  }

  // ---- MapPathPruning ----

  bool isInMap(Node* mp) {
    if (mp->type == MPType::Ancient || mp->type == MPType::Boss) return true;
    return grid[mp->col][mp->row] == mp;
  }
  bool isRemoved(Node* mp) { return grid[mp->col][mp->row] == nullptr; }

  void removePoint(Node* mp) {
    grid[mp->col][mp->row] = nullptr;
    startMapPoints.erase(mp);
    std::vector<Node*> kids(mp->children.begin(), mp->children.end());
    for (Node* c : kids) mp->removeChild(c);
    std::vector<Node*> pars(mp->parents.begin(), mp->parents.end());
    for (Node* p : pars) p->removeChild(mp);
  }

  static bool isValidSegmentStart(Node* n) {
    if (n->children.size() <= 1) return n->row == 0;
    return true;
  }
  static bool isValidSegmentEnd(Node* n) { return n->parents.size() >= 2; }

  std::vector<std::vector<Node*>> findAllPaths(Node* current) {
    std::vector<std::vector<Node*>> result;
    if (current->type == MPType::Boss) {
      result.push_back({current});
      return result;
    }
    for (Node* child : current->children) {
      auto sub = findAllPaths(child);
      for (auto& p : sub) {
        std::vector<Node*> np;
        np.reserve(p.size() + 1);
        np.push_back(current);
        np.insert(np.end(), p.begin(), p.end());
        result.push_back(std::move(np));
      }
    }
    return result;
  }

  static std::string generateSegmentKey(const std::vector<Node*>& segment) {
    Node* a = segment.front();
    Node* b = segment.back();
    std::string key;
    if (a->row == 0) {
      key = std::to_string(a->row) + "-" + std::to_string(b->col) + "," + std::to_string(b->row) + "-";
    } else {
      key = std::to_string(a->col) + "," + std::to_string(a->row) + "-" + std::to_string(b->col) + "," + std::to_string(b->row) + "-";
    }
    for (size_t i = 0; i < segment.size(); i++) {
      if (i) key += ",";
      key += std::to_string((int)segment[i]->type);
    }
    return key;
  }

  static bool overlappingSegment(const std::vector<Node*>& a, const std::vector<Node*>& b) {
    if (a.size() < 3 || b.size() < 3) return false;
    for (size_t i = 1; i + 2 <= a.size() && i < b.size(); i++)
      if (a[i] == b[i]) return true;
    return false;
  }

  void addSegmentsToDictionary(const std::vector<Node*>& path, std::map<std::string, std::vector<std::vector<Node*>>>& segments) {
    for (size_t i = 0; i + 1 < path.size(); i++) {
      if (!isValidSegmentStart(path[i])) continue;
      for (size_t j = 2; j < path.size() - i; j++) {
        Node* endPoint = path[i + j];
        if (!isValidSegmentEnd(endPoint)) continue;
        std::vector<Node*> seg(path.begin() + i, path.begin() + i + j + 1);
        std::string key = generateSegmentKey(seg);
        auto it = segments.find(key);
        if (it == segments.end()) {
          segments[key] = {seg};
        } else if (!std::any_of(it->second.begin(), it->second.end(), [&](const std::vector<Node*>& existing) {
                     return overlappingSegment(existing, seg);
                   })) {
          it->second.push_back(seg);
        }
      }
    }
  }

  std::vector<std::vector<std::vector<Node*>>> findMatchingSegments(Node* startPoint) {
    auto paths = findAllPaths(startPoint);
    std::map<std::string, std::vector<std::vector<Node*>>> segments;
    for (auto& path : paths) addSegmentsToDictionary(path, segments);
    std::vector<std::vector<std::vector<Node*>>> dupes;
    for (auto& kv : segments) if (kv.second.size() > 1) dupes.push_back(kv.second);
    return dupes;
  }

  bool pruneSegment(const std::vector<Node*>& segment) {
    bool result = false;
    for (size_t i = 0; i + 1 < segment.size(); i++) {
      Node* mp = segment[i];
      if (!isInMap(mp)) return true;
      bool skip = mp->children.size() > 1 || mp->parents.size() > 1 ||
                  std::any_of(mp->parents.begin(), mp->parents.end(), [&](Node* n) {
                    return n->children.size() == 1 && !isRemoved(n);
                  });
      if (skip) continue;
      std::vector<Node*> source(segment.begin() + i, segment.end());
      bool anyBranch = std::any_of(source.begin(), source.end(), [&](Node* n) {
        return n->children.size() > 1 && n->parents.size() == 1;
      });
      if (anyBranch) continue;
      if (segment.back()->parents.size() == 1) return false;
      bool anyChildEscapes = std::any_of(mp->children.begin(), mp->children.end(), [&](Node* c) {
        bool inSeg = std::find(segment.begin(), segment.end(), c) != segment.end();
        return !inSeg && c->parents.size() == 1;
      });
      if (!anyChildEscapes) {
        removePoint(mp);
        result = true;
      }
    }
    return result;
  }

  int pruneAllButLast(const std::vector<std::vector<Node*>>& matches) {
    int num = 0;
    for (auto& match : matches) {
      if (num == (int)matches.size() - 1) return num;
      if (pruneSegment(match)) num++;
    }
    return num;
  }

  static bool breakParentChildInSegment(std::vector<Node*>& segment) {
    bool result = false;
    for (size_t i = 0; i + 1 < segment.size(); i++) {
      Node* mp = segment[i];
      if (mp->children.size() >= 2) {
        Node* mp2 = segment[i + 1];
        if (mp2->parents.size() != 1) {
          mp->removeChild(mp2);
          result = true;
        }
      }
    }
    return result;
  }
  static bool breakParentChildInAnySegment(std::vector<std::vector<Node*>>& matches) {
    for (auto& m : matches) if (breakParentChildInSegment(m)) return true;
    return false;
  }

  bool prunePaths(std::vector<std::vector<std::vector<Node*>>>& matchingSegments) {
    for (auto& group : matchingSegments) {
      rng.shuffle(group);  // UnstableShuffle
      if (pruneAllButLast(group) != 0) return true;
      if (breakParentChildInAnySegment(group)) return true;
    }
    return false;
  }

  void pruneDuplicateSegments() {
    int iterations = 0;
    auto matching = findMatchingSegments(startingPoint);
    while (prunePaths(matching)) {
      iterations++;
      if (iterations > 50) break;  // C# throws; no exceptions on the 3DS: keep the map as is
      matching = findMatchingSegments(startingPoint);
    }
  }

  bool repairPointType(MPType type, int targetCount) {
    auto all = getAllMapPoints();
    int current = (int)std::count_if(all.begin(), all.end(), [&](Node* p) { return p->type == type; });
    int need = targetCount - current;
    if (need <= 0) return false;
    bool result = false;
    std::vector<Node*> candidates;
    for (Node* p : all) if (p->type == MPType::Monster && p->canBeModified) candidates.push_back(p);
    stableShuffle(candidates, rng, coordLess);
    for (Node* item : candidates) {
      if (need == 0) break;
      if (isValidPointType(type, item)) {
        item->type = type;
        need--;
        result = true;
      }
    }
    return result;
  }

  bool repairPrunedPointTypes() {
    // Not short-circuited: every call must run to keep the Rng stream in sync
    // with the original (C#'s `|=` on bool doesn't short-circuit either).
    bool a = repairPointType(MPType::Shop, kNumOfShops);
    bool b = repairPointType(MPType::Elite, numOfElites);
    bool c = repairPointType(MPType::RestSite, numOfRests);
    bool d = repairPointType(MPType::Unknown, numOfUnknowns);
    return a || b || c || d;
  }

  void pruneAndRepair() {
    for (int i = 0; i < 3; i++) {
      pruneDuplicateSegments();
      if (!repairPrunedPointTypes()) break;
    }
  }

  // ---- MapPostProcessing ----

  static bool isColumnEmpty(std::vector<std::vector<Node*>>& grid, int col) {
    for (int r = 0; r < (int)grid[col].size(); r++) if (grid[col][r]) return false;
    return true;
  }

  void centerGrid() {
    bool leftEmpty = isColumnEmpty(grid, 0) && isColumnEmpty(grid, 1);
    bool rightEmpty = isColumnEmpty(grid, kMapWidth - 1) && isColumnEmpty(grid, kMapWidth - 2);
    int shift = 0;
    if (leftEmpty && !rightEmpty) shift = -1;
    else if (!leftEmpty && rightEmpty) shift = 1;
    if (shift == 0) return;
    if (shift > 0) {
      for (int row = 0; row < mapLength; row++) {
        for (int col = kMapWidth - 1; col >= 0; col--) {
          Node* n = grid[col][row];
          grid[col][row] = nullptr;
          int nc = col + shift;
          if (nc < kMapWidth) {
            grid[nc][row] = n;
            if (n) n->col = nc;
          }
        }
      }
    } else {
      for (int row = 0; row < mapLength; row++) {
        for (int col = 0; col < kMapWidth; col++) {
          Node* n = grid[col][row];
          grid[col][row] = nullptr;
          int nc = col + shift;
          if (nc >= 0) {
            grid[nc][row] = n;
            if (n) n->col = nc;
          }
        }
      }
    }
  }

  void straightenPaths() {
    for (int row = 0; row < mapLength; row++) {
      for (int col = 0; col < kMapWidth; col++) {
        Node* n = grid[col][row];
        if (!n || n->parents.size() != 1 || n->children.size() != 1) continue;
        Node* parent = *n->parents.begin();
        Node* child = *n->children.begin();
        bool leansLeft = n->col < child->col && n->col < parent->col;
        bool leansRight = n->col > child->col && n->col > parent->col;
        if (leansLeft && col < kMapWidth - 1) {
          int nc = col + 1;
          if (grid[nc][row] != nullptr) continue;
          n->col = nc;
          grid[col][row] = nullptr;
          grid[nc][row] = n;
        }
        if (leansRight && col > 0) {
          int nc = col - 1;
          if (grid[nc][row] == nullptr) {
            n->col = nc;
            grid[col][row] = nullptr;
            grid[nc][row] = n;
          }
        }
      }
    }
  }

  static std::vector<int> neighborAllowedPositions(int column, int totalColumns) {
    std::vector<int> out;
    for (int i = -1; i <= 1; i++) {
      int v = column + i;
      if (v >= 0 && v < totalColumns) out.push_back(v);
    }
    return out;
  }

  std::vector<int> getAllowedPositions(Node* node) {
    std::vector<bool> allowed(kMapWidth, true);
    auto intersect = [&](int col) {
      std::vector<bool> mask(kMapWidth, false);
      for (int v : neighborAllowedPositions(col, kMapWidth)) mask[v] = true;
      for (int i = 0; i < kMapWidth; i++) allowed[i] = allowed[i] && mask[i];
    };
    for (Node* p : node->parents) intersect(p->col);
    for (Node* c : node->children) intersect(c->col);
    std::vector<int> out;
    for (int i = 0; i < kMapWidth; i++) if (allowed[i]) out.push_back(i);
    return out;
  }

  static int computeGap(int candidateCol, const std::vector<Node*>& rowNodes, Node* current) {
    int best = INT_MAX;
    for (Node* n : rowNodes) if (n != current) best = std::min(best, std::abs(candidateCol - n->col));
    return best;
  }

  void spreadAdjacentMapPoints() {
    for (int row = 0; row < mapLength; row++) {
      std::vector<Node*> rowNodes;
      for (int col = 0; col < kMapWidth; col++) if (grid[col][row]) rowNodes.push_back(grid[col][row]);
      bool changed;
      do {
        changed = false;
        for (Node* item : rowNodes) {
          int col = item->col;
          std::vector<int> allowedPositions = getAllowedPositions(item);
          int bestCol = col;
          int bestGap = computeGap(col, rowNodes, item);
          for (int candidate : allowedPositions) {
            if (candidate != col && (grid[candidate][row] == nullptr || grid[candidate][row] == item)) {
              int gap = computeGap(candidate, rowNodes, item);
              if (gap > bestGap) {
                bestCol = candidate;
                bestGap = gap;
              }
            }
          }
          if (bestCol != col) {
            grid[col][row] = nullptr;
            grid[bestCol][row] = item;
            item->col = bestCol;
            changed = true;
          }
        }
      } while (changed);
    }
  }
};

RoomType toRoomType(MPType t) {
  switch (t) {
    case MPType::Monster: return RoomType::Monster;
    case MPType::Elite: return RoomType::Elite;
    case MPType::RestSite: return RoomType::Rest;
    case MPType::Treasure: return RoomType::Treasure;
    case MPType::Unknown: return RoomType::Unknown;
    case MPType::Shop: return RoomType::Shop;
    case MPType::Boss: return RoomType::Boss;
    default: return RoomType::Monster;  // Unassigned/Ancient never reach this (the start is added separately)
  }
}

}  // namespace

namespace {
std::vector<MapNode> buildStandardActMap(Rng& mapRng, int actIndex, int numOfElites, bool hasSecondBoss,
                                         const MapTypeCounts* counts);
}  // namespace

std::vector<MapNode> generateStandardActMap(Rng& mapRng, int actIndex, int numOfElites, bool hasSecondBoss) {
  return buildStandardActMap(mapRng, actIndex, numOfElites, hasSecondBoss, nullptr);
}

std::vector<MapNode> generateStandardActMap(Rng& mapRng, int actIndex, const MapTypeCounts& counts, bool hasSecondBoss) {
  return buildStandardActMap(mapRng, actIndex, counts.elites, hasSecondBoss, &counts);
}

namespace {
std::vector<MapNode> buildStandardActMap(Rng& mapRng, int actIndex, int numOfElites, bool hasSecondBoss,
                                         const MapTypeCounts* counts) {
  // BaseNumberOfRooms: Overgrowth 15, Hive 14, Glory 13.
  static const int kRooms[] = {15, 14, 13};
  actIndex = std::clamp(actIndex, 0, 2);
  MapBuilder b(mapRng, kRooms[actIndex] + 1);
  b.startingPoint = b.makeNode(kMapWidth / 2, 0);
  b.bossPoint = b.makeNode(kMapWidth / 2, b.mapLength);

  // <Act>.GetMapPointTypes (before GenerateMap, per the C# constructor order);
  // MapPointTypeCounts.StandardRandomUnknownCount, minus one in Hive and Glory.
  // A MapPointTypeCounts override (M11) replaces it without drawing from the Rng.
  if (counts) {
    b.numOfRests = counts->rests;
    b.numOfUnknowns = counts->unknowns;
    b.elitesIgnoreRules = counts->elitesIgnoreRules;
  } else {
    if (actIndex == 0) b.numOfRests = nextGaussianInt(mapRng, 7, 1, 6, 7);
    else if (actIndex == 1) b.numOfRests = nextGaussianInt(mapRng, 6, 1, 6, 7);
    else b.numOfRests = mapRng.nextInt(5, 7);
    b.numOfUnknowns = nextGaussianInt(mapRng, 12, 1, 10, 14) - (actIndex > 0 ? 1 : 0);
  }
  b.numOfElites = numOfElites;

  b.generateMap();
  b.assignPointTypes();
  b.pruneAndRepair();
  b.centerGrid();
  b.spreadAdjacentMapPoints();
  b.straightenPaths();

  // Build the output: one MapNode per non-null grid point (rows 1..15 -> our
  // 0..14), boss appended last (row 15, col 3), next = child indices.
  std::vector<MapNode> nodes;
  std::vector<std::vector<int>> indexOf(kMapWidth, std::vector<int>(b.mapLength, -1));
  for (int row = 1; row < b.mapLength; row++) {
    for (int col = 0; col < kMapWidth; col++) {
      Node* n = b.grid[col][row];
      if (!n) continue;
      indexOf[col][row] = (int)nodes.size();
      MapNode mn;
      mn.col = col;
      mn.row = row - 1;
      mn.type = toRoomType(n->type);
      nodes.push_back(mn);
    }
  }
  int bossIndex = (int)nodes.size();
  MapNode bossNode;
  bossNode.col = kMapWidth / 2;
  bossNode.row = b.mapLength - 1;  // one past the last room row
  bossNode.type = RoomType::Boss;
  nodes.push_back(bossNode);
  if (hasSecondBoss) {  // StandardActMap: SecondBossMapPoint at (col / 2, rowCount + 1), the boss's child
    MapNode second = bossNode;
    second.row = b.mapLength;
    nodes[bossIndex].next.push_back(bossIndex + 1);
    nodes.push_back(second);
  }

  int outIdx = 0;
  for (int row = 1; row < b.mapLength; row++) {
    for (int col = 0; col < kMapWidth; col++) {
      Node* n = b.grid[col][row];
      if (!n) continue;
      for (Node* child : n->children) {
        if (child == b.bossPoint) {
          nodes[outIdx].next.push_back(bossIndex);
        } else {
          int ci = indexOf[child->col][child->row];
          if (ci >= 0) nodes[outIdx].next.push_back(ci);
        }
      }
      outIdx++;
    }
  }
  // The StartingMapPoint (MapPointType.Ancient) goes first, one row below the rooms; its
  // children are every room of the first row.
  MapNode start;
  start.col = kMapWidth / 2;
  start.row = -1;
  start.type = RoomType::Ancient;
  for (auto& n : nodes)
    for (int& c : n.next) ++c;
  for (int i = 0; i < (int)nodes.size(); ++i)
    if (nodes[i].row == 0) start.next.push_back(i + 1);
  nodes.insert(nodes.begin(), start);
  return nodes;
}
}  // namespace

// ---------------------------------------------------------------- SpoilsActMap (E2)
// MegaCrit.Sts2.Core.Map.SpoilsActMap: the act map SpoilsMap asks for in act 2. Seven paths that all
// pass through one centred treasure point (rows narrow towards it, then widen again), the
// standard point type rules and MapPathPruning, no MapPostProcessing. Same output indexing as
// generateStandardActMap (start point first, boss last).
namespace {

struct SpoilsBuilder : MapBuilder {
  int treasureRow = 0;
  SpoilsBuilder(Rng& r, int len) : MapBuilder(r, len), treasureRow(len - 7) {}

  Node* point(int col, int row) {  // ActMap.GetPoint (null outside the grid)
    if (col < 0 || col >= kMapWidth || row < 0 || row >= mapLength) return nullptr;
    return grid[col][row];
  }
  Node* getOrCreate(int col, int row) {  // SpoilsActMap.GetOrCreatePoint
    if (Node* n = point(col, row)) return n;
    Node* n = makeNode(col, row);
    if (col >= 0 && col < kMapWidth && row >= 0 && row < mapLength) grid[col][row] = n;
    return n;
  }
  std::pair<int, int> allowedColumns(int row) const {  // GetAllowedColumnsForRow
    int center = kMapWidth / 2;
    int toTreasure = std::abs(row - treasureRow);
    int toEnd = mapLength - 1 - row;
    int widthByEnd = std::min(center, std::max(0, toEnd) + 1);
    int half = std::min(center, std::min(toTreasure, widthByEnd));
    return {std::max(0, center - half), std::min(6, center + half)};
  }
  bool invalidCrossover(Node* current, int targetCol) {  // HasInvalidCrossover
    int diff = targetCol - current->col;
    if (diff == 0) return false;
    Node* n = point(targetCol, current->row);
    if (!n) return false;
    for (Node* child : n->children)
      if (child->col - n->col == -diff) return true;
    return false;
  }
  static std::vector<int> centeredPriority(int currentCol, int centerCol) {  // BuildCenteredPriorityList
    std::vector<int> list;
    int dir = (centerCol > currentCol) - (centerCol < currentCol);
    if (dir != 0) list.push_back(dir);
    list.push_back(0);
    if (dir != 0) list.push_back(-dir);
    if (std::find(list.begin(), list.end(), -1) == list.end()) list.push_back(-1);
    if (std::find(list.begin(), list.end(), 1) == list.end()) list.push_back(1);
    return list;
  }
  void nextCoord(Node* current, int& outCol, int& outRow) {  // GenerateNextCoord
    int row = current->row + 1;
    auto [minCol, maxCol] = allowedColumns(row);
    int center = kMapWidth / 2;
    std::vector<int> list = {-1, 0, 1};
    int toTreasure = treasureRow - current->row;
    if (toTreasure > 3) rng.shuffle(list);  // StableShuffle: the list is already sorted
    else if (toTreasure > 0) list = centeredPriority(current->col, center);
    else rng.shuffle(list);
    outRow = row;
    for (int d : list) {
      int next = d == -1 ? std::max(0, current->col - 1) : d == 1 ? std::min(6, current->col + 1) : current->col;
      if (next < minCol || next > maxCol || invalidCrossover(current, next)) continue;
      Node* p = point(next, row);
      bool parentsOk = !p || p->parents.contains(current) || p->parents.size() < 3;
      bool childrenOk = current == startingPoint || current->children.size() < 3 || (p && current->children.contains(p));
      if (parentsOk && childrenOk) { outCol = next; return; }  // (C# also asserts |step| <= 1)
    }
    int col = std::clamp(center, minCol, maxCol);
    if (std::abs(col - current->col) > 1) {
      int sign = (col > current->col) - (col < current->col);
      col = std::clamp(current->col + sign, minCol, maxCol);
    }
    if (invalidCrossover(current, col)) col = std::clamp(current->col, minCol, maxCol);
    outCol = col;  // C# throws when this still steps more than one column; it cannot with these bounds
  }
  void pathGenerateSpoils(Node* start) {
    Node* cur = start;
    while (cur->row < mapLength - 1) {
      int col, row;
      nextCoord(cur, col, row);
      Node* next = getOrCreate(col, row);
      cur->addChild(next);
      cur = next;
    }
  }
  void redirectToTreasure(Node* stray, Node* treasure) {
    std::vector<Node*> pars(stray->parents.begin(), stray->parents.end());
    for (Node* p : pars) { p->removeChild(stray); p->addChild(treasure); }
    std::vector<Node*> kids(stray->children.begin(), stray->children.end());
    for (Node* c : kids) { stray->removeChild(c); treasure->addChild(c); }
    grid[stray->col][stray->row] = nullptr;
  }
  void generateHourglass() {  // GenerateHourglassMap
    for (int i = 0; i < kMapWidth; i++) {
      Node* p = getOrCreate(rng.nextInt(0, 7), 1);
      if (i == 1)
        while (startMapPoints.contains(p)) p = getOrCreate(rng.nextInt(0, 7), 1);
      startMapPoints.insert(p);
      pathGenerateSpoils(p);
    }
    Node* treasure = getOrCreate(kMapWidth / 2, treasureRow);
    treasure->type = MPType::Treasure;
    treasure->canBeModified = false;
    std::vector<Node*> row;
    forEachInRow(grid, treasureRow, [&](Node* n) { row.push_back(n); });
    for (Node* n : row) if (n != treasure) redirectToTreasure(n, treasure);
    forEachInRow(grid, mapLength - 1, [&](Node* x) { if (!x->children.contains(bossPoint)) x->addChild(bossPoint); });
    forEachInRow(grid, 1, [&](Node* x) { if (!startingPoint->children.contains(x)) startingPoint->addChild(x); });
  }
  void assignSpoilsPointTypes() {  // SpoilsActMap.AssignPointTypes (no treasure row: the treasure is placed)
    forEachInRow(grid, mapLength - 1, [&](Node* p) { p->type = MPType::RestSite; p->canBeModified = false; });
    forEachInRow(grid, 1, [&](Node* p) { p->type = MPType::Monster; p->canBeModified = false; });
    std::deque<MPType> toAssign;
    for (int i = 0; i < numOfRests; i++) toAssign.push_back(MPType::RestSite);
    for (int i = 0; i < kNumOfShops; i++) toAssign.push_back(MPType::Shop);
    for (int i = 0; i < numOfElites; i++) toAssign.push_back(MPType::Elite);
    for (int i = 0; i < numOfUnknowns; i++) toAssign.push_back(MPType::Unknown);
    assignRemainingTypesToRandomPoints(toAssign);
    for (Node* p : getAllMapPoints()) if (p->type == MPType::Unassigned) p->type = MPType::Monster;
    bossPoint->type = MPType::Boss;
    startingPoint->type = MPType::Ancient;
  }
};

}  // namespace

std::vector<MapNode> generateSpoilsActMap(Rng& mapRng, int actIndex, int numOfElites, const MapTypeCounts* counts) {
  static const int kRooms[] = {15, 14, 13};
  actIndex = std::clamp(actIndex, 0, 2);
  SpoilsBuilder b(mapRng, kRooms[actIndex] + 1);
  // Constructor order: GetMapPointTypes(_rng) (or the override), then the points.
  if (counts) {
    b.numOfRests = counts->rests;
    b.numOfUnknowns = counts->unknowns;
    b.elitesIgnoreRules = counts->elitesIgnoreRules;
  } else {
    if (actIndex == 0) b.numOfRests = nextGaussianInt(mapRng, 7, 1, 6, 7);
    else if (actIndex == 1) b.numOfRests = nextGaussianInt(mapRng, 6, 1, 6, 7);
    else b.numOfRests = mapRng.nextInt(5, 7);
    b.numOfUnknowns = nextGaussianInt(mapRng, 12, 1, 10, 14) - (actIndex > 0 ? 1 : 0);
  }
  b.numOfElites = numOfElites;
  b.bossPoint = b.makeNode(kMapWidth / 2, b.mapLength);
  b.startingPoint = b.makeNode(kMapWidth / 2, 0);
  b.generateHourglass();
  b.assignSpoilsPointTypes();
  b.pruneAndRepair();

  // Output as in buildStandardActMap: rooms (rows 1.. -> 0..), the boss, then the start point first.
  std::vector<MapNode> nodes;
  std::vector<std::vector<int>> indexOf(kMapWidth, std::vector<int>(b.mapLength, -1));
  for (int row = 1; row < b.mapLength; row++)
    for (int col = 0; col < kMapWidth; col++) {
      Node* n = b.grid[col][row];
      if (!n) continue;
      indexOf[col][row] = (int)nodes.size();
      MapNode mn;
      mn.col = col;
      mn.row = row - 1;
      mn.type = toRoomType(n->type);
      nodes.push_back(mn);
    }
  int bossIndex = (int)nodes.size();
  MapNode bossNode;
  bossNode.col = kMapWidth / 2;
  bossNode.row = b.mapLength - 1;
  bossNode.type = RoomType::Boss;
  nodes.push_back(bossNode);
  int outIdx = 0;
  for (int row = 1; row < b.mapLength; row++)
    for (int col = 0; col < kMapWidth; col++) {
      Node* n = b.grid[col][row];
      if (!n) continue;
      for (Node* child : n->children) {
        if (child == b.bossPoint) nodes[outIdx].next.push_back(bossIndex);
        else if (int ci = indexOf[child->col][child->row]; ci >= 0) nodes[outIdx].next.push_back(ci);
      }
      outIdx++;
    }
  MapNode start;
  start.col = kMapWidth / 2;
  start.row = -1;
  start.type = RoomType::Ancient;
  for (auto& n : nodes)
    for (int& c : n.next) ++c;
  for (int i = 0; i < (int)nodes.size(); ++i)
    if (nodes[i].row == 0) start.next.push_back(i + 1);
  nodes.insert(nodes.begin(), start);
  return nodes;
}

}  // namespace sts
