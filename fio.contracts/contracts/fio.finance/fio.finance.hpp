/** Fio Finance header file
 *  Description: FioFinance smart contract supports funds request and approval.
 *  @author Ciju John
 *  @file fio.finance.hpp
 *  @copyright Dapix
 *
 *  Changes:
 */


#pragma once

#include <eosiolib/eosio.hpp>
#include <string>
#include <eosiolib/singleton.hpp>
#include <eosiolib/asset.hpp>

using std::string;

namespace fioio {

    using namespace eosio;

    // Transaction types
    // Very specific and explicit on transaction details.
    // Transaction version will be handled by defining new transaction type.
    enum class trx_type {
        NO_REQ_RPRT=    0,  // report without request
        REQ=            1,  // funds request
        REQ_CANCEL=     2,  // cancel funds request
        REQ_RPRT=       3,  // request fulfilment report
        RCPT_VRFY=      4,  // recipient verification
        REQ_REJECT=     5   // reject request
    };

    // Transaction status types.
    // Even though this is similar to transaction type its more generic and can be carried across transaction versions.
    // Does not specify transaction details.
    enum class trx_sts {
        RPRT=           0,  // report
        REQ=            1,  // funds request
        REQ_CANCEL=     2,  // cancel funds request
        RCPT_VRFY=      3,   // recipient verification
        REQ_REJECT=      4
    };

    // Structure for "FIO transaction chain" context.
    // A single object per FIO transaction chain.
    // FIO transaction chain is a series of linked individual blockchain transactions.
    // @abi table trxcontexts i64
    struct [[eosio::action]] trxcontext { // TBD transaction context
        uint64_t    fioappid;   // unique id representing a chain of linked blockchain transactions, generated by the FIO app
        name        originator; // funds originator
        name        receiver;   // funds receiver
        string      chain;      // chain_type enumeration representing supported chain
        string      asset;      // asset being transferred
        string      quantity;   // asset quantity being transferred

        uint64_t primary_key() const    { return fioappid; }

        uint64_t by_originator() const { return originator.value; }

        uint64_t by_receiver() const { return receiver.value; }
        EOSLIB_SERIALIZE(trxcontext, (fioappid)(originator)(receiver)(chain)(asset)(quantity))
    };
    // transaction contexts table
    typedef multi_index<"trxcontexts"_n, trxcontext,
            indexed_by<"byoriginator"_n, const_mem_fun < trxcontext, uint64_t, &trxcontext::by_originator> >,
    indexed_by<"byreceiver"_n, const_mem_fun<trxcontext, uint64_t, &trxcontext::by_receiver> >
    > transaction_contexts_table;

    // Structure for FIO transaction chain updates
    // Chain of objects representing audit trail of a FIO transaction chain
    // @abi table trxlogs i64
    struct [[eosio::action]] trxlog {
        uint64_t key;       // unique index
        uint64_t fioappid;  // key to trxcontext table
        uint16_t type;      // log message of type ${trx_type}
        uint16_t status;    // status message of type ${trx_sts}
        time time;          // transaction received (by blockchain) time
        string data;        // data binding specific to transaction type. Defined in ${FioFinance::trx_type_dta_*} variables

        uint64_t primary_key() const    { return key; }
        uint64_t by_fioappid() const    { return fioappid; }
        EOSLIB_SERIALIZE(trxlog, (key)(fioappid)(type)(status)(time)(data))
    };
    // transactions log table
    typedef multi_index<"trxlogs"_n, trxlog,
            indexed_by<"byfioappid"_n, const_mem_fun < trxlog, uint64_t, &trxlog::by_fioappid> > >
    transaction_logs_table;

    // Structure for FIO funds request
    // This is a temporary structure for holding pending requests. Once request is reported it will be erased
    // @abi table pendrqsts i64
    // @abi table prsrqsts i64
    struct [[eosio::action]] fundsrequest {
        uint64_t    requestid;  // user supplied request id, mainly for user to track requests
        uint64_t    fioappid;   // key to trxcontext table
        name        originator; // funds originator
        name        receiver;   // funds receiver

        uint64_t primary_key() const    { return fioappid; }
        uint64_t by_requestid() const    { return requestid; }

        uint64_t by_originator() const { return originator.value; }

        uint64_t by_receiver() const { return receiver.value; }
        EOSLIB_SERIALIZE(fundsrequest, (requestid)(fioappid)(originator)(receiver))
    };
    // funds requests table
    typedef multi_index<"pendrqsts"_n, fundsrequest,
            indexed_by<"byrequestid"_n, const_mem_fun < fundsrequest, uint64_t, &fundsrequest::by_requestid> >,
    indexed_by<"byoriginator"_n, const_mem_fun<fundsrequest, uint64_t, &fundsrequest::by_originator> >,
    indexed_by<"byreceiver"_n, const_mem_fun<fundsrequest, uint64_t, &fundsrequest::by_receiver> >
    > pending_requests_table;

    // processed funds requests table
    typedef multi_index<"prsrqsts"_n, fundsrequest,
            indexed_by<"byrequestid"_n, const_mem_fun < fundsrequest, uint64_t, &fundsrequest::by_requestid> >,
    indexed_by<"byoriginator"_n, const_mem_fun<fundsrequest, uint64_t, &fundsrequest::by_originator> >,
    indexed_by<"byreceiver"_n, const_mem_fun<fundsrequest, uint64_t, &fundsrequest::by_receiver> >
    > processed_requests_table;

    struct contr_state {
        uint64_t current_fioappid = 0; // obt generator

        EOSLIB_SERIALIZE(contr_state, (current_fioappid))
    };
    typedef singleton<"state"_n, contr_state> statecontainer;
}
