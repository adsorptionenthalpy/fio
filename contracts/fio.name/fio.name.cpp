/** FioName Token implementation file
 *  Description: FioName smart contract allows issuance of unique domains and names for easy public address resolution
 *  @author Adam Androulidakis, Casey Gardiner, Ciju John, Ed Rotthoff, Phil Mesnier
 *  @file fio.name.hpp
 *  @copyright Dapix
 */

#include <eosiolib/asset.hpp>
#include "fio.name.hpp"
#include <fio.common/fio.common.hpp>
#include <fio.common/json.hpp>

#include <eosio/chain/fioio/fioerror.hpp>
#include <eosio/chain/fioio/fio_common_validator.hpp>
#include <eosio/chain/fioio/chain_control.hpp>

namespace fioio {

    class FioNameLookup : public contract {
    private:
        domains_table domains;
        chains_table chains;
        fionames_table fionames;
        keynames_table keynames;
        trxfees_singleton trxfees;
        eosio_names_table eosionames;
        config appConfig;

        const account_name TokenContract = eosio::string_to_name(TOKEN_CONTRACT);

    public:
        FioNameLookup(account_name self)
                : contract(self), domains(self, self), fionames(self, self), keynames(self, self),
                  trxfees(FeeContract, FeeContract), eosionames(self, self), chains(self, self) {
            configs_singleton configsSingleton(FeeContract, FeeContract);
            appConfig = configsSingleton.get_or_default(config());
        }

        [[eosio::action]]
        void registername(const string &fioname, const name &actor) {
            require_auth(actor); // check for requestor authority; required for fee transfer

            // Split the fio name and domain portions
            FioAddress fa;
            getFioAddressStruct(fioname, fa);
            register_errors(fa);

            uint64_t domainHash = ::eosio::string_to_uint64_t(fa.fiodomain.c_str());
            uint32_t expiration_time = fio_table_update(actor, fa, domainHash);

            const auto fees = trxfees.get_or_default(trxfee());
            asset registerFee = fees.upaddress;
            fio_fees(actor, registerFee);

            nlohmann::json json = {{"status",     "OK"},
                                   {"fio_name",   fa.fioaddress},
                                   {"expiration", expiration_time}};
            send_response(json.dump().c_str());
        }


        /***
         * Given a fio user name, chain name and chain specific address will attach address to the user's FIO fioname.
         *
         * @param fioaddress The FIO user name e.g. "adam.fio"
         * @param tokencode The chain name e.g. "btc"
         * @param pubaddress The chain specific user address
         */
        [[eosio::action]]
        void addaddress(const string &fioaddress, const string &tokencode, const string &pubaddress,
                        const account_name &actor) {
            FioAddress fa;
            getFioAddressStruct(fioaddress, fa);
            addaddress_errors(tokencode, pubaddress, fa);

            chain_data_update(fioaddress, tokencode, pubaddress, fa,actor);

            const auto fees = trxfees.get_or_default(trxfee());
            asset registerFee = fees.upaddress;
            fio_fees(actor, registerFee);

            nlohmann::json json = {{"status",     "OK"},
                                   {"fioaddress", fioaddress},
                                   {"tokencode",  tokencode},
                                   {"pubaddress", pubaddress},
                                   {"actor",      actor}};
            send_response(json.dump().c_str());
        } //addaddress


        [[eosio::action]]
        void adddomain(const string &domain, const string &pubaddress, const account_name &actor)
        {
          // insert/update key into key-name table for reverse lookup
          auto idx = keynames.get_index<N(bykey)>();
          auto keyhash = string_to_uint64_t(pubaddress.c_str());
          auto matchingItem = idx.lower_bound(keyhash);
          uint64_t domainHash = string_to_uint64_t(domain.c_str());
          auto domain_iter = domains.find(domainHash);

          uint32_t domain_expiration = domain_iter->expiration;
          // TODO: Is there a fee for adding a domain ?

          // Advance to the first entry matching the specified address and chain
          while(matchingItem != idx.end() && matchingItem->keyhash == keyhash) {
              matchingItem++;
          }

          if(matchingItem == idx.end() || matchingItem->keyhash != keyhash) {
              keynames.emplace(_self, [&](struct key_name &k) {
                  k.id = keynames.available_primary_key();        // use next available primary key
                  k.key = pubaddress;                             // persist key
                  k.keyhash = keyhash;                            // persist key hash
                  k.chaintype = 0;                       // specific chain type
                  k.name = domain_iter->name;                    // FIO name
                  k.expiration = domain_expiration;
              });
          } else {
              idx.modify(matchingItem, _self, [&](struct key_name &k) {
                  k.name = domain_iter->name;    // FIO name
              });
          }

          nlohmann::json json = {{"status",     "OK"},
                                 {"domain", domain},
                                 {"tokencode",  0},
                                 {"pubaddress", pubaddress},
                                 {"actor",      actor}};

          send_response(json.dump().c_str());

        } //adddomain

