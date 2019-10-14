/** FioTreasury implementation file
 *  Description: FioTreasury smart contract controls block producer and tpid payments.
 *  @author Adam Androulidakis
 *  @modifedby
 *  @file fio.treasury.cpp
 *  @copyright Dapix
 */

#define TESTNET

#ifdef TESTNET
#define REWARDMAX 100
#define PAYSCHEDTIME 121
#define FDTNRWDTHRESH 100
#define MAXTOMINT 5000000000
#define MAXRESERVE 15000000000
#else
#define REWARDMAX 100000000000
#define PAYSCHEDTIME 172801
#define FDTNRWDTHRESH 100000000000
#define MAXTOMINT 50000000000000
#define MAXRESERVE 50000000000000000
#endif

#include "fio.treasury.hpp"

namespace fioio {

    class [[eosio::contract("FIOTreasury")]]  FIOTreasury : public eosio::contract {

    private:
        tpids_table tpids;
        fionames_table fionames;
        domains_table domains;
        rewards_table clockstate;
        bprewards_table bprewards;
        bpbucketpool_table bucketrewards;
        fdtnrewards_table fdtnrewards;
        voteshares_table voteshares;
        eosiosystem::eosio_global_state gstate;
        eosiosystem::global_state_singleton global;
        eosiosystem::producers_table producers;
        bool rewardspaid;
        uint64_t lasttpidpayout;

    public:
        using contract::contract;

        FIOTreasury(name s, name code, datastream<const char *> ds) : contract(s, code, ds),
                                                                      tpids(TPIDContract, TPIDContract.value),
                                                                      fionames(SystemContract, SystemContract.value),
                                                                      domains(SystemContract, SystemContract.value),
                                                                      bprewards(_self, _self.value),
                                                                      clockstate(_self, _self.value),
                                                                      voteshares(_self, _self.value),
                                                                      producers("eosio"_n, name("eosio").value),
                                                                      global("eosio"_n, name("eosio").value),
                                                                      fdtnrewards(_self, _self.value),
                                                                      bucketrewards(_self, _self.value) {
        }

        // @abi action
        [[eosio::action]]
        void tpidclaim(const name &actor) {

            require_auth(actor);

            uint64_t tpids_paid = 0;

            auto clockiter = clockstate.begin();

            //This contract should only be able to iterate throughout the entire tpids table to
            //to check for rewards once every x blocks.
            if (now() > clockiter->lasttpidpayout + 60) {
                for (const auto &itr : tpids) {
                    if (itr.rewards >= REWARDMAX) {  //100 FIO (100,000,000,000 SUF)
                        auto namesbyname = fionames.get_index<"byname"_n>();
                        auto itrfio = namesbyname.find(string_to_uint128_hash(itr.fioaddress.c_str()));

                        // If the fioaddress exists (address could have been burned)
                        if (itrfio != namesbyname.end()) {
                            action(permission_level{get_self(), "active"_n},
                                   TokenContract, "transfer"_n,
                                   make_tuple(TREASURYACCOUNT, name(itrfio->owner_account),
                                              asset(itr.rewards, symbol("FIO", 9)),
                                              string("Paying TPID from treasury."))
                            ).send();
                        } else { //Allocate to BP buckets instead
                            uint64_t reward = bprewards.get().rewards;
                            reward += itr.rewards;
                            bprewards.set(bpreward{reward}, _self);
                        }
                        action(permission_level{get_self(), "active"_n},
                               "fio.tpid"_n, "rewardspaid"_n,
                               make_tuple(itr.fioaddress)
                        ).send();
                    } // endif itr.rewards >=

                    tpids_paid++;
                    if (tpids_paid >= 100) break; //only paying 100 tpids
                } // for (auto &itr : tpids)

                //update the clock but only if there has been a tpid paid out.
                if (tpids_paid > 0) {
                    action(permission_level{get_self(), "active"_n},
                           get_self(), "updateclock"_n,
                           make_tuple()
                    ).send();
                }
            } //end if lasttpidpayout < now() 60

            nlohmann::json json = {{"status",     "OK"},
                                   {"tpids_paid", tpids_paid}};
            send_response(json.dump().c_str());
        } //tpid_claim

