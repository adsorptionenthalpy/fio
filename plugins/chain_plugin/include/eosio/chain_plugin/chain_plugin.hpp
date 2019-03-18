/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#pragma once

#include <appbase/application.hpp>
#include <eosio/chain/asset.hpp>
#include <eosio/chain/authority.hpp>
#include <eosio/chain/account_object.hpp>
#include <eosio/chain/block.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/contract_table_objects.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/transaction.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/plugin_interface.hpp>
#include <eosio/chain/types.hpp>

#include <boost/container/flat_set.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <fc/static_variant.hpp>

namespace fc { class variant; }

namespace eosio {
    using chain::controller;
    using std::unique_ptr;
    using std::pair;
    using namespace appbase;
    using chain::name;
    using chain::uint128_t;
    using chain::public_key_type;
    using chain::transaction;
    using chain::transaction_id_type;
    using fc::optional;
    using boost::container::flat_set;
    using chain::asset;
    using chain::symbol;
    using chain::authority;
    using chain::account_name;
    using chain::action_name;
    using chain::abi_def;
    using chain::abi_serializer;

    namespace chain_apis {

        struct empty {
        };

        struct permission {
            name perm_name;
            name parent;
            authority required_auth;
        };

        struct fiodomain_record {

            string fio_domain;
            string expiration;

        };

        struct fioaddress_record {
            string fio_address;
            string expiration;

        };

        struct request_record {
            uint64_t fioreqid;     // one up index starting at 0
            uint64_t fromfioadd;   // sender FIO address e.g. john.xyz
            uint64_t tofioadd;     // receiver FIO address e.g. jane.xyz
            string topubadd;       // chain specific receiver public address e.g 0xC8a5bA5868A5E9849962167B2F99B2040Cee2031
            string amount;         // token quantity
            string tokencode;      // token type e.g. BLU
            string metadata;       // JSON formatted meta data e.g. {"memo":"utility payment"}
            uint64_t timestamp;    // FIO blockchain request received timestamp
        };

        template<typename>
        struct resolver_factory;

        // see specialization for uint64_t in source file
        template<typename Type>
        Type convert_to_type(const string &str, const string &desc) {
            try {
                return fc::variant(str).as<Type>();
            } FC_RETHROW_EXCEPTIONS(warn, "Could not convert ${desc} string '${str}' to key type.",
                                    ("desc", desc)("str", str))
        }

        template<>
        uint64_t convert_to_type(const string &str, const string &desc);

        struct fio_config_parameters {
            string proxy_account = "fio.system";
            string proxy_key = "5KBX1dwHME4VyuUss2sYM25D5ZTDvyYrbEz37UJqwAVAsR4tGuY";
            uint64_t proxy_name = N(fio.system);
        };

        class read_only {
            const controller &db;
            const fc::microseconds abi_serializer_max_time;
            bool shorten_abi_errors = true;

        public:
            static const string KEYi64;

            read_only(const controller &db, const fc::microseconds &abi_serializer_max_time)
                    : db(db), abi_serializer_max_time(abi_serializer_max_time) {}

            void validate() const {}

            void set_shorten_abi_errors(bool f) { shorten_abi_errors = f; }

            using get_info_params = empty;

            struct get_info_results {
                string server_version;
                chain::chain_id_type chain_id;
                uint32_t head_block_num = 0;
                uint32_t last_irreversible_block_num = 0;
                chain::block_id_type last_irreversible_block_id;
                chain::block_id_type head_block_id;
                fc::time_point head_block_time;
                account_name head_block_producer;

                uint64_t virtual_block_cpu_limit = 0;
                uint64_t virtual_block_net_limit = 0;

                uint64_t block_cpu_limit = 0;
                uint64_t block_net_limit = 0;
                //string recent_slots;
                //double participation_rate = 0;
                optional<string> server_version_string;
            };

            get_info_results get_info(const get_info_params &) const;

            struct producer_info {
                name producer_name;
            };

            using account_resource_limit = chain::resource_limits::account_resource_limit;

            struct get_account_results {
                name account_name;
                uint32_t head_block_num = 0;
                fc::time_point head_block_time;

                bool privileged = false;
                fc::time_point last_code_update;
                fc::time_point created;