        /**
         *
         * Separate out the management of platform-specific identities from the fio names
         * and domains. bind2eosio, the space restricted variant of "Bind to EOSIO"
         * takes a platform-specific account name and a wallet generated public key.
         *
         * First it verifie that either tsi is a new account and none othe exists, or this
         * is an existing eosio account and it is indeed bound to this key. If it is a new,
         * unbound account name, then bind name to the key and add it to the list.
         *
         **/

        [[eosio::action]]
        void bind2eosio(name account, const string &client_key, bool existing) {
            // The caller of this contract must have the private key in their wallet for the FIO.SYSTEM account
            require_auth(::eosio::string_to_name(FIO_SYSTEM));

            auto other = eosionames.find(account);
            if ( other != eosionames.end()) {
                eosio_assert_message_code(existing && client_key == other->clientkey, "EOSIO account already bound",
                                          ErrorPubAddressExist);
                // name in the table and it matches
            } else {
                eosio_assert_message_code(!existing, "existing EOSIO account not bound to a key", ErrorPubAddressExist);
                eosionames.emplace(_self, [&] (struct eosio_name &p) {
                    p.account = account;
                    p.clientkey = client_key;
                });
            }
        }

        void removename () {
            print("Begin removename()");
        }

        void removedomain () {
            print("Begin removedomain()");
        }

        void rmvaddress () {
            print("Begin rmvaddress()");
        }

        inline void fio_fees(const account_name &actor, const asset &registerFee) const {
            if(appConfig.pmtson) {
                // check for funds is implicitly done as part of the funds transfer.
                print("Collecting registration fees: ", registerFee);
                action(permission_level{actor, N(active)},
                       TokenContract, N(transfer),
                       make_tuple(actor, _self, registerFee,
                                  string("Registration fees. Thank you."))
                ).send();
            } else {
                print("Payments currently disabled.");
            }
        }

        inline void register_errors(const FioAddress &fa) const {
            int res = fa.domainOnly ? isFioNameValid(fa.fiodomain) * 10 : isFioNameValid(fa.fioname);
            fio_400_assert(res == 0, "fio_name", fa.fioaddress, "Invalid FIO name format", ErrorInvalidFioNameFormat);
        }

        inline void addaddress_errors(const string &tokencode, const string &pubaddress, const FioAddress &fa) const {
            fio_400_assert(isFioNameValid(fa.fioaddress), "fioaddress", fa.fioaddress, "Invalid public address format",
                           ErrorDomainAlreadyRegistered);
            // Chain input validation
            fio_400_assert(isChainNameValid(tokencode), "tokencode", tokencode, "Invalid token code format",
                           ErrorInvalidFioNameFormat);
            fio_400_assert(isPubAddressValid(pubaddress), "pubaddress", pubaddress, "Invalid public address format",
                           ErrorChainAddressEmpty);

        }

        uint32_t fio_table_update (const name &actor, const FioAddress &fa, uint64_t domainHash) {
            uint32_t expiration_time = 0;

            if ( fa.domainOnly ) { // domain register
                // check for domain availability
                auto domains_iter = domains.find(domainHash);
                fio_400_assert(domains_iter == domains.end(), "fio_name", fa.fioaddress,
                               "FIO domain already registered", ErrorDomainAlreadyRegistered);
                // check if callee has requisite dapix funds. Also update to domain fees

                //get the expiration for this new domain.
                expiration_time = get_now_plus_one_year();

                // Issue, create and transfer nft domain token
                // Add domain entry in domain table
                domains.emplace(_self, [&] (struct domain &d) {
                    d.name = fa.fiodomain;
                    d.domainhash = domainHash;
                    d.expiration = expiration_time;
                    d.account = actor;
                });
               adddomain(fa.fiodomain, actor.to_string(), actor);
            } else { // fioname register

                // check if domain exists.
                auto domains_iter = domains.find(domainHash);
                fio_400_assert(domains_iter != domains.end(), "fio_name", fa.fioaddress, "FIO Domain not registered",
                               ErrorDomainNotRegistered);

                // TODO check if domain permission is valid.

                //check if the domain is expired.
                uint32_t domain_expiration = domains_iter->expiration;
                uint32_t present_time = now();
                fio_400_assert(present_time <= domain_expiration, "fio_name", fa.fioaddress, "FIO Domain expired",
                               ErrorDomainExpired);

                // check if fioname is available
                uint64_t nameHash = string_to_uint64_t(fa.fioaddress.c_str());
                print("Name hash: ", nameHash, ", Domain has: ", domainHash, "\n");
                auto fioname_iter = fionames.find(nameHash);
                fio_400_assert(fioname_iter == fionames.end(), "fio_name", fa.fioaddress,
                               "FIO name already registered", ErrorFioNameAlreadyRegistered);

                //set the expiration on this new fioname
                expiration_time = get_now_plus_one_year();

                // check if callee has requisite dapix funds.
                // DO SOMETHING

                // Issue, create and transfer fioname token
                // DO SOMETHING

                // Add fioname entry in fionames table
                fionames.emplace(_self, [&] (struct fioname &a) {
                    a.name = fa.fioaddress;
                    a.addresses = vector<string>(20, ""); // TODO: Remove prior to production
                    a.namehash = nameHash;
                    a.domain = fa.fiodomain;
                    a.domainhash = domainHash;
                    a.expiration = expiration_time;
                    a.account = actor;
                });
                addaddress(fa.fioaddress, "FIO", actor.to_string(), actor);
            } // else

            return expiration_time;
        }

