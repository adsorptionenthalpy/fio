#pragma once

#include <vector>
#include <string>
#include <eosiolib/eosio.hpp>
#include <eosiolib/system.hpp>
#include <eosiolib/singleton.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/crypto.hpp>
#include "json.hpp"
#include "keyops.hpp"
#include "fioerror.hpp"
#include "fio_common_validator.hpp"
#include "chain_control.hpp"
#include "account_operations.hpp"

#ifndef YEARTOSECONDS
#define YEARTOSECONDS 31536000
#endif

#ifndef MAXBOUNTYTOKENSTOMINT
#define MAXBOUNTYTOKENSTOMINT 200000000000000000
#endif


namespace fioio {

    using namespace eosio;
    using namespace std;

    static const name FeeContract = name("fio.fee");    // account hosting the fee contract
    static const name SystemContract = name("fio.system");
    static const name TPIDContract = name("fio.tpid");
    static const name TokenContract = name("fio.token");
    static const name FOUNDATIONACCOUNT = name("fio.foundatn");
    static const name TREASURYACCOUNT = name("fio.treasury");

    struct config {
        name tokencontr; // owner of the token contract
        bool pmtson = true; // enable/disable payments

        EOSLIB_SERIALIZE(config, (tokencontr)(pmtson))
    };
    typedef singleton<"configs"_n, config> configs_singleton;


    static constexpr char char_to_symbol(char c) {
        if (c >= 'a' && c <= 'z')
            return (c - 'a') + 6;
        if (c >= '1' && c <= '5')
            return (c - '1') + 1;
        return 0;
    }


    static constexpr uint64_t string_to_name(const char *str) {

        uint32_t len = 0;
        while (str[len]) ++len;

        uint64_t value = 0;

        for (uint32_t i = 0; i <= 12; ++i) {
            uint64_t c = 0;
            if (i < len && i <= 12) c = uint64_t(char_to_symbol(str[i]));
            if (i < 12) {
                c &= 0x1f;
                c <<= 64 - 5 * (i + 1);
            } else {
                c &= 0x0f;
            }

            value |= c;
        }

        return value;
    }

    static constexpr uint64_t string_to_uint64_hash(const char *str) {

        uint32_t len = 0;
        while (str[len]) ++len;

        uint64_t value = 0;
        uint64_t multv = 0;
        if (len > 0) {
            multv = 60 / len;
        }
        for (uint32_t i = 0; i < len; ++i) {
            uint64_t c = 0;
            if (i < len) c = uint64_t(str[i]);

            if (i < 60) {
                c &= 0x1f;
                c <<= 64 - multv * (i + 1);
            } else {
                c &= 0x0f;
            }

            value |= c;
        }

        return value;
    }

    static uint128_t string_to_uint128_hash(const char *str) {

        eosio::checksum160 tmp;
        uint128_t retval=0;
        uint8_t *bp =(uint8_t*) &tmp;
        uint32_t len = 0;

        while (str[len]) ++len;

        tmp = eosio::sha1(str, len);

        bp = (uint8_t *)&tmp;
        memcpy(&retval,bp,sizeof(retval));

        return retval;
    }


    //use this for debug to see the value of your uint128_t, this will match what shows in get table.
    static std::string to_hex(const char* d, uint32_t s )
    {
        std::string r;
        const char* to_hex="0123456789abcdef";
        uint8_t* c = (uint8_t*)d;
        for( uint32_t i = 0; i < s; ++i ) {
          (r += to_hex[(c[i] >> 4)]) += to_hex[(c[i] & 0x0f)];

        }
        return r;
    }

    struct [[eosio::action]] bpreward {

      uint64_t rewards;

      uint64_t primary_key() const {return rewards;}

      EOSLIB_SERIALIZE(bpreward, (rewards))


    };

    typedef multi_index<"bprewards"_n, bpreward> bprewards_table;

    // @abi table bucketpool i64
    struct [[eosio::action]] bucketpool {

      uint64_t rewards;

      uint64_t primary_key() const {return rewards;}

      EOSLIB_SERIALIZE(bucketpool, (rewards))


    };

    typedef multi_index<"bpbucketpool"_n, bucketpool> bpbucketpool_table;

    // @abi table fdtnreward i64
    struct [[eosio::action]] fdtnreward {

      uint64_t rewards;

      uint64_t primary_key() const {return rewards;}

      EOSLIB_SERIALIZE(fdtnreward, (rewards))


    };

    typedef multi_index<"fdtnrewards"_n, fdtnreward> fdtnrewards_table;

    // @abi table bounties i64
    struct [[eosio::action]] bounty {

      uint64_t tokensminted;
      uint64_t primary_key() const {return tokensminted;}

      EOSLIB_SERIALIZE(bounty, (tokensminted))


    };

    typedef multi_index<"bounties"_n, bounty> bounties_table;