        // @abi action
        [[eosio::action]]
        void bpclaim(const string &fio_address, const name &actor) {

            require_auth(actor);
            auto namesbyname = fionames.get_index<"byname"_n>();
            auto fioiter = namesbyname.find(string_to_uint128_hash(fio_address.c_str()));

            fio_400_assert(fioiter != namesbyname.end(), fio_address, "fio_address",
                           "Invalid FIO Address", ErrorNoFioAddressProducer);

            uint64_t producer = fioiter->owner_account;

            fio_400_assert(producers.find(producer) != producers.end(), fio_address, "fio_address",
                           "FIO Address not producer or nothing payable", ErrorNoFioAddressProducer);

            auto clockiter = clockstate.begin();

            /***************  Pay schedule expiration *******************/
            //if it has been 24 hours, transfer remaining producer vote_shares to the foundation and record the rewards back into bprewards,
            // then erase the pay schedule so a new one can be created in a subsequent call to bpclaim.
            if (now() >= clockiter->payschedtimer + PAYSCHEDTIME) { //+ 172801

                if (std::distance(voteshares.begin(), voteshares.end()) > 0) {

                    auto iter = voteshares.begin();
                    while (iter != voteshares.end()) {
                        iter = voteshares.erase(iter);
                    }
                }
            }

            //*********** CREATE PAYSCHEDULE **************
            // If there is no pay schedule then create a new one
            if (std::distance(voteshares.begin(), voteshares.end()) == 0) { //if new payschedule
                //Create the payment schedule
                uint64_t bpcounter = 0;
                auto proditer = producers.get_index<"prototalvote"_n>();
                for (const auto &itr : proditer) {
                    if (itr.is_active) {
                        voteshares.emplace(get_self(), [&](auto &p) {
                            p.owner = itr.owner;
                            p.votes = itr.total_votes;
                        });
                    }

                    bpcounter++;
                    if (bpcounter > 42) break;
                } // &itr : producers

                //Move 1/365 of the bucketpool to the bpshare
                uint64_t temp = bprewards.get().rewards;
                uint64_t amount = static_cast<uint64_t>(bucketrewards.get().rewards / 365);
                bprewards.set(bpreward{temp + amount}, _self);
                temp = bucketrewards.get().rewards;
                bucketrewards.set(bucketpool{temp - amount}, _self);

                //uint64_t projectedpay = bprewards.begin()->rewards;

                uint64_t tomint = MAXTOMINT - bprewards.get().rewards;

                // from DEV1 tests - if (bprewards.begin()->rewards < 5000000000 && clockiter->reservetokensminted < 15000000000) { // lowered values for testing
                if (bprewards.get().rewards < MAXTOMINT && clockiter->reservetokensminted < MAXRESERVE) {

                    //Mint new tokens up to 50,000 FIO
                    action(permission_level{get_self(), "active"_n},
                           TokenContract, "mintfio"_n,
                           make_tuple(tomint)
                    ).send();

                    clockstate.modify(clockiter, get_self(), [&](auto &entry) {
                        entry.reservetokensminted += tomint;
                    });

                    //Include the minted tokens in the reward payout
                    temp = bprewards.get().rewards;
                    bprewards.set(bpreward{temp + tomint}, _self);
                    //This new reward amount that has been minted will be appended to the rewards being divied up next
                }
                //!!!rewards is now 0 in the bprewards table and can no longer be referred to. If needed use projectedpay

                // All bps are now in pay schedule, calculate the shares
                uint64_t bpcount = std::distance(voteshares.begin(), voteshares.end());
                uint64_t abpcount = 21;

                if (bpcount >= 42) bpcount = 42; //limit to 42 producers in voteshares
                if (bpcount <= 21) abpcount = bpcount;
                auto bprewardstat = bprewards.get();
                uint64_t tostandbybps = static_cast<uint64_t>(bprewardstat.rewards * .60);
                uint64_t toactivebps = static_cast<uint64_t>(bprewardstat.rewards * .40);

                bpcounter = 0;
                uint64_t abpayshare = 0;
                uint64_t sbpayshare = 0;
                gstate = global.get();
                for (const auto &itr : voteshares) {
                    abpayshare = static_cast<uint64_t>(toactivebps / abpcount);
                    sbpayshare = static_cast<uint64_t>((tostandbybps) *
                                                       (itr.votes / gstate.total_producer_vote_weight));
                    if (bpcounter <= abpcount) {
                        voteshares.modify(itr, get_self(), [&](auto &entry) {
                            entry.abpayshare = abpayshare;
                        });
                    }
                    voteshares.modify(itr, get_self(), [&](auto &entry) {
                        entry.sbpayshare = sbpayshare;
                    });
                    bpcounter++;
                } // &itr : voteshares

                //Start 24 track for daily pay
                clockstate.modify(clockiter, get_self(), [&](auto &entry) {
                    entry.payschedtimer = now();
                });
            } //if new payschedule
            //*********** END OF CREATE PAYSCHEDULE **************

            auto bpiter = voteshares.find(producer);

            const auto &prod = producers.get(producer);

            /******* Payouts *******/
            //This contract should only allow the producer to be able to claim rewards once every 172800 blocks (1 day).
            uint64_t payout = 0;

            if (bpiter != voteshares.end()) {
                payout = static_cast<uint64_t>(bpiter->abpayshare + bpiter->sbpayshare);

                auto domainsbyname = domains.get_index<"byname"_n>();
                auto domiter = domainsbyname.find(fioiter->domainhash);

                fio_400_assert(now() < domiter->expiration, domiter->name, "domain",
                               "FIO Domain expired", ErrorDomainExpired);

                fio_400_assert(now() < fioiter->expiration, fio_address, "fio_address",
                               "FIO Address expired", ErrorFioNameExpired);

                check(prod.active(), "producer does not have an active key");
                if (payout > 0) {
                    action(permission_level{get_self(), "active"_n},
                           TokenContract, "transfer"_n,
                           make_tuple(TREASURYACCOUNT, name(bpiter->owner), asset(payout, symbol("FIO", 9)),
                                      string("Paying producer from treasury."))
                    ).send();

                    // Reduce the producer's share of daily rewards and bucketrewards

                    if (bpiter->abpayshare > 0) {
                        auto temp = bprewards.get().rewards;
                        bprewards.set(bpreward{temp - payout}, _self);
                    }
                    //Keep track of rewards paid for reserve minting
                    clockstate.modify(clockiter, get_self(), [&](auto &entry) {
                        entry.rewardspaid += payout;
                    });

                    //Invoke system contract to reset producer last_claim_time and unpaid_blocks
                    action(permission_level{get_self(), "active"_n},
                           SystemContract, "resetclaim"_n,
                           make_tuple(producer)
                    ).send();
                }
                // PAY FOUNDATION //
                fdtnreward fdtnstate = fdtnrewards.get();
                if (fdtnstate.rewards > FDTNRWDTHRESH) { // 100 FIO = 100000000000 SUFs
                    action(permission_level{get_self(), "active"_n},
                           TokenContract, "transfer"_n,
                           make_tuple(TREASURYACCOUNT, FOUNDATIONACCOUNT, asset(fdtnstate.rewards, symbol("FIO", 9)),
                                      string("Paying foundation from treasury."))
                    ).send();

                    //Clear the foundation rewards counter
                    fdtnrewards.set(fdtnreward{0}, _self);
                    //////////////////////////////////////
                }
                //remove the producer from payschedule
                voteshares.erase(bpiter);
            } //endif now() > bpiter + 172800

            nlohmann::json json = {{"status", "OK"},
                                   {"amount", payout}};

            send_response(json.dump().c_str());

        } //bpclaim