        void chain_data_update(const string &fioaddress, const string &tokencode, const string &pubaddress,
                               const FioAddress &fa, const account_name &actor) {
            uint64_t nameHash = string_to_uint64_t(fa.fioaddress.c_str());
            uint64_t domainHash = string_to_uint64_t(fa.fiodomain.c_str());

            auto fioname_iter = fionames.find(nameHash);
            fio_404_assert(fioname_iter != fionames.end(), "FIO Address not found", ErrorFioNameNotRegistered);

            //check that the name is not expired
            uint32_t name_expiration = fioname_iter->expiration;
            uint32_t present_time = now();

            uint64_t account = fioname_iter->account;
            print("account: ",account," actor: ",actor,"\n");
            fio_403_assert(account == actor,ErrorSignature);

            //print("name_expiration: ", name_expiration, ", present_time: ", present_time, "\n");
            fio_400_assert(present_time <= name_expiration, "fioaddress", fioaddress,
                           "FIO Address or FIO Domain expired", ErrorFioNameExpired);

            auto domains_iter = domains.find(domainHash);
            fio_404_assert(domains_iter != domains.end(), "FIO Domain not found", ErrorDomainNotRegistered);

            uint32_t expiration = domains_iter->expiration;
            fio_400_assert(present_time <= expiration, "domain", fa.fiodomain, "FIO Address or FIO Domain expired",
                           ErrorDomainExpired);

            uint64_t chainhash = string_to_uint64_t(tokencode.c_str());
            auto size = distance(chains.cbegin(), chains.cend());
            auto chain_iter = chains.find(chainhash);

            if(chain_iter == chains.end()) {
                chains.emplace(_self, [&](struct chainList &a) {
                    a.id = size;
                    a.chainname = tokencode;
                    a.chainhash = chainhash;
                });
                chain_iter = chains.find(chainhash);
            }

            // insert/update <chain, address> pair
            fionames.modify(fioname_iter, _self, [&](struct fioname &a) {
                a.addresses[static_cast<size_t>((chain_iter)->by_index())] = pubaddress;
            });

            // insert/update key into key-name table for reverse lookup
            auto idx = keynames.get_index<N(bykey)>();
            auto keyhash = string_to_uint64_t(pubaddress.c_str());
            auto matchingItem = idx.lower_bound(keyhash);

            // Advance to the first entry matching the specified address and chain
            while(matchingItem != idx.end() && matchingItem->keyhash == keyhash) {
                matchingItem++;
            }

            if(matchingItem == idx.end() || matchingItem->keyhash != keyhash) {
                keynames.emplace(_self, [&](struct key_name &k) {
                    k.id = keynames.available_primary_key();        // use next available primary key
                    k.key = pubaddress;                             // persist key
                    k.keyhash = keyhash;                            // persist key hash
                    k.chaintype = (chain_iter)->by_index();                       // specific chain type
                    k.name = fioname_iter->name;                    // FIO name
                    k.expiration = name_expiration;
                });
            } else {
                idx.modify(matchingItem, _self, [&](struct key_name &k) {
                    k.name = fioname_iter->name;    // FIO name
                });
            }
        }

        /***
         * This method will return now plus one year.
         * the result is the present block time, which is number of seconds since 1970
         * incremented by secondss per year.
         */
        inline uint32_t get_now_plus_one_year() {
            uint32_t present_time = now();
            uint32_t incremented_time = present_time + DAYTOSECONDS;
            return incremented_time;
        }
    }; // class FioNameLookup

    EOSIO_ABI(FioNameLookup, (registername)(addaddress)(adddomain)(removename)(removedomain)(rmvaddress)(bind2eosio))
}