    void process_rewards(const string &tpid, const uint64_t &amount, const name &actor) {

      action(
      permission_level{actor,"active"_n},
      TREASURYACCOUNT,
      "fdtnrwdupdat"_n,
      std::make_tuple((uint64_t)(static_cast<double>(amount) * .02))
      ).send();
      fionames_table fionames(SystemContract, SystemContract.value);
      uint128_t fioaddhash = string_to_uint128_hash(tpid.c_str());

      auto namesbyname = fionames.get_index<"byname"_n>();
      auto fionamefound = namesbyname.find(fioaddhash);

      print("\nfionamefound: ",fionamefound->name,"\n");
      if (fionamefound != namesbyname.end()) {

        bounties_table bounties(TPIDContract, TPIDContract.value);
        uint64_t bamount = 0;
        eosio::print("\nBounties: ",bounties.begin()->tokensminted,"\n");
        if (bounties.begin()->tokensminted < MAXBOUNTYTOKENSTOMINT) {
          bamount = (uint64_t)(static_cast<double>(amount) * .65);
          eosio::print("\nBounty payout: ", bamount, "\n");
          action(permission_level{TREASURYACCOUNT, "active"_n},
            TokenContract, "mintfio"_n,
            make_tuple(bamount)
          ).send();

          action(
          permission_level{TREASURYACCOUNT,"active"_n},
          TPIDContract,
          "updatebounty"_n,
          std::make_tuple(bamount)
          ).send();

        }

        action(
        permission_level{actor,"active"_n},
        TPIDContract,
        "updatetpid"_n,
        std::make_tuple(tpid, actor, (amount / 10) + bamount)
        ).send();


        action(
        permission_level{actor,"active"_n},
        TREASURYACCOUNT,
        "bprewdupdate"_n,
        std::make_tuple((uint64_t)(static_cast<double>(amount) * .88))
        ).send();

        eosio::print("\nTest Bucket Update Amount: ",((uint64_t)(static_cast<double>(amount) * .88)), "\n");
      } else {
      print("Cannot register TPID or FIO Address not found. The transaction will continue without TPID payment.","\n");

      action(
      permission_level{actor,"active"_n},
      TREASURYACCOUNT,
      "bprewdupdate"_n,
      std::make_tuple((uint64_t)(static_cast<double>(amount) * .98))
      ).send();
    }

  }

    void processbucketrewards(const string &tpid, const uint64_t &amount, const name &actor) {

        action(
        permission_level{actor,"active"_n},
        TREASURYACCOUNT,
        "fdtnrwdupdat"_n,
        std::make_tuple((uint64_t)(static_cast<double>(amount) * .02))
        ).send();

        fionames_table fionames(SystemContract, SystemContract.value);
        uint128_t fioaddhash = string_to_uint128_hash(tpid.c_str());
        auto namesbyname = fionames.get_index<"byname"_n>();
        auto fionamefound = namesbyname.find(fioaddhash);
        print("\nfionamefound: ",fionamefound->name,"\n");
        if (fionamefound != namesbyname.end()) {

          bounties_table bounties(TPIDContract, TPIDContract.value);
          uint64_t bamount = 0;
          eosio::print("\nBounties: ",bounties.begin()->tokensminted,"\n");
          if (bounties.begin()->tokensminted < MAXBOUNTYTOKENSTOMINT) {
            bamount = (uint64_t)(static_cast<double>(amount) * .65);
            eosio::print("\nBounty payout: ", bamount, "\n");
            action(permission_level{TREASURYACCOUNT, "active"_n},
              TokenContract, "mintfio"_n,
              make_tuple(bamount)
            ).send();

            action(
            permission_level{TREASURYACCOUNT,"active"_n},
            TPIDContract,
            "updatebounty"_n,
            std::make_tuple(bamount)
            ).send();

          }

          action(
          permission_level{actor,"active"_n},
          TPIDContract,
          "updatetpid"_n,
          std::make_tuple(tpid, actor, (amount / 10) + bamount)
          ).send();


          action(
          permission_level{actor,"active"_n},
          TREASURYACCOUNT,
          "bppoolupdate"_n,
          std::make_tuple((uint64_t)(static_cast<double>(amount) * .88))
          ).send();
          eosio::print("\nTest Bucket Update Amount: ",((uint64_t)(static_cast<double>(amount) * .88)), "\n");
        } else {
        print("Cannot register TPID or FIO Address not found. The transaction will continue without TPID payment.","\n");

        action(
        permission_level{actor,"active"_n},
        TREASURYACCOUNT,
        "bppoolupdate"_n,
        std::make_tuple((uint64_t)(static_cast<double>(amount) * .98))
        ).send();

      }
    }


  //Precondition: this method should only be called by register_producer, vote_producer, unregister_producer, register_proxy, unregister_proxy, vote_proxy
  // after transaction fees have been defined
  //Postcondition: the foundation has been rewarded 2% of the transaction fee and top 21/active block producers rewarded 98% of the transaction fee
  void processrewardsnotpid(const uint64_t &amount, const name &actor) {

    action(
    permission_level{actor,"active"_n},
    TREASURYACCOUNT,
    "bprewdupdate"_n,
    std::make_tuple((uint64_t)(static_cast<double>(amount) * .98))
    ).send();

    action(
    permission_level{actor,"active"_n},
    TREASURYACCOUNT,
    "fdtnrwdupdat"_n,
    std::make_tuple((uint64_t)(static_cast<double>(amount) * .02))
    ).send();
  }


} // namespace fioio
