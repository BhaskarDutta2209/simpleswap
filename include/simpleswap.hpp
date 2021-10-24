#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>
#include <eosio/transaction.hpp>
#include <eosio/singleton.hpp>

using namespace eosio;
using namespace std;

class [[eosio::contract("simpleswap")]] simpleswap : public contract {
  public:
    using contract::contract;

    ACTION giveout(name receiver, asset quantity, name token_contract, uint64_t pool_id);
    ACTION swap(name username, string beneficiary, asset from, name contract_name, string to_chain, string to_token);
    ACTION setoracle(name oracle_account);
    ACTION useticket(uint64_t ticket_key);
    ACTION createpool(asset initial_supply, name contract_name, string target_chain, string target_token);
    ACTION withdraw(name account, asset quantity, name token_contract);
    ACTION clearall();


    [[eosio::on_notify("*::transfer")]]
    void deposite(name sender, name receiver, asset quantity, string memo);

    simpleswap(name receiver, name code, datastream<const char*> ds) : contract(receiver, code, ds),
      _pools(receiver, receiver.value),
      _swaptickets(receiver, receiver.value),
      _wallets(receiver, receiver.value),
      _constants(receiver, receiver.value) {}

  private:
    TABLE pools {
      uint64_t key;
      symbol token_symbol;
      name token_contract;
      string x_chain;
      string x_token;
      asset balance;

      auto primary_key() const { return key; }
      uint128_t secondary_key() const { return ((uint128_t)token_symbol.code().raw() << 64)|(uint128_t)token_contract.value; }
    };
    typedef multi_index<name("pools"), pools,
      eosio::indexed_by<name("secid"), eosio::const_mem_fun<pools, uint128_t, &pools::secondary_key>>
    > pools_table;
    pools_table _pools;

    TABLE swaptickets {
      uint64_t key;
      name username;
      string beneficiary;
      asset amount;
      name contract_name;
      string to_chain;
      string to_token;

      auto primary_key() const { return key; }
      uint64_t by_username() const { return username.value; }
    };
    typedef multi_index<name("swaptickets"), swaptickets,
      eosio::indexed_by<name("byusername"), eosio::const_mem_fun<swaptickets, uint64_t, &swaptickets::by_username>>
    > swaptickets_table;
    swaptickets_table _swaptickets;

    TABLE wallet {
      uint64_t wallet_id;
      name username;
      asset balance;
      name contract_name;

      auto primary_key() const { return wallet_id; }
      uint128_t secondary_key() const { return ((uint128_t)username.value << 64)|(uint128_t)balance.symbol.code().raw(); }
      uint64_t by_account() const { return username.value; }
    };
    typedef multi_index<name("wallet"), wallet,
      eosio::indexed_by<name("secid"), eosio::const_mem_fun<wallet, uint128_t, &wallet::secondary_key>>,
      eosio::indexed_by<name("byaccount"), eosio::const_mem_fun<wallet, uint64_t, &wallet::by_account>>
    > wallets_table;
    wallets_table _wallets;

    TABLE constants {
      name oracle;
    };
    typedef singleton<name("constants"), constants> constants_table;
    constants_table _constants;

    pair<bool, uint64_t> havebalance(name account, asset quantity, name contract_name);
    void addbalance(name sender, asset quantity, name contract_name);
    void subbalance(uint64_t wallet_id, asset quantity);
    void sendtoken(name from, name from_permission, name to, asset quantity, name token_contract, string memo);
    pair<bool, uint64_t> findpool(symbol token_symbol, name contract_name, string x_chain, string x_token);
    void addtopool(uint64_t key, asset quantity);
    void subtopool(uint64_t key, asset quantity);

};