                optional<asset> core_liquid_balance;

                int64_t ram_quota = 0;
                int64_t net_weight = 0;
                int64_t cpu_weight = 0;

                account_resource_limit net_limit;
                account_resource_limit cpu_limit;
                int64_t ram_usage = 0;

                vector<permission> permissions;

                fc::variant total_resources;
                fc::variant self_delegated_bandwidth;
                fc::variant refund_request;
                fc::variant voter_info;
            };

            struct get_account_params {
                name account_name;
                optional<symbol> expected_core_symbol;
            };

            get_account_results get_account(const get_account_params &params) const;


            struct get_code_results {
                name account_name;
                string wast;
                string wasm;
                fc::sha256 code_hash;
                optional<abi_def> abi;
            };

            struct get_code_params {
                name account_name;
                bool code_as_wasm = false;
            };

            struct get_code_hash_results {
                name account_name;
                fc::sha256 code_hash;
            };

            struct get_code_hash_params {
                name account_name;
            };

            struct get_abi_results {
                name account_name;
                optional<abi_def> abi;
            };

            struct get_abi_params {
                name account_name;
            };

            struct get_raw_code_and_abi_results {
                name account_name;
                chain::blob wasm;
                chain::blob abi;
            };

            struct get_raw_code_and_abi_params {
                name account_name;
            };

            struct get_raw_abi_params {
                name account_name;
                optional<fc::sha256> abi_hash;
            };

            struct get_raw_abi_results {
                name account_name;
                fc::sha256 code_hash;
                fc::sha256 abi_hash;
                optional<chain::blob> abi;
            };


            get_code_results get_code(const get_code_params &params) const;

            get_code_hash_results get_code_hash(const get_code_hash_params &params) const;

            get_abi_results get_abi(const get_abi_params &params) const;

            get_raw_code_and_abi_results get_raw_code_and_abi(const get_raw_code_and_abi_params &params) const;

            get_raw_abi_results get_raw_abi(const get_raw_abi_params &params) const;


            struct abi_json_to_bin_params {
                name code;
                name action;
                fc::variant args;
            };
            struct abi_json_to_bin_result {
                vector<char> binargs;
            };

            abi_json_to_bin_result abi_json_to_bin(const abi_json_to_bin_params &params) const;


            struct abi_bin_to_json_params {
                name code;
                name action;
                vector<char> binargs;
            };
            struct abi_bin_to_json_result {
                fc::variant args;
            };

            abi_bin_to_json_result abi_bin_to_json(const abi_bin_to_json_params &params) const;

            struct serialize_json_params {

                name action;
                fc::variant json;
            };
            struct serialize_json_result {
                vector<char> serialized_json;
            };

            serialize_json_result serialize_json(const serialize_json_params &params) const;

            struct get_required_keys_params {
                fc::variant transaction;
                flat_set<public_key_type> available_keys;
            };
            struct get_required_keys_result {
                flat_set<public_key_type> required_keys;
            };

            get_required_keys_result get_required_keys(const get_required_keys_params &params) const;

            using get_transaction_id_params = transaction;
            using get_transaction_id_result = transaction_id_type;

            get_transaction_id_result get_transaction_id(const get_transaction_id_params &params) const;

            struct get_block_params {
                string block_num_or_id;
            };

            fc::variant get_block(const get_block_params &params) const;

            struct get_block_header_state_params {
                string block_num_or_id;
            };

            fc::variant get_block_header_state(const get_block_header_state_params &params) const;

            struct get_table_rows_params {
                bool json = false;
                name code;
                string scope;
                name table;
                string table_key;
                string lower_bound;
                string upper_bound;
                uint32_t limit = 10;
                string key_type;  // type of key specified by index_position
                string index_position; // 1 - primary (first), 2 - secondary index (in order defined by multi_index), 3 - third index, etc
                string encode_type{"dec"}; //dec, hex , default=dec
            };

            struct get_table_rows_result {
                vector<fc::variant> rows; ///< one row per item, either encoded as hex String or JSON object
                bool more = false; ///< true if last element in data is not the end and sizeof data() < limit
            };

            get_table_rows_result get_table_rows(const get_table_rows_params &params) const;

            ////////////////
            // FIO COMMON //

