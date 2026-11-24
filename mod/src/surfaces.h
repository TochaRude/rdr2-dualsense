// What Arthur (or his horse) is standing on.
// RAGE names a ground material by joaat("<name>_<its line in materials.dat>"), e.g. "mud_soft_39" (found by
// matching GTA V's published material hashes: "default_0", "concrete_1"...). So the material hash the game
// gives us is identified by trying known material names with every line number, then mapped to a surface:
//   1. wading = water
//   2. labels you taught in game (F10 cycles the label of the ground under you) -> DualSenseRDR2/materials.json
//   3. the identified material name (mud_soft -> mud, pine_needles -> grass, wood_roof -> wood...)
//   4. context: indoors = wooden floor, snow on the ground = snow
//   5. otherwise plain ground
#pragma once

#include "common.h"

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

namespace dsr2 {

class Surfaces {
public:
    ~Surfaces() { shutdown(); }
    void shutdown();
    static const std::vector<std::string>& labels();   // ground grass mud stone gravel wood snow sand water
    void load(const fs::path& json);
    // a material seen for the first time is identified in the background (a few ms); until then: context
    std::string classify(uint32_t material, bool indoors, float submerged, float snow_level);
    // the label after `current` for this material (back to the identified one = untaught), saves, returns it
    std::string teach(uint32_t material, const std::string& current);
    bool taught(uint32_t material) const { return taught_.count(material) != 0; }
    std::string name_of(uint32_t material) const;       // "mud_soft_39", "" = unknown / not identified yet

    static std::string identify(uint32_t material);     // blocking search: "mud_soft_39" or ""
    static std::string label_for(const std::string& name);

private:
    void save() const;
    void start_worker();
    fs::path path_;
    std::map<uint32_t, std::string> taught_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<uint32_t> queue_;
    std::map<uint32_t, std::string> names_;            // "" = searched, no match
    std::unordered_map<uint32_t, std::string> label_cache_;  // script-thread fast cache
    std::thread worker_;
    bool stop_ = false;
    bool shutting_down_ = false;
    std::set<uint32_t> asked_;
};

}  // namespace dsr2
