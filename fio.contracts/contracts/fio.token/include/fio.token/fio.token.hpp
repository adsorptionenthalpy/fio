/** FioToken implementation file
 *  Description: FioToken is the smart contract that help manage the FIO Token.
 *  @author Adam Androulidakis, Casey Gardiner, Ed Rotthoff
 *  @modifedby
 *  @file fio.token.cpp
 *  @copyright Dapix
 */

#pragma once

#include <fio.common/fio.common.hpp>
#include <fio.address/fio.address.hpp>
#include <fio.fee/fio.fee.hpp>
#include <fio.tpid/fio.tpid.hpp>

namespace eosiosystem {
    class system_contract;
}

namespace eosio {
    using namespace fioio;

    using std::string;

    class [[eosio::contract("fio.token")]] token : public contract {
    private:
        fioio::eosio_names_table eosionames;
        fioio::fiofee_table fiofees;
        fioio::config appConfig;
        fioio::tpids_table tpids;
        fioio::fionames_table fionames;
        eosiosystem::locked_tokens_table lockedTokensTable;

    public:
        token(name s, name code, datastream<const char *> ds) : contract(s, code, ds),
                                                                eosionames(fioio::AddressContract,
                                                                           fioio::AddressContract.value),
                                                                fionames(fioio::AddressContract,
                                                                         fioio::AddressContract.value),
                                                                fiofees(fioio::FeeContract, fioio::FeeContract.value),
                                                                tpids(TPIDContract, TPIDContract.value),
                                                                lockedTokensTable(SYSTEMACCOUNT, SYSTEMACCOUNT.value){
            fioio::configs_singleton configsSingleton(fioio::FeeContract, fioio::FeeContract.value);
            appConfig = configsSingleton.get_or_default(fioio::config());
        }

        [[eosio::action]]
        void create(asset maximum_supply);

        [[eosio::action]]
        void issue(name to, asset quantity, string memo);

        [[eosio::action]]
        void mintfio(const name &to, const uint64_t &amount);

        inline void fio_fees(const name &actor, const asset &fee);


        [[eosio::action]]
        void retire(asset quantity, string memo);

        [[eosio::action]]
        void transfer(name from,
                      name to,
                      asset quantity,
                      string memo);

        [[eosio::action]]
        void trnsfiopubky(const string &payee_public_key,
                          const int64_t &amount,
                          const int64_t &max_fee,
                          const name &actor,
                          const string &tpid);

        static asset get_supply(name token_contract_account, symbol_code sym_code) {
            stats statstable(token_contract_account, sym_code.raw());
            const auto &st = statstable.get(sym_code.raw());
            return st.supply;
        }

        static asset get_balance(name token_contract_account, name owner, symbol_code sym_code) {
            accounts accountstable(token_contract_account, owner.value);
            const auto &ac = accountstable.get(sym_code.raw());
            return ac.balance;
        }

        using create_action = eosio::action_wrapper<"create"_n, &token::create>;
        using issue_action = eosio::action_wrapper<"issue"_n, &token::issue>;
        using mintfio_action = eosio::action_wrapper<"mintfio"_n, &token::mintfio>;
        using retire_action = eosio::action_wrapper<"retire"_n, &token::retire>;
        using transfer_action = eosio::action_wrapper<"transfer"_n, &token::transfer>;

    private:
        struct [[eosio::table]] account {
            asset balance;

            uint64_t primary_key() const { return balance.symbol.code().raw(); }
        };

        struct [[eosio::table]] currency_stats {
            asset supply;
            asset max_supply;
            name issuer = SYSTEMACCOUNT;
            uint64_t primary_key() const { return supply.symbol.code().raw(); }
        };

        typedef eosio::multi_index<"accounts"_n, account> accounts;
        typedef eosio::multi_index<"stat"_n, currency_stats> stats;

        void sub_balance(name owner, asset value);
        void add_balance(name owner, asset value, name ram_payer);
        bool can_transfer(const name &tokenowner,const uint64_t &feeamount,const uint64_t &transferamount, const bool &isfee);

    public:

        struct transfer_args {
            name from;
            name to;
            asset quantity;
            string memo;
        };

        struct bind2eosio {
            name accountName;
            string public_key;
            bool existing;
        };


        static constexpr symbol FIOSYMBOL = symbol("FIO", 9);
        static constexpr name FIOISSUER = name("eosio"_n);