            //begin get pending fio requests
            struct get_pending_fio_requests_params {
                string fiopubadd;  // FIO public address to find requests for..
            };

            struct get_pending_fio_requests_result {
                vector<request_record> requests;
            };

            get_pending_fio_requests_result
            get_pending_fio_requests(const get_pending_fio_requests_params &params) const;

            //end get pending fio requests
            //begin get sent fio requests
            struct get_sent_fio_requests_params {
                string fiopubadd;  // FIO public address to find requests for..
            };

            struct get_sent_fio_requests_result {
                vector<request_record> requests;
            };

            get_sent_fio_requests_result
            get_sent_fio_requests(const get_sent_fio_requests_params &params) const;
            //end get sent fio requests

            struct get_fio_names_params {
                string fio_pub_address;
            };
            struct get_fio_names_result {
                string fio_pub_address;
                vector<fiodomain_record> fio_domains;
                vector<fioaddress_record> fio_addresses;
            };

            struct pub_address_lookup_params {
                fc::string fio_address;
                fc::string token_code;
            };

            struct pub_address_lookup_result {
                fc::string fio_address;
                fc::string token_code;
                fc::string pub_address;
            };

            pub_address_lookup_result pub_address_lookup(const pub_address_lookup_params &params) const;

            /**
             * Lookup FIO domains and addresses based upon public address
             * @param params
             * @return
             */
            get_fio_names_result get_fio_names(const get_fio_names_params &params) const;


            //avail_check - FIO Address or Domain availability check
            struct avail_check_params {
                string fio_name;
            };

            struct avail_check_result {
                string fio_name = "";
                bool is_registered = false;
            };

            avail_check_result avail_check(const avail_check_params &params) const;

            //key lookups
            struct fio_key_lookup_params {
                string key;     // chain key e.g. for Ethereum: 0xC2D7CF95645D33006175B78989035C7c9061d3F9
                string chain;   // chain name e.g. BTC, ETH, EOS etc.
            };
            struct fio_key_lookup_result {
                string name = "";   // FIO name
                string expiration = "";
            };

            /**
             * Lookup FIO name based upon blockchain key(address|account)
             * @param params
             * @return
             */
            fio_key_lookup_result fio_key_lookup(const fio_key_lookup_params &params) const;

            struct get_table_by_scope_params {
                name code; // mandatory
                name table = 0; // optional, act as filter
                string lower_bound; // lower bound of scope, optional
                string upper_bound; // upper bound of scope, optional
                uint32_t limit = 10;
            };
            struct get_table_by_scope_result_row {
                name code;
                name scope;
                name table;
                name payer;
                uint32_t count;
            };
            struct get_table_by_scope_result {
                vector<get_table_by_scope_result_row> rows;
                string more; ///< fill lower_bound with this value to fetch more rows
            };

            get_table_by_scope_result get_table_by_scope(const get_table_by_scope_params &params) const;

            struct get_currency_balance_params {
                name code;
                name account;
                optional<string> symbol;
            };

            vector<asset> get_currency_balance(const get_currency_balance_params &params) const;

            struct get_currency_stats_params {
                name code;
                string symbol;
            };


            struct get_currency_stats_result {
                asset supply;
                asset max_supply;
                account_name issuer;
            };

            fc::variant get_currency_stats(const get_currency_stats_params &params) const;

            struct get_producers_params {
                bool json = false;
                string lower_bound;
                uint32_t limit = 50;
            };

            struct get_producers_result {
                vector<fc::variant> rows; ///< one row per item, either encoded as hex string or JSON object
                double total_producer_vote_weight;
                string more; ///< fill lower_bound with this value to fetch more rows
            };

            get_producers_result get_producers(const get_producers_params &params) const;

            struct get_producer_schedule_params {
            };

            struct get_producer_schedule_result {
                fc::variant active;
                fc::variant pending;
                fc::variant proposed;
            };

            get_producer_schedule_result get_producer_schedule(const get_producer_schedule_params &params) const;

            struct get_scheduled_transactions_params {
                bool json = false;
                string lower_bound;  /// timestamp OR transaction ID
                uint32_t limit = 50;
            };

