#include<simpleswap.hpp>

using namespace std;
using namespace eosio;

pair<bool, uint64_t> simpleswap::havebalance(name account, asset quantity, name contract_name) {
  auto wt = _wallets.get_index<name("secid")>();
  uint128_t key = ((uint128_t)account.value << 64) | (uint128_t)quantity.symbol.code().raw();
  auto itr = wt.find(key);

  if(itr == wt.end()) {
    return make_pair(false, 0);
  }

  if(itr->contract_name == contract_name) {
    return make_pair(true, itr->wallet_id);
  }

  while(itr != wt.end()) {
    if(
      itr->username == account &&
      itr->balance >= quantity &&
      itr->contract_name == contract_name
    ) {
      return make_pair(true, itr->wallet_id);
    }
    itr++;
  }

  return make_pair(false, 0);
}

void simpleswap::addbalance(name sender, asset quantity, name contract_name) {

  pair<bool, uint64_t> res = havebalance(sender, asset(0, quantity.symbol), contract_name);
  if(res.first) {
    auto itr = _wallets.find(res.second);
    _wallets.modify(itr, get_self(), [&](auto& row){
      row.balance += quantity;
    });
  } else {
    _wallets.emplace(get_self(), [&](auto& row){
      row.username = sender;
      row.balance = quantity;
      row.contract_name = contract_name;
    });
  }

}

void simpleswap::subbalance(uint64_t wallet_id, asset quantity) {
  auto itr = _wallets.find(wallet_id);
  check(itr != _wallets.end(), "wallet_id is non existing");

  if(itr->balance == quantity) {
    _wallets.erase(itr);
  } else {
    _wallets.modify(itr, get_self(), [&](auto& row){
      row.balance = row.balance - quantity;
    });
  }
}

void simpleswap::sendtoken(name from, name from_permission, name to, asset quantity, name token_contract, string memo) {
  if(!(to == get_self() && from == get_self()) && quantity.amount > 0) {

      action {
          permission_level(from, from_permission),
          token_contract,
          name("transfer"),
          std::make_tuple(from, to, quantity, memo)
      }.send();
  }
}

pair<bool, uint64_t> simpleswap::findpool(symbol token_symbol, name contract_name, string x_chain, string x_token) {

  auto pt = _pools.get_index<name("secid")>();
  uint128_t key = ((uint128_t)token_symbol.code().raw() << 64) | (uint128_t)contract_name.value;
  auto itr = pt.find(key);

  if(itr == pt.end()) {
    return make_pair(false, 0);
  }

  if(itr->x_chain == x_chain && itr->x_token == x_token) {
    return make_pair(true, itr->key);
  }

  while(itr != pt.end()) {

    if(
      itr->token_symbol == token_symbol &&
      itr->token_contract == contract_name &&
      itr->x_chain == x_chain &&
      itr->x_token == x_token
    ) {
      return make_pair(true, itr->key);
    }
    itr++;
  }

  return make_pair(false, 0);
}

void simpleswap::addtopool(uint64_t key, asset quantity) {
  auto itr = _pools.find(key);
  check(itr != _pools.end(), "pool_id is non existing");

  _pools.modify(itr, get_self(), [&](auto& row){
    row.balance += quantity;
  });
}

void simpleswap::subtopool(uint64_t key, asset quantity) {
  auto itr = _pools.find(key);
  check(itr != _pools.end(), "pool_id is non existing");

  _pools.modify(itr, get_self(), [&](auto& row){
    row.balance = row.balance - quantity;
  });
}

ACTION simpleswap::setoracle(name oracle_account) {
  require_auth(get_self());
  auto entry = _constants.get_or_create(get_self());
  entry.oracle = oracle_account;
  _constants.set(entry, get_self());
}

ACTION simpleswap::giveout(name receiver, asset quantity, name token_contract, uint64_t pool_id) {
  require_auth(_constants.get().oracle);
  subtopool(pool_id, quantity);
  sendtoken(get_self(), name("active"), receiver, quantity, token_contract, "Swap payout");
}

