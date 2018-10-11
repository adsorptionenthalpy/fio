/** Fio Finance implementation file
 *  Description: FioFinance smart contract supports funds request and approval.
 *  @author Ciju John
 *  @file fio.finance.cpp
 *  @copyright Dapix
 *
 *  Changes:
 */

#include <eosiolib/asset.hpp>
#include "fio.finance.hpp"

#include <climits>
#include <sstream>

namespace fioio{


    class FioFinance : public contract {
    private:
        configs config;
        statecontainer contract_state;
        transaction_contexts_table transaction_contexts;
        transaction_logs_table transaction_logs;
        pending_requests_table pending_requests;
        processed_requests_table processed_requests;

        // data format of transactions, tail numeral indicates number of operands
        string trx_type_dta_NO_REQ_RPRT=  R"({"obtid":"%s"})";                  // ${trx_type::NO_REQ_RPRT}: obtid
        string trx_type_dta_REQ=          R"({"reqid":"%lld","memo":"%s"})";    // ${trx_type::REQ}: requestid, memo
        string trx_type_dta_REQ_CANCEL=   R"({"memo":"%s"})";                   // ${trx_type::REQ_CANCEL}
        string trx_type_dta_REQ_RPRT=     R"({"obtid":"%s","memo":"%s"})";      // ${trx_type::REQ_RPRT}: obtid, memo
        string trx_type_dta_RCPT_VRFY=    R"({"memo":"%s"})";                   // ${trx_type::RCPT_VRFY}
        string trx_type_dta_REQ_REJECT=   R"({"memo":"%s"})";                   // ${trx_type::REQ_REJECT}

        // User printable supported chains strings.
        const std::vector<std::string> chain_str {
                "FIO",
                "EOS",
                "BTC",
                "ETH",
                "XMR",
                "BRD",
                "BCH"
        };


    public:
        explicit FioFinance(account_name self)
                : contract(self), config(self, self), contract_state(self,self), transaction_contexts(self, self),
                transaction_logs(self,self), pending_requests(self, self), processed_requests(self,self)
        { }

        /***
         * Convert chain name to chain type.
         *
         * @param chain The chain name e.g. "BTC"
         * @return chain_type::NONE if no match.
         */
        inline chain_type str_to_chain_type(const string &chain) {

            print("size: ", chain_str.size(), "\n");
            for (size_t i = 0; i < chain_str.size(); i++) {
                print("..chain: ", chain, ", chain_str: ", chain_str[i]);
                if (chain == chain_str[i]) {
                    print("..Found supported chain.");
                    return static_cast<chain_type>(i);
                }
            }
            return chain_type::NONE;
        }

        /***
         * Validate chain is in the supported chains list.
         * @param chain The chain to validate, expected to be in lower case.s
         */
        inline void assert_valid_chain(const string &chain) {
            assert(str_to_chain_type(chain) != chain_type::NONE);
        }

        // generic function to construct Fio log data string.
        inline static std::string trxlogformat(const std::string format, ...)
        {
            va_list args;
            va_start (args, format);
            size_t len = static_cast<size_t>(std::vsnprintf(NULL, 0, format.c_str(), args));
            va_end (args);
            std::vector<char> vec(len + 1);
            va_start (args, format);
            std::vsnprintf(&vec[0], len + 1, format.c_str(), args);
            va_end (args);
            return &vec[0];
        }

        /***
         * Requestor initiates funds request.
         */
        // @abi action
        void requestfunds(uint64_t requestid, const name& requestor, const name& requestee, const string &chain,
                const string& asset, const string quantity, const string &memo) {
            print("..Validating authority ", requestor);
            require_auth(requestor); // we need requesters authorization
            is_account(requestee); // ensure requestee exists

            // validate chain is supported. This is a case insensitive check.
            string my_chain = chain;
            transform(my_chain.begin(), my_chain.end(), my_chain.begin(), ::toupper);
            print("..Validating chain support: ", my_chain);
            assert_valid_chain(my_chain);

            // Validate requestid is unique for this user
            print("..Validating requestid uniqueness");
            auto idx = pending_requests.get_index<N(byrequestid)>();
            auto matchingItem = idx.lower_bound(requestid);

            // Advance to the first entry matching the specified requestid and from
            while (matchingItem != idx.end() && matchingItem->requestid == requestid &&
                   matchingItem->originator != requestor) {
                matchingItem++;
            }

            // assert identical request from same user doesn't exist
            assert(matchingItem == idx.end() ||
                   !(matchingItem->requestid == requestid && matchingItem->originator == requestor));

//            print("Validate quantity is double.");
//            // TBD: Below API is not linking. Research and fix
////            std::stod(quantity);
//            double myquant;
//            std::stringstream iss (quantity);
//            iss >> myquant;
//            print("Converted quantity: ", myquant);

            // get the current FIO app id from contract state
            auto currentState = contract_state.get_or_default(contr_state());
            currentState.current_fioappid++; // increment the fioappid

            print("..Adding transaction context record");
            transaction_contexts.emplace(_self, [&](struct trxcontext &ctx) {
               ctx.fioappid = currentState.current_fioappid;
               ctx.originator = requestor;
               ctx.receiver = requestee;
               ctx.chain = chain;
               ctx.asset = asset;
               ctx.quantity = quantity;
            });

            print("..Adding transaction log");
            transaction_logs.emplace(_self, [&](struct trxlog &log) {
                log.key = transaction_logs.available_primary_key();
                print("..Log key: ", log.key);
                log.fioappid = currentState.current_fioappid;
                log.type = static_cast<uint16_t>(trx_type::REQ);
                log.status = static_cast<uint16_t>(trx_sts::REQ);
                log.time = now();

                print("..Log requestid: ", requestid, ", memo: ", memo);
                string data = FioFinance::trxlogformat(trx_type_dta_REQ, requestid, memo.c_str());
                log.data=data;
            });

            print("..Adding pending request id: ", requestid);
            // Add fioname entry in fionames table
            pending_requests.emplace(_self, [&](struct fundsrequest &fr) {
                fr.requestid = requestid;
                fr.fioappid = currentState.current_fioappid;
                fr.originator = requestor;
                fr.receiver = requestee;
            });

            // Persist contract state
            contract_state.set(currentState, _self);
        }