            struct get_scheduled_transactions_result {
                fc::variants transactions;
                string more; ///< fill lower_bound with this to fetch next set of transactions
            };

            get_scheduled_transactions_result
            get_scheduled_transactions(const get_scheduled_transactions_params &params) const;

            static void copy_inline_row(const chain::key_value_object &obj, vector<char> &data) {
                data.resize(obj.value.size());
                memcpy(data.data(), obj.value.data(), obj.value.size());
            }

            template<typename Function>
            void walk_key_value_table(const name &code, const name &scope, const name &table, Function f) const {
                const auto &d = db.db();
                const auto *t_id = d.find<chain::table_id_object, chain::by_code_scope_table>(
                        boost::make_tuple(code, scope, table));
                if (t_id != nullptr) {
                    const auto &idx = d.get_index<chain::key_value_index, chain::by_scope_primary>();
                    decltype(t_id->id) next_tid(t_id->id._id + 1);
                    auto lower = idx.lower_bound(boost::make_tuple(t_id->id));
                    auto upper = idx.lower_bound(boost::make_tuple(next_tid));

                    for (auto itr = lower; itr != upper; ++itr) {
                        if (!f(*itr)) {
                            break;
                        }
                    }
                }
            }

            static uint64_t get_table_index_name(const read_only::get_table_rows_params &p, bool &primary);

            template<typename IndexType, typename SecKeyType, typename ConvFn>
            read_only::get_table_rows_result
            get_table_rows_by_seckey(const read_only::get_table_rows_params &p, const abi_def &abi, ConvFn conv) const {
                read_only::get_table_rows_result result;
                const auto &d = db.db();

                uint64_t scope = convert_to_type<uint64_t>(p.scope, "scope");

                abi_serializer abis;
                abis.set_abi(abi, abi_serializer_max_time);
                bool primary = false;
                const uint64_t table_with_index = get_table_index_name(p, primary);
                const auto *t_id = d.find<chain::table_id_object, chain::by_code_scope_table>(
                        boost::make_tuple(p.code, scope, p.table));
                const auto *index_t_id = d.find<chain::table_id_object, chain::by_code_scope_table>(
                        boost::make_tuple(p.code, scope, table_with_index));
                if (t_id != nullptr && index_t_id != nullptr) {
                    const auto &secidx = d.get_index<IndexType, chain::by_secondary>();
                    decltype(index_t_id->id) low_tid(index_t_id->id._id);
                    decltype(index_t_id->id) next_tid(index_t_id->id._id + 1);
                    auto lower = secidx.lower_bound(boost::make_tuple(low_tid));
                    auto upper = secidx.lower_bound(boost::make_tuple(next_tid));

                    if (p.lower_bound.size()) {
                        if (p.key_type == "name") {
                            name s(p.lower_bound);
                            SecKeyType lv = convert_to_type<SecKeyType>(s.to_string(),
                                                                        "lower_bound name"); // avoids compiler error
                            lower = secidx.lower_bound(boost::make_tuple(low_tid, conv(lv)));
                        } else {
                            SecKeyType lv = convert_to_type<SecKeyType>(p.lower_bound, "lower_bound");
                            lower = secidx.lower_bound(boost::make_tuple(low_tid, conv(lv)));
                        }
                    }
                    if (p.upper_bound.size()) {
                        if (p.key_type == "name") {
                            name s(p.upper_bound);
                            SecKeyType uv = convert_to_type<SecKeyType>(s.to_string(), "upper_bound name");
                            upper = secidx.lower_bound(boost::make_tuple(low_tid, conv(uv)));
                        } else {
                            SecKeyType uv = convert_to_type<SecKeyType>(p.upper_bound, "upper_bound");
                            upper = secidx.lower_bound(boost::make_tuple(low_tid, conv(uv)));
                        }
                    }

                    vector<char> data;

                    auto end = fc::time_point::now() + fc::microseconds(1000 * 10); /// 10ms max time

                    unsigned int count = 0;
                    auto itr = lower;
                    for (; itr != upper; ++itr) {

                        const auto *itr2 = d.find<chain::key_value_object, chain::by_scope_primary>(
                                boost::make_tuple(t_id->id, itr->primary_key));
                        if (itr2 == nullptr) continue;
                        copy_inline_row(*itr2, data);

                        if (p.json) {
                            result.rows.emplace_back(
                                    abis.binary_to_variant(abis.get_table_type(p.table), data, abi_serializer_max_time,
                                                           shorten_abi_errors));
                        } else {
                            result.rows.emplace_back(fc::variant(data));
                        }

                        if (++count == p.limit || fc::time_point::now() > end) {
                            break;
                        }
                    }
                    if (itr != upper) {
                        result.more = true;
                    }
                }
                return result;
            }