        // @abi action
        [[eosio::action]]
        void updateclock() {
            require_auth(TREASURYACCOUNT);

            auto clockiter = clockstate.begin();

            clockstate.erase(clockiter);
            clockstate.emplace(_self, [&](struct treasurystate &entry) {
                entry.lasttpidpayout = now();
            });
        }

        // @abi action
        [[eosio::action]]
        void startclock() {
            require_auth(TREASURYACCOUNT);

            if (std::distance(clockstate.begin(), clockstate.end()) == 0) {
                clockstate.emplace(_self, [&](struct treasurystate &entry) {
                    entry.lasttpidpayout = now() - 56;
                    entry.payschedtimer = now();
                });
            }

            bucketrewards.set(bucketpool{0}, _self);
            bprewdupdate(0);
        }

        // @abi action
        [[eosio::action]]
        void bprewdupdate(const uint64_t &amount) {

            eosio_assert((has_auth(SystemContract) || has_auth(TokenContract)) || has_auth(TREASURYACCOUNT) ||
                         (has_auth("fio.reqobt"_n)) || (has_auth("eosio"_n)),
                         "missing required authority of fio.system, fio.treasury, fio.token, eosio or fio.reqobt");

            if (!bprewards.exists()) {
                bprewards.set(bpreward{amount}, _self);
            } else {
                bprewards.set(bpreward{bprewards.get().rewards + amount}, _self);
            }
        }

        // @abi action
        [[eosio::action]]
        void bppoolupdate(const uint64_t &amount) {

            eosio_assert((has_auth(SystemContract) || has_auth(TokenContract)) || has_auth(TREASURYACCOUNT) ||
                         (has_auth("fio.reqobt"_n)),
                         "missing required authority of fio.system, fio.treasury, fio.token, or fio.reqobt");

            if (!bucketrewards.exists()) {
                bucketrewards.set(bucketpool{amount}, _self);
            } else {
                bucketrewards.set(bucketpool{bucketrewards.get().rewards + amount}, _self);
            }
        }

        // @abi action
        [[eosio::action]]
        void fdtnrwdupdat(const uint64_t &amount) {

            eosio_assert((has_auth(SystemContract) || has_auth(TokenContract)) || has_auth(TREASURYACCOUNT) ||
                         (has_auth("fio.reqobt"_n)) || (has_auth("eosio"_n)),
                         "missing required authority of fio.system, fio.token, fio.treasury or fio.reqobt");

            if (!fdtnrewards.exists()) {
                fdtnrewards.set(fdtnreward{amount}, _self);
            } else {
                fdtnrewards.set(fdtnreward{fdtnrewards.get().rewards + amount}, _self);
            }
        }
    }; //class FIOTreasury

    EOSIO_DISPATCH(FIOTreasury, (tpidclaim)(updateclock)(startclock)(bprewdupdate)(fdtnrwdupdat)(bppoolupdate)
    (bpclaim))
}