        //this will compute the present unlocked tokens for this user based on the
        //unlocking schedule, it will update the lockedtokens table if the doupdate
        //is set to true.
        static uint64_t computeremaininglockedtokens(const name &actor, bool doupdate){
            uint32_t present_time = now();

            print(" unlock_tokens for ",actor,"\n");

            eosiosystem::locked_tokens_table lockedTokensTable(SYSTEMACCOUNT, SYSTEMACCOUNT.value);
            auto lockiter = lockedTokensTable.find(actor.value);
            if(lockiter != lockedTokensTable.end()){
                if(lockiter->inhibit_unlocking && (lockiter->grant_type == 2)){
                    return lockiter->remaining_locked_amount;
                }
                if (lockiter->unlocked_period_count < 9)  {
                    print(" issue time is ",lockiter->timestamp,"\n");
                    print(" present time - issue time is ",(present_time  - lockiter->timestamp),"\n");
                    // uint32_t timeElapsed90DayBlocks = (int)((present_time  - lockiter->timestamp) / SECONDSPERDAY) / 90;
                    //we kludge the time block evaluation to become one block per 3 minutes
                    uint32_t timeElapsed90DayBlocks = (int)((present_time  - lockiter->timestamp) / 120) / 1;
                    print("--------------------DANGER------------------------------ ","\n");
                    print("--------------------DANGER------------------------------ ","\n");
                    print("--------------------DANGER------------------------------ ","\n");
                    print("------time step for unlocking is kludged to 2 min-------","\n");
                    print("--------------------DANGER------------------------------ ","\n");
                    print("--------------------DANGER------------------------------ ","\n");
                    print(" timeElapsed90DayBlocks ",timeElapsed90DayBlocks,"\n");
                    uint32_t numberVestingPayouts = lockiter->unlocked_period_count;
                    print(" number payouts so far ",numberVestingPayouts,"\n");
                    uint32_t remainingPayouts = 0;

                    uint64_t newlockedamount = lockiter->remaining_locked_amount;
                    print(" locked amount ",newlockedamount,"\n");

                    uint64_t totalgrantamount = lockiter->total_grant_amount;
                    print(" total grant amount ",totalgrantamount,"\n");

                    uint64_t amountpay = 0;

                    uint64_t addone = 0;

                    if (timeElapsed90DayBlocks > 8){
                        timeElapsed90DayBlocks = 8;
                    }

                    bool didsomething = false;

                    //do the day zero unlocking, this is the first unlocking.
                    if(numberVestingPayouts == 0) {
                        if (lockiter->grant_type == 1) {
                            //pay out 1% for type 1
                            amountpay = totalgrantamount / 100;
                            print(" amount to pay type 1 ",amountpay,"\n");

                        } else if (lockiter->grant_type == 2) {
                            //pay out 2% for type 2
                            amountpay = (totalgrantamount/100)*2;
                            print(" amount to pay type 2 ",amountpay,"\n");
                        }else{
                            check(false,"unknown grant type");
                        }
                        if (newlockedamount > amountpay) {
                            newlockedamount -= amountpay;
                        }else {
                            newlockedamount = 0;
                        }
                        print(" recomputed locked amount ",newlockedamount,"\n");
                        addone = 1;
                        didsomething = true;
                    }

                    //this accounts for the first unlocking period being the day 0 unlocking period.
                    if (numberVestingPayouts >0){
                        numberVestingPayouts--;
                    }

                    if (timeElapsed90DayBlocks > numberVestingPayouts) {
                        remainingPayouts = timeElapsed90DayBlocks - numberVestingPayouts;
                        uint64_t percentperblock = 0;
                        if (lockiter->grant_type == 1) {
                            //this logic assumes to have 3 decimal places in the specified percentage
                            percentperblock = 12375;
                        } else {
                            //this is assumed to have 3 decimal places in the specified percentage
                            percentperblock = 12275;
                        }
                        print("remaining payouts ", remainingPayouts, "\n");
                        //this is assumed to have 3 decimal places in the specified percentage
                        amountpay = (remainingPayouts * (totalgrantamount * percentperblock)) / 100000;
                        print(" amount to pay ", amountpay, "\n");

                        if (newlockedamount > amountpay) {
                            newlockedamount -= amountpay;
                        } else {
                            newlockedamount = 0;
                        }
                        print(" recomputed locked amount ", newlockedamount, "\n");
                        didsomething = true;
                    }

                    if(didsomething && doupdate) {
                        print(" updating recomputed locked amount into table ", newlockedamount, "\n");
                        //get fio balance for this account,
                        uint32_t present_time = now();
                        symbol sym_name = symbol("FIO", 9);
                        const auto my_balance = eosio::token::get_balance("fio.token"_n,actor, sym_name.code() );
                        uint64_t amount = my_balance.amount;

                        if (newlockedamount > amount){
                            print(" WARNING computed amount ",newlockedamount," is more than amount in account ",amount," \n ",
                                    " Transaction processing order can cause this, this amount is being re-aligned, resetting remaining locked amount to ", amount, "\n");
                            newlockedamount = amount;
                        }
                        //update the locked table.
                        lockedTokensTable.modify(lockiter, SYSTEMACCOUNT, [&](auto &av) {
                            av.remaining_locked_amount = newlockedamount;
                            av.unlocked_period_count += remainingPayouts + addone;
                        });
                    }else {
                        print(" NOT updating recomputed locked amount into table ", newlockedamount, "\n");
                    }

                    return newlockedamount;

                }else{
                    return lockiter->remaining_locked_amount;
                }
            }
            return 0;
        }

    };
} /// namespace eosio