            template<typename IndexType>
            read_only::get_table_rows_result
            get_table_rows_ex(const read_only::get_table_rows_params &p, const abi_def &abi) const {
                read_only::get_table_rows_result result;
                const auto &d = db.db();

                uint64_t scope = convert_to_type<uint64_t>(p.scope, "scope");

                abi_serializer abis;
                abis.set_abi(abi, abi_serializer_max_time);
                const auto *t_id = d.find<chain::table_id_object, chain::by_code_scope_table>(
                        boost::make_tuple(p.code, scope, p.table));
                if (t_id != nullptr) {
                    const auto &idx = d.get_index<IndexType, chain::by_scope_primary>();
                    decltype(t_id->id) next_tid(t_id->id._id + 1);
                    auto lower = idx.lower_bound(boost::make_tuple(t_id->id));
                    auto upper = idx.lower_bound(boost::make_tuple(next_tid));

                    if (p.lower_bound.size()) {
                        if (p.key_type == "name") {
                            name s(p.lower_bound);
                            lower = idx.lower_bound(boost::make_tuple(t_id->id, s.value));
                        } else {
                            auto lv = convert_to_type<typename IndexType::value_type::key_type>(p.lower_bound,
                                                                                                "lower_bound");
                            lower = idx.lower_bound(boost::make_tuple(t_id->id, lv));
                        }
                    }
                    if (p.upper_bound.size()) {
                        if (p.key_type == "name") {
                            name s(p.upper_bound);
                            upper = idx.lower_bound(boost::make_tuple(t_id->id, s.value));
                        } else {
                            auto uv = convert_to_type<typename IndexType::value_type::key_type>(p.upper_bound,
                                                                                                "upper_bound");
                            upper = idx.lower_bound(boost::make_tuple(t_id->id, uv));
                        }
                    }

                    vector<char> data;

                    auto end = fc::time_point::now() + fc::microseconds(1000 * 10); /// 10ms max time

                    unsigned int count = 0;
                    auto itr = lower;
                    for (; itr != upper; ++itr) {
                        copy_inline_row(*itr, data);

                        if (p.json) {
                            result.rows.emplace_back(
                                    abis.binary_to_variant(abis.get_table_type(p.table), data, abi_serializer_max_time,
                                                           shorten_abi_errors));
                        } else {
                            result.rows.emplace_back(fc::variant(data));
                        }

                        if (++count == p.limit || fc::time_point::now() > end) {
                            ++itr;
                            break;
                        }
                    }
                    if (itr != upper) {
                        result.more = true;
                    }
                }
                return result;
            }

            chain::symbol extract_core_symbol() const;

            friend struct resolver_factory<read_only>;
        };

        class read_write {
            controller &db;
            const fc::microseconds abi_serializer_max_time;
        public:
            read_write(controller &db, const fc::microseconds &abi_serializer_max_time);

            void validate() const;

            using push_block_params = chain::signed_block;
            using push_block_results = empty;

            void push_block(const push_block_params &params,
                            chain::plugin_interface::next_function<push_block_results> next);

            using push_transaction_params = fc::variant_object;
            struct push_transaction_results {
                chain::transaction_id_type transaction_id;
                fc::variant processed;
            };

            void push_transaction(const push_transaction_params &params,
                                  chain::plugin_interface::next_function<push_transaction_results> next);

            using register_fio_name_params = fc::variant_object;
            struct register_fio_name_results {
                chain::transaction_id_type transaction_id;
                fc::variant processed;
            };

            void register_fio_name(const register_fio_name_params &params,
                                   chain::plugin_interface::next_function<register_fio_name_results> next);

            using add_pub_address_params = fc::variant_object;
            struct add_pub_address_results {
                chain::transaction_id_type transaction_id;
                fc::variant processed;
            };