        /***
         * Requestor cancel pending funds request
         */
        // @abi action
        void cancelrqst(uint64_t requestid, const name& requestor, const string memo) {
            print("..Validating authority ", requestor);
            require_auth(requestor); // we need requesters authorization

            print("..Validating pending request exists. Request id: ", requestid);
            // validate a pending request exists for this requester with this requestid
            auto idx = pending_requests.get_index<N(byrequestid)>();
            auto matchingItem = idx.lower_bound(requestid);

            // Advance to the first entry matching the specified vID
            while (matchingItem != idx.end() && matchingItem->requestid == requestid &&
                   matchingItem->originator != requestor) {
                matchingItem++;
            }

            // assert on match found
            assert(matchingItem != idx.end() && matchingItem->requestid == requestid &&
                   matchingItem->originator == requestor);

            print("..Adding transaction log");
            transaction_logs.emplace(_self, [&](struct trxlog &log) {
                log.key = transaction_logs.available_primary_key();
                log.fioappid = matchingItem->fioappid;
                log.type = static_cast<uint16_t>(trx_type::REQ_CANCEL);
                log.status = static_cast<uint16_t>(trx_sts::REQ_CANCEL);
                log.time = now();

                string data = FioFinance::trxlogformat(trx_type_dta_REQ_CANCEL, memo.c_str());
                log.data=data;
            });

            print("..Moving request to processed requests table.");
            processed_requests.emplace(_self, [&](struct fundsrequest &req) {
                req=*matchingItem;
            });

            // drop pending request
            print("..Removing pending request: ", matchingItem->requestid);
            idx.erase(matchingItem);
        }

        /***
         * Requestee reports(approve) pending funds request
         */
        // @abi action
        void reportrqst(const uint64_t fioappid, const name& requestee, const string& obtid, const string memo) {
            print("..Validating authority ", requestee);
            require_auth(requestee); // we need requesters authorization

            print("..Validating pending request exists. Fio app id: ", fioappid);
            // validate request fioappid exists and is for this requestee
            auto matchingItem = pending_requests.lower_bound(fioappid);
            assert(matchingItem != pending_requests.end());
            assert(matchingItem->receiver == requestee);

            print("..Adding report(approve) request transaction log");
            transaction_logs.emplace(_self, [&](struct trxlog &log) {
                log.key = transaction_logs.available_primary_key();
                log.fioappid = matchingItem->fioappid;
                log.type = static_cast<uint16_t>(trx_type::REQ_RPRT);
                log.status = static_cast<uint16_t>(trx_sts::RPRT);
                log.time = now();

                string data = FioFinance::trxlogformat(trx_type_dta_REQ_RPRT, obtid.c_str(), memo.c_str());
                log.data=data;
            });

            print("..Moving request to processed requests table.");
            processed_requests.emplace(_self, [&](struct fundsrequest &req) {
                req=*matchingItem;
            });

            // drop pending request
            print("..Removing pending request: ", matchingItem->requestid);
            pending_requests.erase(matchingItem);
        }

        /***
         * Requestee reject pending funds request
         */
        // @abi action
        void rejectrqst(uint64_t fioappid, const name& requestee, const string& memo) {
            print("..Validating authority ", requestee);
            require_auth(requestee); // we need requesters authorization

            print("..Validating pending request exists. Fio app id: ", fioappid);
            // validate request fioappid exists and is for this requestee
            auto matchingItem = pending_requests.lower_bound(fioappid);
            assert(matchingItem != pending_requests.end());
            assert(matchingItem->receiver == requestee);

            print("..Adding reject request transaction log");
            transaction_logs.emplace(_self, [&](struct trxlog &log) {
                log.key = transaction_logs.available_primary_key();
                log.fioappid = matchingItem->fioappid;
                log.type = static_cast<uint16_t>(trx_type::REQ_REJECT);
                log.status = static_cast<uint16_t>(trx_sts::REQ_REJECT);
                log.time = now();

                string data = FioFinance::trxlogformat(trx_type_dta_REQ_REJECT, memo.c_str());
                log.data=data;
            });

            print("..Moving request to processed requests table.");
            processed_requests.emplace(_self, [&](struct fundsrequest &req) {
                req=*matchingItem;
            });

            // drop pending request
            print("..Removing pending request: ", matchingItem->requestid);
            pending_requests.erase(matchingItem);
        }


    }; // class FioFinance


    EOSIO_ABI( FioFinance, (requestfunds)(cancelrqst)(reportrqst)(rejectrqst) )
}
