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
#include "fio.rewards.hpp"

#ifndef YEARTOSECONDS
#define YEARTOSECONDS 31536000
#endif

namespace fioio {

    using namespace eosio;
    using namespace std;
    using time = uint32_t;

    static const name FeeContract = name("fio.fee");    // account hosting the fee contract
    static const name SystemContract = name("fio.system");
    static const name TPIDContract = name("fio.tpid");
    static const name TokenContract = name("fio.token");
    static const name FOUNDATIONACCOUNT = name("fio.foundatn");

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


} // namespace fioio