            void add_pub_address(const add_pub_address_params &params,
                                 chain::plugin_interface::next_function<add_pub_address_results> next);

            //Begin Added for reject request api method
            using reject_funds_request_params = fc::variant_object;
            struct reject_funds_request_results {
                fc::variant processed;
            };

            void reject_funds_request(const reject_funds_request_params &params,
                                      chain::plugin_interface::next_function<reject_funds_request_results> next);
            //End added for record send api method.

            //Begin Added for record send api method
            using record_send_params = fc::variant_object;

            struct record_send_results {
                fc::variant processed;
            };

            void record_send(const record_send_params &params,
                             chain::plugin_interface::next_function<record_send_results> next);
            //End added for record send api method.

            //Begin Added for new funds request api method
            using new_funds_request_params = fc::variant_object;

            struct new_funds_request_results {
                fc::variant processed;
            };

            void new_funds_request(const new_funds_request_params &params,
                                   chain::plugin_interface::next_function<new_funds_request_results> next);
            //End added for new funds request api method.

            using push_transactions_params  = vector<push_transaction_params>;
            using push_transactions_results = vector<push_transaction_results>;

            void push_transactions(const push_transactions_params &params,
                                   chain::plugin_interface::next_function<push_transactions_results> next);

            friend resolver_factory<read_write>;
        };

        //support for --key_types [sha256,ripemd160] and --encoding [dec/hex]
        constexpr const char i64[] = "i64";
        constexpr const char i128[] = "i128";
        constexpr const char i256[] = "i256";
        constexpr const char float64[] = "float64";
        constexpr const char float128[] = "float128";
        constexpr const char sha256[] = "sha256";
        constexpr const char ripemd160[] = "ripemd160";
        constexpr const char dec[] = "dec";
        constexpr const char hex[] = "hex";


        template<const char *key_type, const char *encoding = chain_apis::dec>
        struct keytype_converter;

        template<>
        struct keytype_converter<chain_apis::sha256, chain_apis::hex> {
            using input_type = chain::checksum256_type;
            using index_type = chain::index256_index;

            static auto function() {
                return [](const input_type &v) {
                    chain::key256_t k;
                    k[0] = ((uint128_t *) &v._hash)[0]; //0-127
                    k[1] = ((uint128_t *) &v._hash)[1]; //127-256
                    return k;
                };
            }
        };

        //key160 support with padding zeros in the end of key256
        template<>
        struct keytype_converter<chain_apis::ripemd160, chain_apis::hex> {
            using input_type = chain::checksum160_type;
            using index_type = chain::index256_index;

            static auto function() {
                return [](const input_type &v) {
                    chain::key256_t k;
                    memset(k.data(), 0, sizeof(k));
                    memcpy(k.data(), v._hash, sizeof(v._hash));
                    return k;
                };
            }
        };

        template<>
        struct keytype_converter<chain_apis::i256> {
            using input_type = boost::multiprecision::uint256_t;
            using index_type = chain::index256_index;

            static auto function() {
                return [](const input_type v) {
                    chain::key256_t k;
                    k[0] = ((uint128_t *) &v)[0]; //0-127
                    k[1] = ((uint128_t *) &v)[1]; //127-256
                    return k;
                };
            }
        };

    } // namespace chain_apis

    class chain_plugin : public plugin<chain_plugin> {
    public:
        APPBASE_PLUGIN_REQUIRES()

        chain_plugin();

        virtual ~chain_plugin();

        virtual void set_program_options(options_description &cli, options_description &cfg) override;

        void plugin_initialize(const variables_map &options);

        void plugin_startup();

        void plugin_shutdown();

        chain_apis::read_only get_read_only_api() const {
            return chain_apis::read_only(chain(), get_abi_serializer_max_time());
        }

        chain_apis::read_write get_read_write_api();

        void accept_block(const chain::signed_block_ptr &block);

        void accept_transaction(const chain::packed_transaction &trx,
                                chain::plugin_interface::next_function<chain::transaction_trace_ptr> next);

        bool block_is_on_preferred_chain(const chain::block_id_type &block_id);

