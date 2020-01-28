/** fio_address_validator definitions file
 *  Description: Takes a fio.name and validates it as spec.
 *  @author Casey Gardiner
 *  @file fio_name_validator.hpp
 *  @copyright Dapix
 *
 *  Changes:
 */
#pragma once

#include <string>
//#include <regex>

//#include <boost/algorithm/string.hpp> // *DANGER - don't use! Severe performance side effects may result

#pragma once

namespace fioio {

    constexpr auto maxFioLen = 64;
    constexpr auto maxFioDomainLen = 62;
    std::vector<int> locationMap { 10, 20, 30, 40, 50, 60, 70, 80 };

    using namespace std;

    struct FioAddress {
        string fioaddress;
        string fioname;
        string fiodomain;
        bool domainOnly;
    };

    inline void getFioAddressStruct(const string &p, FioAddress &fa) {
        // Split the fio name and domain portions
        fa.fioname = "";
        fa.fiodomain = "";

        size_t pos = p.find('@');
        fa.domainOnly = pos == 0 || pos == string::npos;

        //Lower Case
        fa.fioaddress = p;
        for (auto &c : fa.fioaddress) {
            c = char(::tolower(c));
        }

        if (pos == string::npos) {
            fa.fiodomain = fa.fioaddress;
        } else {
            if (!fa.domainOnly) { fa.fioname = fa.fioaddress.substr(0, pos); }
            fa.fiodomain = fa.fioaddress.substr(pos + 1);
        }
    }

    inline int charValidate(const string &name){
        if (name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos) {
            return 2;
        }
        if (name.front() == '-' || name.back() == '-') {
            return 3;
        }

        return 0;
    }

    inline int validateFioNameFormat(const FioAddress &fa) {
        int rt = 0;
        if (fa.domainOnly) {
            if (fa.fiodomain.size() < 1 || fa.fiodomain.size() > maxFioDomainLen) {
                return 1;
            }
            rt = charValidate(fa.fiodomain);
        } else {
            if (fa.fioaddress.size() < 3 || fa.fioaddress.size() > maxFioLen ) {
                return 1;
            }
            rt = charValidate(fa.fioname) + charValidate(fa.fiodomain);
        }

        return rt;
    }

    inline bool isChainNameValid(const string &chain) {
        if (chain.size() >= 1 && chain.size() <= 10) {
            if (chain.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") !=
                std::string::npos) {
                return false;
            }
        }
        return true;
    }

    inline int isTPIDFormatValid(const string &tpid) {
        if( tpid.size() != 0 ){
            FioAddress fa;
            getFioAddressStruct(tpid, fa);
            return validateFioNameFormat(fa);
        }
        return 1;
    }

    inline bool isPubAddressValid(const std::string &address) {
        if ( address.length() > 0 && address.length() <= 128 ) {
            if(address.find_first_not_of(' ') != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    inline string chainToUpper(string chain) {
        transform(chain.begin(), chain.end(), chain.begin(), ::toupper);

        return chain;
    }

    inline bool isURLValid(const std::string &url) {
        if (url.length() >= 10 && url.length() <= 50) {
            return true;
        }
        return false;
    }

    inline bool isLocationValid(const uint16_t &location){
        if( std::find(locationMap.begin(), locationMap.end(), location)!= locationMap.end()){
            return true;
        }
        return false;
    }

    bool isStringInt(const std::string &s) {
        return !s.empty() && std::find_if(s.begin(),
                                          s.end(), [](char c) { return !std::isdigit(c); }) == s.end();
    }

    /** All alphanumeric characters except for "0", "I", "O", and "l" */
        static const char *pszBase58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
        static const int8_t mapBase58[256] = {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, -1, -1, -1, -1, -1, -1,
                -1, 9, 10, 11, 12, 13, 14, 15, 16, -1, 17, 18, 19, 20, 21, -1,
                22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, -1, -1, -1, -1, -1,
                -1, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, -1, 44, 45, 46,
                47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        };

        bool DecodeBase58(const char *psz, std::vector<unsigned char> &vch) {
            // Skip leading spaces.
            while (*psz && isspace(*psz))
                psz++;
            // Skip and count leading '1's.
            int zeroes = 0;
            int length = 0;
            while (*psz == '1') {
                zeroes++;
                psz++;
            }
            // Allocate enough space in big-endian base256 representation.
            int size = strlen(psz) * 733 / 1000 + 1; // log(58) / log(256), rounded up.
            std::vector<unsigned char> b256(size);
            // Process the characters.
            static_assert(sizeof(mapBase58) / sizeof(mapBase58[0]) == 256,
                          "mapBase58.size() should be 256"); // guarantee not out of range
            while (*psz && !isspace(*psz)) {
                // Decode base58 character
                int carry = mapBase58[(uint8_t) *psz];
                if (carry == -1)  // Invalid b58 character
                    return false;
                int i = 0;
                for (std::vector<unsigned char>::reverse_iterator it = b256.rbegin();
                     (carry != 0 || i < length) && (it != b256.rend()); ++it, ++i) {
                    carry += 58 * (*it);
                    *it = carry % 256;
                    carry /= 256;
                }
                assert(carry == 0);
                length = i;
                psz++;
            }
            // Skip trailing spaces.
            while (isspace(*psz))
                psz++;
            if (*psz != 0)
                return false;
            // Skip leading zeroes in b256.
            std::vector<unsigned char>::iterator it = b256.begin() + (size - length);
            while (it != b256.end() && *it == 0)
                it++;
            // Copy result into output vector.
            vch.reserve(zeroes + (b256.end() - it));
            vch.assign(zeroes, 0x00);
            while (it != b256.end())
                vch.push_back(*(it++));
            return true;
        }

        bool decode_base58(const string &str, vector<unsigned char> &vch) {
            return DecodeBase58(str.c_str(), vch);
        }
}
