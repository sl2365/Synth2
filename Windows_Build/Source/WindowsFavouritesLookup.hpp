#pragma once
// Windows-only browser lookup cache. No changes to favourites.json or .sy1 files.
// Rebuilt after library refresh / successful favourites save; never during paint.
#include "WindowsFavourites.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace oxo::winfav {
class Lookup {
public:
    template<class Bank>
    void RebuildBanks(const fs::path& root, const std::vector<Bank>& banks) {
        std::vector<std::string> keys;
        std::unordered_map<std::string,int> firstBank;
        keys.reserve(banks.size());
        firstBank.reserve(banks.size());
        for(size_t i=0;i<banks.size();++i) {
            const auto& bank=banks[i];
            std::string key=BankKey(root,fs::path(bank.source),bank.readOnly);
            // Preserve legacy behaviour if multiple banks share the same key.
            firstBank.emplace(key,static_cast<int>(i));
            keys.push_back(std::move(key));
        }
        bankKeys_.swap(keys);
        firstBank_.swap(firstBank);
    }

    void RebuildFavourites(const std::vector<Item>& favourites) {
        std::unordered_map<std::string,std::unordered_set<int>> byBank;
        for(const auto& item:favourites)
            byBank[item.bank].insert(item.slot);
        favouriteSlots_.swap(byBank);
    }

    std::string BankKeyAt(int bank) const {
        if(bank<0 || static_cast<size_t>(bank)>=bankKeys_.size())return {};
        return bankKeys_[static_cast<size_t>(bank)];
    }
    bool FindBank(const std::string& key,int& bank) const {
        const auto it=firstBank_.find(key);
        if(it==firstBank_.end())return false;
        bank=it->second;
        return true;
    }
    bool Contains(int bank,int slot) const {
        if(bank<0 || slot<0 || static_cast<size_t>(bank)>=bankKeys_.size())return false;
        const auto it=favouriteSlots_.find(bankKeys_[static_cast<size_t>(bank)]);
        return it!=favouriteSlots_.end() && it->second.find(slot)!=it->second.end();
    }
private:
    std::vector<std::string> bankKeys_;
    std::unordered_map<std::string,int> firstBank_;
    std::unordered_map<std::string,std::unordered_set<int>> favouriteSlots_;
};
} // namespace oxo::winfav