        static bool recover_reversible_blocks(const fc::path &db_dir,
                                              uint32_t cache_size,
                                              optional<fc::path> new_db_dir = optional<fc::path>(),
                                              uint32_t truncate_at_block = 0
        );

        static bool import_reversible_blocks(const fc::path &reversible_dir,
                                             uint32_t cache_size,
                                             const fc::path &reversible_blocks_file
        );

        static bool export_reversible_blocks(const fc::path &reversible_dir,
                                             const fc::path &reversible_blocks_file
        );

        // Only call this after plugin_initialize()!
        controller &chain();

        // Only call this after plugin_initialize()!
        const controller &chain() const;

        chain::chain_id_type get_chain_id() const;

        fc::microseconds get_abi_serializer_max_time() const;

        const chain_apis::fio_config_parameters &get_fio_config() const;

        void handle_guard_exception(const chain::guard_exception &e) const;

        static void handle_db_exhaustion();

    private:
        void log_guard_exception(const chain::guard_exception &e) const;

        unique_ptr<class chain_plugin_impl> my;
    };

}

FC_REFLECT(eosio::chain_apis::permission, (perm_name)(parent)(required_auth))
FC_REFLECT(eosio::chain_apis::empty,)
FC_REFLECT(eosio::chain_apis::read_only::get_info_results,
           (server_version)(chain_id)(head_block_num)(last_irreversible_block_num)(last_irreversible_block_id)(
                   head_block_id)(head_block_time)(head_block_producer)(virtual_block_cpu_limit)(
                   virtual_block_net_limit)(block_cpu_limit)(block_net_limit)(server_version_string))
FC_REFLECT(eosio::chain_apis::read_only::get_block_params, (block_num_or_id))
FC_REFLECT(eosio::chain_apis::read_only::get_block_header_state_params, (block_num_or_id))

FC_REFLECT(eosio::chain_apis::read_write::push_transaction_results, (transaction_id)(processed))

FC_REFLECT(eosio::chain_apis::read_only::get_table_rows_params,
           (json)(code)(scope)(table)(table_key)(lower_bound)(upper_bound)(limit)(key_type)(index_position)(
                   encode_type))
FC_REFLECT(eosio::chain_apis::read_only::get_table_rows_result, (rows)(more));

FC_REFLECT(eosio::chain_apis::read_only::get_pending_fio_requests_params, (fiopubadd))
FC_REFLECT(eosio::chain_apis::read_only::get_pending_fio_requests_result, (requests))
FC_REFLECT(eosio::chain_apis::read_only::get_sent_fio_requests_params, (fiopubadd))
FC_REFLECT(eosio::chain_apis::read_only::get_sent_fio_requests_result, (requests))
FC_REFLECT(eosio::chain_apis::request_record,
           (fioreqid)(fromfioadd)(tofioadd)(topubadd)(amount)(tokencode)(metadata)(timestamp))

FC_REFLECT(eosio::chain_apis::read_only::pub_address_lookup_params, (fio_address)(token_code))
FC_REFLECT(eosio::chain_apis::read_only::pub_address_lookup_result, (fio_address)(token_code)(pub_address));

FC_REFLECT(eosio::chain_apis::fiodomain_record, (fio_domain)(expiration))
FC_REFLECT(eosio::chain_apis::fioaddress_record, (fio_address)(expiration))

FC_REFLECT(eosio::chain_apis::read_only::get_fio_names_params, (fio_pub_address))
FC_REFLECT(eosio::chain_apis::read_only::get_fio_names_result, (fio_pub_address)(fio_domains)(fio_addresses));

FC_REFLECT(eosio::chain_apis::read_only::avail_check_params, (fio_name))
FC_REFLECT(eosio::chain_apis::read_only::avail_check_result, (fio_name)(is_registered));

FC_REFLECT(eosio::chain_apis::read_only::fio_key_lookup_params, (key)(chain))
FC_REFLECT(eosio::chain_apis::read_only::fio_key_lookup_result, (name)(expiration));
FC_REFLECT(eosio::chain_apis::read_write::register_fio_name_results, (transaction_id)(processed))
FC_REFLECT(eosio::chain_apis::read_write::reject_funds_request_results, (processed))
FC_REFLECT(eosio::chain_apis::read_write::record_send_results, (processed))