ACTION simpleswap::swap(name username, string beneficiary, asset from, name contract_name, string to_chain, string to_token) {

  require_auth(username);

  pair<bool, uint64_t> res = findpool(from.symbol, contract_name, to_chain, to_token);
  check(res.first, "pool not found");

  // Check if wallet has enough balance
  auto wallet = _wallets.get_index<name("secid")>();
  uint128_t key = ((uint128_t)username.value << 64) | (uint128_t)from.symbol.code().raw();
  auto wallet_itr = wallet.find(key);

  check(wallet_itr != wallet.end(), "Sufficient balance not found in wallet");

  bool foundInWallet = false;
  uint64_t wallet_id;
  if(
    wallet_itr->contract_name == contract_name &&
    wallet_itr->balance >= from
  ) {
    foundInWallet = true;
    wallet_id = wallet_itr->wallet_id;
  } else {
    while(wallet_itr != wallet.end()) {
      if(
        wallet_itr->username == username &&
        wallet_itr->balance >= from &&
        wallet_itr->contract_name == contract_name
      ) {
        foundInWallet = true;
        wallet_id = wallet_itr->wallet_id;
        break;
      }
      wallet_itr++;
    }
  }
  check(foundInWallet, "Sufficient balance not found in wallet");

  // Subtract the amount from wallet
  subbalance(wallet_id, from);

  // Generate the ticket
  _swaptickets.emplace(get_self(), [&](auto& row) {
    row.key = _swaptickets.available_primary_key();
    row.username = username;
    row.beneficiary = beneficiary;
    row.amount = from;
    row.contract_name = contract_name;
    row.to_chain = to_chain;
    row.to_token = to_token;
  });

}

ACTION simpleswap::useticket(uint64_t ticket_key) {
  require_auth(_constants.get().oracle);
  auto itr = _swaptickets.find(ticket_key);
  check(itr != _swaptickets.end(), "ticket_key non existent");
  _swaptickets.erase(itr);
}

ACTION simpleswap::createpool(asset initial_supply, name contract_name, string target_chain, string target_token) {

  // Check if pool is present
  pair<bool, uint64_t> isPresent = findpool(initial_supply.symbol, contract_name, target_chain, target_token);

  check(!isPresent.first, "Pool is already present");

  _pools.emplace(get_self(), [&](auto& row){
    row.key = _pools.available_primary_key();
    row.token_symbol = initial_supply.symbol;
    row.token_contract = contract_name;
    row.x_chain = target_chain;
    row.x_token = target_token;
    row.balance = initial_supply;
  });

}

ACTION simpleswap::withdraw(name account, asset quantity, name token_contract) {
  require_auth(account);

  // Check have balance
  pair<bool, uint64_t> res = havebalance(account, quantity, token_contract);
  check(res.first, "Insufficient balance");

  // Send token to the account
  sendtoken(get_self(), name("active"), account, quantity, token_contract, "Withdraw");

  // Subtract the balance
  subbalance(res.second, quantity);
}

ACTION simpleswap::clearall() {
  require_auth(get_self());

  auto i1 = _pools.begin();
  while(i1 != _pools.end()) {
    i1 = _pools.erase(i1);
  }

  auto i2 = _swaptickets.begin();
  while(i2 != _swaptickets.end()) {
    i2 = _swaptickets.erase(i2);
  }

  auto i3 = _wallets.begin();
  while(i3 != _wallets.end()) {
    i3 = _wallets.erase(i3);
  }

  _constants.remove();
}

[[eosio::on_notify("*::transfer")]]
void simpleswap::deposite(name sender, name receiver, asset quantity, string memo) {
  if(receiver != get_self()) return;

  if(sender == get_self() ||
    memo == "Swap payout" ||
    memo == "Withdraw" ||
    memo == "Donate"
  ) {
    return;
  }

  // add to wallet
  addbalance(sender, quantity, get_first_receiver());
}
