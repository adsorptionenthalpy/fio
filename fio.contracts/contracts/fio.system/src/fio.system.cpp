/** fio system contract file
 *  Description: this contract controls many of the core functions of the fio protocol.
 *  @author Adam Androulidakis, Casey Gardiner, Ed Rotthoff
 *  @file fio.system.cpp
 *  @copyright Dapix
 *
 *  Changes:
 */
#include <fio.system/fio.system.hpp>
#include <eosiolib/dispatcher.hpp>
#include <eosiolib/crypto.h>
#include "producer_pay.cpp"
#include "delegate_bandwidth.cpp"
#include "voting.cpp"
#include <fio.address/fio.address.hpp>
#include <fio.fee/fio.fee.hpp>

namespace eosiosystem {

    system_contract::system_contract(name s, name code, datastream<const char *> ds)
            : native(s, code, ds),
              _voters(_self, _self.value),
              _producers(_self, _self.value),
              _global(_self, _self.value),
              _global2(_self, _self.value),
              _global3(_self, _self.value),
              _fionames(AddressContract, AddressContract.value),
              _domains(AddressContract, AddressContract.value),
              _accountmap(AddressContract, AddressContract.value),
              _fiofees(FeeContract, FeeContract.value){
        _gstate = _global.exists() ? _global.get() : get_default_parameters();
        _gstate2 = _global2.exists() ? _global2.get() : eosio_global_state2{};
        _gstate3 = _global3.exists() ? _global3.get() : eosio_global_state3{};
    }

    eosiosystem::eosio_global_state eosiosystem::system_contract::get_default_parameters() {
        eosio_global_state dp;
        get_blockchain_parameters(dp);
        return dp;
    }

    time_point eosiosystem::system_contract::current_time_point() {
        const static time_point ct{microseconds{static_cast<int64_t>( current_time())}};
        return ct;
    }

    time_point_sec eosiosystem::system_contract::current_time_point_sec() {
        const static time_point_sec cts{current_time_point()};
        return cts;
    }

    block_timestamp eosiosystem::system_contract::current_block_time() {
        const static block_timestamp cbt{current_time_point()};
        return cbt;
    }

    eosiosystem::system_contract::~system_contract() {
        _global.set(_gstate, _self);
        _global2.set(_gstate2, _self);
        _global3.set(_gstate3, _self);
    }

    void eosiosystem::system_contract::setparams(const eosio::blockchain_parameters &params) {
        require_auth(_self);
        (eosio::blockchain_parameters & )(_gstate) = params;
        check(3 <= _gstate.max_authority_depth, "max_authority_depth should be at least 3");
        set_blockchain_parameters(params);
    }

    void eosiosystem::system_contract::setpriv(name account, uint8_t ispriv) {
        require_auth(_self);
        set_privileged(account.value, ispriv);
    }

    void eosiosystem::system_contract::rmvproducer(name producer) {
        require_auth(_self);
        auto prod = _producers.find(producer.value);
        check(prod != _producers.end(), "producer not found");
        _producers.modify(prod, same_payer, [&](auto &p) {
            p.deactivate();
        });
    }

    void eosiosystem::system_contract::updtrevision(uint8_t revision) {
        require_auth(_self);
        check(_gstate2.revision < 255, "can not increment revision"); // prevent wrap around
        check(revision == _gstate2.revision + 1, "can only increment revision by one");
        check(revision <= 1, // set upper bound to greatest revision supported in the code
              "specified revision is not yet supported by the code");
        _gstate2.revision = revision;
    }

    /**
     *  Called after a new account is created. This code enforces resource-limits rules
     *  for new accounts as well as new account naming conventions.
     *
     *  Account names containing '.' symbols must have a suffix equal to the name of the creator.
     *  This allows users who buy a premium name (shorter than 12 characters with no dots) to be the only ones
     *  who can create accounts with the creator's name as a suffix.
     *
     */
    void eosiosystem::native::newaccount(name creator,
                                         name newact,
                                         ignore <authority> owner,
                                         ignore <authority> active) {


        check((creator == SYSTEMACCOUNT || creator == TokenContract ||
                 creator == AddressContract), "new account is not permitted");
        
        if (creator != _self) {
            uint64_t tmp = newact.value >> 4;
            bool has_dot = false;

            for (uint32_t i = 0; i < 12; ++i) {
                has_dot |= !(tmp & 0x1f);
                tmp >>= 5;
            }
            if (has_dot) { // or is less than 12 characters
                auto suffix = newact.suffix();
                if (suffix != newact) {
                    check(creator == suffix, "only suffix may create this account");
                }
            }
        }

       //in the FIO protocol we want all of our accounts to be created with unlimited
       //CPU NET and RAM. to do this we need our accounts to NOT have entrees in the
       //resources table. our accounts will all be unlimited CPU NET and RAM for the
       //foreseeable future of the FIO protocol.
       // user_resources_table userres(_self, newact.value);

       // userres.emplace(newact, [&](auto &res) {
       //     res.owner = newact;
           // res.net_weight = asset(0, system_contract::get_core_symbol());
           // res.cpu_weight = asset(0, system_contract::get_core_symbol());
       // });

       // set_resource_limits(newact.value, 0, 0, 0);
    }

    void eosiosystem::native::setabi(name acnt, const std::vector<char> &abi) {

        check( (acnt == MSIGACCOUNT ||
                acnt == WHITELISTACCOUNT ||
                acnt == WRAPACCOUNT ||
                acnt == FeeContract ||
                acnt == AddressContract ||
                acnt == TPIDContract ||
                acnt == TokenContract ||
                acnt == FOUNDATIONACCOUNT ||
                acnt == TREASURYACCOUNT ||
                acnt == SYSTEMACCOUNT), "setabi is not permitted");


        eosio::multi_index<"abihash"_n, abi_hash> table(_self, _self.value);
        auto itr = table.find(acnt.value);
        if (itr == table.end()) {
            table.emplace(acnt, [&](auto &row) {
                row.owner = acnt;
                sha256(const_cast<char *>(abi.data()), abi.size(), &row.hash);
            });
        } else {
            table.modify(itr, same_payer, [&](auto &row) {
                sha256(const_cast<char *>(abi.data()), abi.size(), &row.hash);
            });
        }
    }

    void eosiosystem::system_contract::init(unsigned_int version, symbol core) {
        require_auth(_self);
        check(version.value == 0, "unsupported version for init action");
    }

} /// fio.system


EOSIO_DISPATCH( eosiosystem::system_contract,
// native.hpp (newaccount definition is actually in fio.system.cpp)
(newaccount)(updateauth)(deleteauth)(linkauth)(unlinkauth)(canceldelay)(onerror)(setabi)
        // fio.system.cpp
        (init)(setparams)(setpriv)
        (rmvproducer)(updtrevision)
        // delegate_bandwidth.cpp
        (updatepower)
        // voting.cpp
        (regproducer)(regiproducer)(unregprod)(voteproducer)(voteproxy)(setautoproxy)(crautoproxy)(
        unregproxy)(regiproxy)(regproxy)
        // producer_pay.cpp
        (onblock)
        (resetclaim)
)
