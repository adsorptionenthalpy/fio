#pragma once

#include <vector>
#include <string>
//#include <time>

#include <eosiolib/eosio.hpp>
#include <eosiolib/singleton.hpp>
#include <eosiolib/asset.hpp>

#ifndef FEE_CONTRACT
#define FEE_CONTRACT "fio.fee"
#endif

#ifndef TOKEN_CONTRACT
#define TOKEN_CONTRACT "eosio.token"
#endif

#ifndef FIO_SYSTEM
#define FIO_SYSTEM "fio.system"
#endif

#ifndef DAYTOSECONDS
#define DAYTOSECONDS 31561920
#endif

namespace fioio {

    using namespace eosio;
    using namespace std;

    struct trxfee {

        // election info
        uint64_t    id = 0;             // current election id
        time        expiration = now(); // current election expiration

        // wallet names associated fees
        asset  domregiter = asset(140000, S(4, FIO));   // Fee paid upon the original domain registration/renewal by the user registering. Allows the owner to retain ownership
        // of the wallet domain for a period of 1 year or until transfer
        asset  nameregister = asset(10000, S(4, FIO));  // Fee paid upon the original name registration/renewal by the user registering. Allows the owner to retain ownership
        // of the wallet name for a period of 1 year or until the expiration date of wallet domain. Re-sets the counter for Fee-free Transaction.
        asset  domtransfer = asset(140000, S(4, FIO));  // Fee paid upon wallet domain transfer of ownership by the transferring user.
        asset  nametransfer = asset(1000, S(4, FIO));   // Fee paid upon wallet name transfer of ownership by the transferring user.
        asset  namelookup = asset(1000, S(4, FIO));     // Fee paid for looking up a public address for a given wallet name and coin.
        asset  upaddress = asset(1000, S(4, FIO));      // Fees paid when wallet name to public address mapping is updated.

        // taken associated fees
        asset  transfer = asset(1000, S(4, FIO));   // Fee paid when FIO token is transferred.

        // meta-data associated fees
        asset metadata = asset(1000, S(4, FIO));    // Fee paind when recording information about the transaction (i.e. status or Request)

        EOSLIB_SERIALIZE(trxfee, (id)(expiration) (domregiter)(nameregister)(domtransfer)(nametransfer)(namelookup)(upaddress)(transfer)(metadata))
    }; // struct trxfee
    typedef singleton<N(trxfees), trxfee> trxfees_singleton;    // singleton folding the fee structure
    static const account_name FeeContract = eosio::string_to_name(FEE_CONTRACT);    // account hosting the fee contract

    struct config {
        name tokencontr; // owner of the token contract
        bool pmtson = false; // enable/disable payments

        EOSLIB_SERIALIZE(config, (tokencontr)(pmtson))
    };
    typedef singleton<N(configs), config> configs_singleton;

} // namespace fioio