FC_REFLECT(eosio::chain_apis::read_write::add_pub_address_results, (transaction_id)(processed))
FC_REFLECT(eosio::chain_apis::read_write::new_funds_request_results, (processed))

FC_REFLECT(eosio::chain_apis::read_only::get_table_by_scope_params, (code)(table)(lower_bound)(upper_bound)(limit))
FC_REFLECT(eosio::chain_apis::read_only::get_table_by_scope_result_row, (code)(scope)(table)(payer)(count));
FC_REFLECT(eosio::chain_apis::read_only::get_table_by_scope_result, (rows)(more));

FC_REFLECT(eosio::chain_apis::read_only::get_currency_balance_params, (code)(account)(symbol));
FC_REFLECT(eosio::chain_apis::read_only::get_currency_stats_params, (code)(symbol));
FC_REFLECT(eosio::chain_apis::read_only::get_currency_stats_result, (supply)(max_supply)(issuer));

FC_REFLECT(eosio::chain_apis::read_only::get_producers_params, (json)(lower_bound)(limit))
FC_REFLECT(eosio::chain_apis::read_only::get_producers_result, (rows)(total_producer_vote_weight)(more));

FC_REFLECT_EMPTY(eosio::chain_apis::read_only::get_producer_schedule_params)
FC_REFLECT(eosio::chain_apis::read_only::get_producer_schedule_result, (active)(pending)(proposed));

FC_REFLECT(eosio::chain_apis::read_only::get_scheduled_transactions_params, (json)(lower_bound)(limit))
FC_REFLECT(eosio::chain_apis::read_only::get_scheduled_transactions_result, (transactions)(more));

FC_REFLECT(eosio::chain_apis::read_only::get_account_results,
           (account_name)(head_block_num)(head_block_time)(privileged)(last_code_update)(created)
                   (core_liquid_balance)(ram_quota)(net_weight)(cpu_weight)(net_limit)(cpu_limit)(ram_usage)(
                   permissions)
                   (total_resources)(self_delegated_bandwidth)(refund_request)(voter_info))
FC_REFLECT(eosio::chain_apis::read_only::get_code_results, (account_name)(code_hash)(wast)(wasm)(abi))
FC_REFLECT(eosio::chain_apis::read_only::get_code_hash_results, (account_name)(code_hash))
FC_REFLECT(eosio::chain_apis::read_only::get_abi_results, (account_name)(abi))
FC_REFLECT(eosio::chain_apis::read_only::get_account_params, (account_name)(expected_core_symbol))
FC_REFLECT(eosio::chain_apis::read_only::get_code_params, (account_name)(code_as_wasm))
FC_REFLECT(eosio::chain_apis::read_only::get_code_hash_params, (account_name))
FC_REFLECT(eosio::chain_apis::read_only::get_abi_params, (account_name))
FC_REFLECT(eosio::chain_apis::read_only::get_raw_code_and_abi_params, (account_name))
FC_REFLECT(eosio::chain_apis::read_only::get_raw_code_and_abi_results, (account_name)(wasm)(abi))
FC_REFLECT(eosio::chain_apis::read_only::get_raw_abi_params, (account_name)(abi_hash))
FC_REFLECT(eosio::chain_apis::read_only::get_raw_abi_results, (account_name)(code_hash)(abi_hash)(abi))
FC_REFLECT(eosio::chain_apis::read_only::producer_info, (producer_name))
FC_REFLECT(eosio::chain_apis::read_only::abi_json_to_bin_params, (code)(action)(args))
FC_REFLECT(eosio::chain_apis::read_only::abi_json_to_bin_result, (binargs))
FC_REFLECT(eosio::chain_apis::read_only::abi_bin_to_json_params, (code)(action)(binargs))
FC_REFLECT(eosio::chain_apis::read_only::abi_bin_to_json_result, (args))
FC_REFLECT(eosio::chain_apis::read_only::serialize_json_params, (action)(json))
FC_REFLECT(eosio::chain_apis::read_only::serialize_json_result, (serialized_json))
FC_REFLECT(eosio::chain_apis::read_only::get_required_keys_params, (transaction)(available_keys))
FC_REFLECT(eosio::chain_apis::read_only::get_required_keys_result, (required_keys))