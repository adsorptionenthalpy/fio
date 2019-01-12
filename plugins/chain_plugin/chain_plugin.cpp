/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/chain/fork_database.hpp>
#include <eosio/chain/block_log.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/authorization_manager.hpp>
#include <eosio/chain/producer_object.hpp>
#include <eosio/chain/config.hpp>
#include <eosio/chain/wasm_interface.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/reversible_block_object.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/generated_transaction_object.hpp>
#include <eosio/chain/snapshot.hpp>
#include <eosio/chain/actionmapping.hpp>

#include <eosio/chain/eosio_contract.hpp>

#include <eosio/chain/fioio/fioerror.hpp>

#include <eosio/utilities/key_conversion.hpp>
#include <eosio/utilities/common.hpp>
#include <eosio/chain/wast_to_wasm.hpp>

#include <boost/signals2/connection.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include <fc/io/json.hpp>
#include <fc/variant.hpp>
#include <signal.h>
#include <cstdlib>

#include <eosio.system/eosio.system.wast.hpp>
#include <eosio.system/eosio.system.abi.hpp>

namespace eosio {

//declare operator<< and validate funciton for read_mode in the same namespace as read_mode itself
namespace chain {

std::ostream& operator<<(std::ostream& osm, eosio::chain::db_read_mode m) {
   if ( m == eosio::chain::db_read_mode::SPECULATIVE ) {
      osm << "speculative";
   } else if ( m == eosio::chain::db_read_mode::HEAD ) {
      osm << "head";
   } else if ( m == eosio::chain::db_read_mode::READ_ONLY ) {
      osm << "read-only";
   } else if ( m == eosio::chain::db_read_mode::IRREVERSIBLE ) {
      osm << "irreversible";
   }

   return osm;
}

void validate(boost::any& v,
              const std::vector<std::string>& values,
              eosio::chain::db_read_mode* /* target_type */,
              int)
{
  using namespace boost::program_options;

  // Make sure no previous assignment to 'v' was made.
  validators::check_first_occurrence(v);

  // Extract the first string from 'values'. If there is more than
  // one string, it's an error, and exception will be thrown.
  std::string const& s = validators::get_single_string(values);

  if ( s == "speculative" ) {
     v = boost::any(eosio::chain::db_read_mode::SPECULATIVE);
  } else if ( s == "head" ) {
     v = boost::any(eosio::chain::db_read_mode::HEAD);
  } else if ( s == "read-only" ) {
     v = boost::any(eosio::chain::db_read_mode::READ_ONLY);
  } else if ( s == "irreversible" ) {
     v = boost::any(eosio::chain::db_read_mode::IRREVERSIBLE);
  } else {
     throw validation_error(validation_error::invalid_option_value);
  }
}

std::ostream& operator<<(std::ostream& osm, eosio::chain::validation_mode m) {
   if ( m == eosio::chain::validation_mode::FULL ) {
      osm << "full";
   } else if ( m == eosio::chain::validation_mode::LIGHT ) {
      osm << "light";
   }

   return osm;
}

void validate(boost::any& v,
              const std::vector<std::string>& values,
              eosio::chain::validation_mode* /* target_type */,
              int)
{
  using namespace boost::program_options;

  // Make sure no previous assignment to 'v' was made.
  validators::check_first_occurrence(v);

  // Extract the first string from 'values'. If there is more than
  // one string, it's an error, and exception will be thrown.
  std::string const& s = validators::get_single_string(values);

  if ( s == "full" ) {
     v = boost::any(eosio::chain::validation_mode::FULL);
  } else if ( s == "light" ) {
     v = boost::any(eosio::chain::validation_mode::LIGHT);
  } else {
     throw validation_error(validation_error::invalid_option_value);
  }
}

}

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::chain::config;
using namespace eosio::chain::plugin_interface;
using vm_type = wasm_interface::vm_type;
using fc::flat_map;

using boost::signals2::scoped_connection;

//using txn_msg_rate_limits = controller::txn_msg_rate_limits;

#define CATCH_AND_CALL(NEXT)\
   catch ( const fc::exception& err ) {\
      NEXT(err.dynamic_copy_exception());\
   } catch ( const std::exception& e ) {\
      fc::exception fce( \
         FC_LOG_MESSAGE( warn, "rethrow ${what}: ", ("what",e.what())),\
         fc::std_exception_code,\
         BOOST_CORE_TYPEID(e).name(),\
         e.what() ) ;\
      NEXT(fce.dynamic_copy_exception());\
   } catch( ... ) {\
      fc::unhandled_exception e(\
         FC_LOG_MESSAGE(warn, "rethrow"),\
         std::current_exception());\
      NEXT(e.dynamic_copy_exception());\
   }


class chain_plugin_impl {
public:
   chain_plugin_impl()
   :pre_accepted_block_channel(app().get_channel<channels::pre_accepted_block>())
   ,accepted_block_header_channel(app().get_channel<channels::accepted_block_header>())
   ,accepted_block_channel(app().get_channel<channels::accepted_block>())
   ,irreversible_block_channel(app().get_channel<channels::irreversible_block>())
   ,accepted_transaction_channel(app().get_channel<channels::accepted_transaction>())
   ,applied_transaction_channel(app().get_channel<channels::applied_transaction>())
   ,accepted_confirmation_channel(app().get_channel<channels::accepted_confirmation>())
   ,incoming_block_channel(app().get_channel<incoming::channels::block>())
   ,incoming_block_sync_method(app().get_method<incoming::methods::block_sync>())
   ,incoming_transaction_async_method(app().get_method<incoming::methods::transaction_async>())
   {}

   bfs::path                        blocks_dir;
   bool                             readonly = false;
   flat_map<uint32_t,block_id_type> loaded_checkpoints;

   fc::optional<fork_database>      fork_db;
   fc::optional<block_log>          block_logger;
   fc::optional<controller::config> chain_config;
   fc::optional<controller>         chain;
   fc::optional<chain_id_type>      chain_id;
   //txn_msg_rate_limits              rate_limits;
   fc::optional<vm_type>            wasm_runtime;
   fc::microseconds                 abi_serializer_max_time_ms;
   fc::optional<bfs::path>          snapshot_path;


   // retained references to channels for easy publication
   channels::pre_accepted_block::channel_type&     pre_accepted_block_channel;
   channels::accepted_block_header::channel_type&  accepted_block_header_channel;
   channels::accepted_block::channel_type&         accepted_block_channel;
   channels::irreversible_block::channel_type&     irreversible_block_channel;
   channels::accepted_transaction::channel_type&   accepted_transaction_channel;
   channels::applied_transaction::channel_type&    applied_transaction_channel;
   channels::accepted_confirmation::channel_type&  accepted_confirmation_channel;
   incoming::channels::block::channel_type&         incoming_block_channel;

   // retained references to methods for easy calling
   incoming::methods::block_sync::method_type&        incoming_block_sync_method;
   incoming::methods::transaction_async::method_type& incoming_transaction_async_method;

   // method provider handles
   methods::get_block_by_number::method_type::handle                 get_block_by_number_provider;
   methods::get_block_by_id::method_type::handle                     get_block_by_id_provider;
   methods::get_head_block_id::method_type::handle                   get_head_block_id_provider;
   methods::get_last_irreversible_block_number::method_type::handle  get_last_irreversible_block_number_provider;

   // scoped connections for chain controller
   fc::optional<scoped_connection>                                   pre_accepted_block_connection;
   fc::optional<scoped_connection>                                   accepted_block_header_connection;
   fc::optional<scoped_connection>                                   accepted_block_connection;
   fc::optional<scoped_connection>                                   irreversible_block_connection;
   fc::optional<scoped_connection>                                   accepted_transaction_connection;
   fc::optional<scoped_connection>                                   applied_transaction_connection;
   fc::optional<scoped_connection>                                   accepted_confirmation_connection;


};

chain_plugin::chain_plugin()
:my(new chain_plugin_impl()) {
}

chain_plugin::~chain_plugin(){}

void chain_plugin::set_program_options(options_description& cli, options_description& cfg)
{
   cfg.add_options()
         ("blocks-dir", bpo::value<bfs::path>()->default_value("blocks"),
          "the location of the blocks directory (absolute path or relative to application data dir)")
         ("checkpoint", bpo::value<vector<string>>()->composing(), "Pairs of [BLOCK_NUM,BLOCK_ID] that should be enforced as checkpoints.")
         ("wasm-runtime", bpo::value<eosio::chain::wasm_interface::vm_type>()->value_name("wavm/wabt"), "Override default WASM runtime")
         ("abi-serializer-max-time-ms", bpo::value<uint32_t>()->default_value(config::default_abi_serializer_max_time_ms),
          "Override default maximum ABI serialization time allowed in ms")
         ("chain-state-db-size-mb", bpo::value<uint64_t>()->default_value(config::default_state_size / (1024  * 1024)), "Maximum size (in MiB) of the chain state database")
         ("chain-state-db-guard-size-mb", bpo::value<uint64_t>()->default_value(config::default_state_guard_size / (1024  * 1024)), "Safely shut down node when free space remaining in the chain state database drops below this size (in MiB).")
         ("reversible-blocks-db-size-mb", bpo::value<uint64_t>()->default_value(config::default_reversible_cache_size / (1024  * 1024)), "Maximum size (in MiB) of the reversible blocks database")
         ("reversible-blocks-db-guard-size-mb", bpo::value<uint64_t>()->default_value(config::default_reversible_guard_size / (1024  * 1024)), "Safely shut down node when free space remaining in the reverseible blocks database drops below this size (in MiB).")
         ("contracts-console", bpo::bool_switch()->default_value(false),
          "print contract's output to console")
         ("actor-whitelist", boost::program_options::value<vector<string>>()->composing()->multitoken(),
          "Account added to actor whitelist (may specify multiple times)")
         ("actor-blacklist", boost::program_options::value<vector<string>>()->composing()->multitoken(),
          "Account added to actor blacklist (may specify multiple times)")
         ("contract-whitelist", boost::program_options::value<vector<string>>()->composing()->multitoken(),
          "Contract account added to contract whitelist (may specify multiple times)")
         ("contract-blacklist", boost::program_options::value<vector<string>>()->composing()->multitoken(),
          "Contract account added to contract blacklist (may specify multiple times)")
         ("action-blacklist", boost::program_options::value<vector<string>>()->composing()->multitoken(),
          "Action (in the form code::action) added to action blacklist (may specify multiple times)")
         ("key-blacklist", boost::program_options::value<vector<string>>()->composing()->multitoken(),
          "Public key added to blacklist of keys that should not be included in authorities (may specify multiple times)")
         ("read-mode", boost::program_options::value<eosio::chain::db_read_mode>()->default_value(eosio::chain::db_read_mode::SPECULATIVE),
          "Database read mode (\"speculative\", \"head\", or \"read-only\").\n"// or \"irreversible\").\n"
          "In \"speculative\" mode database contains changes done up to the head block plus changes made by transactions not yet included to the blockchain.\n"
          "In \"head\" mode database contains changes done up to the current head block.\n"
          "In \"read-only\" mode database contains incoming block changes but no speculative transaction processing.\n"
          )
          //"In \"irreversible\" mode database contains changes done up the current irreversible block.\n")
         ("validation-mode", boost::program_options::value<eosio::chain::validation_mode>()->default_value(eosio::chain::validation_mode::FULL),
          "Chain validation mode (\"full\" or \"light\").\n"
          "In \"full\" mode all incoming blocks will be fully validated.\n"
          "In \"light\" mode all incoming blocks headers will be fully validated; transactions in those validated blocks will be trusted \n")
         ("disable-ram-billing-notify-checks", bpo::bool_switch()->default_value(false),
          "Disable the check which subjectively fails a transaction if a contract bills more RAM to another account within the context of a notification handler (i.e. when the receiver is not the code of the action).")
         ;

// TODO: rate limiting
         /*("per-authorized-account-transaction-msg-rate-limit-time-frame-sec", bpo::value<uint32_t>()->default_value(default_per_auth_account_time_frame_seconds),
          "The time frame, in seconds, that the per-authorized-account-transaction-msg-rate-limit is imposed over.")
         ("per-authorized-account-transaction-msg-rate-limit", bpo::value<uint32_t>()->default_value(default_per_auth_account),
          "Limits the maximum rate of transaction messages that an account is allowed each per-authorized-account-transaction-msg-rate-limit-time-frame-sec.")
          ("per-code-account-transaction-msg-rate-limit-time-frame-sec", bpo::value<uint32_t>()->default_value(default_per_code_account_time_frame_seconds),
           "The time frame, in seconds, that the per-code-account-transaction-msg-rate-limit is imposed over.")
          ("per-code-account-transaction-msg-rate-limit", bpo::value<uint32_t>()->default_value(default_per_code_account),
           "Limits the maximum rate of transaction messages that an account's code is allowed each per-code-account-transaction-msg-rate-limit-time-frame-sec.")*/

   cli.add_options()
         ("genesis-json", bpo::value<bfs::path>(), "File to read Genesis State from")
         ("genesis-timestamp", bpo::value<string>(), "override the initial timestamp in the Genesis State file")
         ("print-genesis-json", bpo::bool_switch()->default_value(false),
          "extract genesis_state from blocks.log as JSON, print to console, and exit")
         ("extract-genesis-json", bpo::value<bfs::path>(),
          "extract genesis_state from blocks.log as JSON, write into specified file, and exit")
         ("fix-reversible-blocks", bpo::bool_switch()->default_value(false),
          "recovers reversible block database if that database is in a bad state")
         ("force-all-checks", bpo::bool_switch()->default_value(false),
          "do not skip any checks that can be skipped while replaying irreversible blocks")
         ("disable-replay-opts", bpo::bool_switch()->default_value(false),
          "disable optimizations that specifically target replay")
         ("replay-blockchain", bpo::bool_switch()->default_value(false),
          "clear chain state database and replay all blocks")
         ("hard-replay-blockchain", bpo::bool_switch()->default_value(false),
          "clear chain state database, recover as many blocks as possible from the block log, and then replay those blocks")
         ("delete-all-blocks", bpo::bool_switch()->default_value(false),
          "clear chain state database and block log")
         ("truncate-at-block", bpo::value<uint32_t>()->default_value(0),
          "stop hard replay / block log recovery at this block number (if set to non-zero number)")
         ("import-reversible-blocks", bpo::value<bfs::path>(),
          "replace reversible block database with blocks imported from specified file and then exit")
         ("export-reversible-blocks", bpo::value<bfs::path>(),
           "export reversible block database in portable format into specified file and then exit")
         ("trusted-producer", bpo::value<vector<string>>()->composing(), "Indicate a producer whose blocks headers signed by it will be fully validated, but transactions in those validated blocks will be trusted.")
         ("snapshot", bpo::value<bfs::path>(), "File to read Snapshot State from")
         ;

}

#define LOAD_VALUE_SET(options, name, container) \
if( options.count(name) ) { \
   const std::vector<std::string>& ops = options[name].as<std::vector<std::string>>(); \
   std::copy(ops.begin(), ops.end(), std::inserter(container, container.end())); \
}

fc::time_point calculate_genesis_timestamp( string tstr ) {
   fc::time_point genesis_timestamp;
   if( strcasecmp (tstr.c_str(), "now") == 0 ) {
      genesis_timestamp = fc::time_point::now();
   } else {
      genesis_timestamp = time_point::from_iso_string( tstr );
   }

   auto epoch_us = genesis_timestamp.time_since_epoch().count();
   auto diff_us = epoch_us % config::block_interval_us;
   if (diff_us > 0) {
      auto delay_us = (config::block_interval_us - diff_us);
      genesis_timestamp += fc::microseconds(delay_us);
      dlog("pausing ${us} microseconds to the next interval",("us",delay_us));
   }

   ilog( "Adjusting genesis timestamp to ${timestamp}", ("timestamp", genesis_timestamp) );
   return genesis_timestamp;
}

void clear_directory_contents( const fc::path& p ) {
   using boost::filesystem::directory_iterator;

   if( !fc::is_directory( p ) )
      return;

   for( directory_iterator enditr, itr{p}; itr != enditr; ++itr ) {
      fc::remove_all( itr->path() );
   }
}

void chain_plugin::plugin_initialize(const variables_map& options) {
   ilog("initializing chain plugin");

   try {
      try {
         genesis_state gs; // Check if EOSIO_ROOT_KEY is bad
      } catch ( const fc::exception& ) {
         elog( "EOSIO_ROOT_KEY ('${root_key}') is invalid. Recompile with a valid public key.",
               ("root_key", genesis_state::eosio_root_key));
         throw;
      }

      my->chain_config = controller::config();

      LOAD_VALUE_SET( options, "actor-whitelist", my->chain_config->actor_whitelist );
      LOAD_VALUE_SET( options, "actor-blacklist", my->chain_config->actor_blacklist );
      LOAD_VALUE_SET( options, "contract-whitelist", my->chain_config->contract_whitelist );
      LOAD_VALUE_SET( options, "contract-blacklist", my->chain_config->contract_blacklist );

      LOAD_VALUE_SET( options, "trusted-producer", my->chain_config->trusted_producers );

      if( options.count( "action-blacklist" )) {
         const std::vector<std::string>& acts = options["action-blacklist"].as<std::vector<std::string>>();
         auto& list = my->chain_config->action_blacklist;
         for( const auto& a : acts ) {
            auto pos = a.find( "::" );
            EOS_ASSERT( pos != std::string::npos, plugin_config_exception, "Invalid entry in action-blacklist: '${a}'", ("a", a));
            account_name code( a.substr( 0, pos ));
            action_name act( a.substr( pos + 2 ));
            list.emplace( code.value, act.value );
         }
      }

      if( options.count( "key-blacklist" )) {
         const std::vector<std::string>& keys = options["key-blacklist"].as<std::vector<std::string>>();
         auto& list = my->chain_config->key_blacklist;
         for( const auto& key_str : keys ) {
            list.emplace( key_str );
         }
      }

      if( options.count( "blocks-dir" )) {
         auto bld = options.at( "blocks-dir" ).as<bfs::path>();
         if( bld.is_relative())
            my->blocks_dir = app().data_dir() / bld;
         else
            my->blocks_dir = bld;
      }

      if( options.count("checkpoint") ) {
         auto cps = options.at("checkpoint").as<vector<string>>();
         my->loaded_checkpoints.reserve(cps.size());
         for( const auto& cp : cps ) {
            auto item = fc::json::from_string(cp).as<std::pair<uint32_t,block_id_type>>();
            auto itr = my->loaded_checkpoints.find(item.first);
            if( itr != my->loaded_checkpoints.end() ) {
               EOS_ASSERT( itr->second == item.second,
                           plugin_config_exception,
                          "redefining existing checkpoint at block number ${num}: original: ${orig} new: ${new}",
                          ("num", item.first)("orig", itr->second)("new", item.second)
               );
            } else {
               my->loaded_checkpoints[item.first] = item.second;
            }
         }
      }

      if( options.count( "wasm-runtime" ))
         my->wasm_runtime = options.at( "wasm-runtime" ).as<vm_type>();

      if(options.count("abi-serializer-max-time-ms"))
         my->abi_serializer_max_time_ms = fc::microseconds(options.at("abi-serializer-max-time-ms").as<uint32_t>() * 1000);

      my->chain_config->blocks_dir = my->blocks_dir;
      my->chain_config->state_dir = app().data_dir() / config::default_state_dir_name;
      my->chain_config->read_only = my->readonly;

      if( options.count( "chain-state-db-size-mb" ))
         my->chain_config->state_size = options.at( "chain-state-db-size-mb" ).as<uint64_t>() * 1024 * 1024;

      if( options.count( "chain-state-db-guard-size-mb" ))
         my->chain_config->state_guard_size = options.at( "chain-state-db-guard-size-mb" ).as<uint64_t>() * 1024 * 1024;

      if( options.count( "reversible-blocks-db-size-mb" ))
         my->chain_config->reversible_cache_size =
               options.at( "reversible-blocks-db-size-mb" ).as<uint64_t>() * 1024 * 1024;

      if( options.count( "reversible-blocks-db-guard-size-mb" ))
         my->chain_config->reversible_guard_size = options.at( "reversible-blocks-db-guard-size-mb" ).as<uint64_t>() * 1024 * 1024;

      if( my->wasm_runtime )
         my->chain_config->wasm_runtime = *my->wasm_runtime;

      my->chain_config->force_all_checks = options.at( "force-all-checks" ).as<bool>();
      my->chain_config->disable_replay_opts = options.at( "disable-replay-opts" ).as<bool>();
      my->chain_config->contracts_console = options.at( "contracts-console" ).as<bool>();
      my->chain_config->allow_ram_billing_in_notify = options.at( "disable-ram-billing-notify-checks" ).as<bool>();

      if( options.count( "extract-genesis-json" ) || options.at( "print-genesis-json" ).as<bool>()) {
         genesis_state gs;

         if( fc::exists( my->blocks_dir / "blocks.log" )) {
            gs = block_log::extract_genesis_state( my->blocks_dir );
         } else {
            wlog( "No blocks.log found at '${p}'. Using default genesis state.",
                  ("p", (my->blocks_dir / "blocks.log").generic_string()));
         }

         if( options.at( "print-genesis-json" ).as<bool>()) {
            ilog( "Genesis JSON:\n${genesis}", ("genesis", json::to_pretty_string( gs )));
         }

         if( options.count( "extract-genesis-json" )) {
            auto p = options.at( "extract-genesis-json" ).as<bfs::path>();

            if( p.is_relative()) {
               p = bfs::current_path() / p;
            }

            fc::json::save_to_file( gs, p, true );
            ilog( "Saved genesis JSON to '${path}'", ("path", p.generic_string()));
         }

         EOS_THROW( extract_genesis_state_exception, "extracted genesis state from blocks.log" );
      }

      if( options.count("export-reversible-blocks") ) {
         auto p = options.at( "export-reversible-blocks" ).as<bfs::path>();

         if( p.is_relative()) {
            p = bfs::current_path() / p;
         }

         if( export_reversible_blocks( my->chain_config->blocks_dir/config::reversible_blocks_dir_name, p ) )
            ilog( "Saved all blocks from reversible block database into '${path}'", ("path", p.generic_string()) );
         else
            ilog( "Saved recovered blocks from reversible block database into '${path}'", ("path", p.generic_string()) );

         EOS_THROW( node_management_success, "exported reversible blocks" );
      }

      if( options.at( "delete-all-blocks" ).as<bool>()) {
         ilog( "Deleting state database and blocks" );
         if( options.at( "truncate-at-block" ).as<uint32_t>() > 0 )
            wlog( "The --truncate-at-block option does not make sense when deleting all blocks." );
         clear_directory_contents( my->chain_config->state_dir );
         fc::remove_all( my->blocks_dir );
      } else if( options.at( "hard-replay-blockchain" ).as<bool>()) {
         ilog( "Hard replay requested: deleting state database" );
         clear_directory_contents( my->chain_config->state_dir );
         auto backup_dir = block_log::repair_log( my->blocks_dir, options.at( "truncate-at-block" ).as<uint32_t>());
         if( fc::exists( backup_dir / config::reversible_blocks_dir_name ) ||
             options.at( "fix-reversible-blocks" ).as<bool>()) {
            // Do not try to recover reversible blocks if the directory does not exist, unless the option was explicitly provided.
            if( !recover_reversible_blocks( backup_dir / config::reversible_blocks_dir_name,
                                            my->chain_config->reversible_cache_size,
                                            my->chain_config->blocks_dir / config::reversible_blocks_dir_name,
                                            options.at( "truncate-at-block" ).as<uint32_t>())) {
               ilog( "Reversible blocks database was not corrupted. Copying from backup to blocks directory." );
               fc::copy( backup_dir / config::reversible_blocks_dir_name,
                         my->chain_config->blocks_dir / config::reversible_blocks_dir_name );
               fc::copy( backup_dir / config::reversible_blocks_dir_name / "shared_memory.bin",
                         my->chain_config->blocks_dir / config::reversible_blocks_dir_name / "shared_memory.bin" );
               fc::copy( backup_dir / config::reversible_blocks_dir_name / "shared_memory.meta",
                         my->chain_config->blocks_dir / config::reversible_blocks_dir_name / "shared_memory.meta" );
            }
         }
      } else if( options.at( "replay-blockchain" ).as<bool>()) {
         ilog( "Replay requested: deleting state database" );
         if( options.at( "truncate-at-block" ).as<uint32_t>() > 0 )
            wlog( "The --truncate-at-block option does not work for a regular replay of the blockchain." );
         clear_directory_contents( my->chain_config->state_dir );
         if( options.at( "fix-reversible-blocks" ).as<bool>()) {
            if( !recover_reversible_blocks( my->chain_config->blocks_dir / config::reversible_blocks_dir_name,
                                            my->chain_config->reversible_cache_size )) {
               ilog( "Reversible blocks database was not corrupted." );
            }
         }
      } else if( options.at( "fix-reversible-blocks" ).as<bool>()) {
         if( !recover_reversible_blocks( my->chain_config->blocks_dir / config::reversible_blocks_dir_name,
                                         my->chain_config->reversible_cache_size,
                                         optional<fc::path>(),
                                         options.at( "truncate-at-block" ).as<uint32_t>())) {
            ilog( "Reversible blocks database verified to not be corrupted. Now exiting..." );
         } else {
            ilog( "Exiting after fixing reversible blocks database..." );
         }
         EOS_THROW( fixed_reversible_db_exception, "fixed corrupted reversible blocks database" );
      } else if( options.at( "truncate-at-block" ).as<uint32_t>() > 0 ) {
         wlog( "The --truncate-at-block option can only be used with --fix-reversible-blocks without a replay or with --hard-replay-blockchain." );
      } else if( options.count("import-reversible-blocks") ) {
         auto reversible_blocks_file = options.at("import-reversible-blocks").as<bfs::path>();
         ilog("Importing reversible blocks from '${file}'", ("file", reversible_blocks_file.generic_string()) );
         fc::remove_all( my->chain_config->blocks_dir/config::reversible_blocks_dir_name );

         import_reversible_blocks( my->chain_config->blocks_dir/config::reversible_blocks_dir_name,
                                   my->chain_config->reversible_cache_size, reversible_blocks_file );

         EOS_THROW( node_management_success, "imported reversible blocks" );
      }

      if( options.count("import-reversible-blocks") ) {
         wlog("The --import-reversible-blocks option should be used by itself.");
      }

      if (options.count( "snapshot" )) {
         my->snapshot_path = options.at( "snapshot" ).as<bfs::path>();
         EOS_ASSERT( fc::exists(*my->snapshot_path), plugin_config_exception,
                     "Cannot load snapshot, ${name} does not exist", ("name", my->snapshot_path->generic_string()) );

         // recover genesis information from the snapshot
         auto infile = std::ifstream(my->snapshot_path->generic_string(), (std::ios::in | std::ios::binary));
         auto reader = std::make_shared<istream_snapshot_reader>(infile);
         reader->validate();
         reader->read_section<genesis_state>([this]( auto &section ){
            section.read_row(my->chain_config->genesis);
         });
         infile.close();

         EOS_ASSERT( options.count( "genesis-json" ) == 0 &&  options.count( "genesis-timestamp" ) == 0,
                 plugin_config_exception,
                 "--snapshot is incompatible with --genesis-json and --genesis-timestamp as the snapshot contains genesis information");

         auto shared_mem_path = my->chain_config->state_dir / "shared_memory.bin";
         EOS_ASSERT( !fc::exists(shared_mem_path),
                 plugin_config_exception,
                 "Snapshot can only be used to initialize an empty database." );

         if( fc::is_regular_file( my->blocks_dir / "blocks.log" )) {
            auto log_genesis = block_log::extract_genesis_state(my->blocks_dir);
            EOS_ASSERT( log_genesis.compute_chain_id() == my->chain_config->genesis.compute_chain_id(),
                    plugin_config_exception,
                    "Genesis information in blocks.log does not match genesis information in the snapshot");
         }

      } else {
         if( options.count( "genesis-json" )) {
            EOS_ASSERT( !fc::exists( my->blocks_dir / "blocks.log" ),
                        plugin_config_exception,
                       "Genesis state can only be set on a fresh blockchain." );

            auto genesis_file = options.at( "genesis-json" ).as<bfs::path>();
            if( genesis_file.is_relative()) {
               genesis_file = bfs::current_path() / genesis_file;
            }

            EOS_ASSERT( fc::is_regular_file( genesis_file ),
                        plugin_config_exception,
                       "Specified genesis file '${genesis}' does not exist.",
                       ("genesis", genesis_file.generic_string()));

            my->chain_config->genesis = fc::json::from_file( genesis_file ).as<genesis_state>();

            ilog( "Using genesis state provided in '${genesis}'", ("genesis", genesis_file.generic_string()));

            if( options.count( "genesis-timestamp" )) {
               my->chain_config->genesis.initial_timestamp = calculate_genesis_timestamp(
                     options.at( "genesis-timestamp" ).as<string>());
            }

            wlog( "Starting up fresh blockchain with provided genesis state." );
         } else if( options.count( "genesis-timestamp" )) {
            EOS_ASSERT( !fc::exists( my->blocks_dir / "blocks.log" ),
                        plugin_config_exception,
                       "Genesis state can only be set on a fresh blockchain." );

            my->chain_config->genesis.initial_timestamp = calculate_genesis_timestamp(
                  options.at( "genesis-timestamp" ).as<string>());

            wlog( "Starting up fresh blockchain with default genesis state but with adjusted genesis timestamp." );
         } else if( fc::is_regular_file( my->blocks_dir / "blocks.log" )) {
            my->chain_config->genesis = block_log::extract_genesis_state( my->blocks_dir );
         } else {
            wlog( "Starting up fresh blockchain with default genesis state." );
         }
      }

      if ( options.count("read-mode") ) {
         my->chain_config->read_mode = options.at("read-mode").as<db_read_mode>();
         EOS_ASSERT( my->chain_config->read_mode != db_read_mode::IRREVERSIBLE, plugin_config_exception, "irreversible mode not currently supported." );
      }

      if ( options.count("validation-mode") ) {
         my->chain_config->block_validation_mode = options.at("validation-mode").as<validation_mode>();
      }

      my->chain.emplace( *my->chain_config );
      my->chain_id.emplace( my->chain->get_chain_id());

      // set up method providers
      my->get_block_by_number_provider = app().get_method<methods::get_block_by_number>().register_provider(
            [this]( uint32_t block_num ) -> signed_block_ptr {
               return my->chain->fetch_block_by_number( block_num );
            } );

      my->get_block_by_id_provider = app().get_method<methods::get_block_by_id>().register_provider(
            [this]( block_id_type id ) -> signed_block_ptr {
               return my->chain->fetch_block_by_id( id );
            } );

      my->get_head_block_id_provider = app().get_method<methods::get_head_block_id>().register_provider( [this]() {
         return my->chain->head_block_id();
      } );

      my->get_last_irreversible_block_number_provider = app().get_method<methods::get_last_irreversible_block_number>().register_provider(
            [this]() {
               return my->chain->last_irreversible_block_num();
            } );

      // relay signals to channels
      my->pre_accepted_block_connection = my->chain->pre_accepted_block.connect([this](const signed_block_ptr& blk) {
         auto itr = my->loaded_checkpoints.find( blk->block_num() );
         if( itr != my->loaded_checkpoints.end() ) {
            auto id = blk->id();
            EOS_ASSERT( itr->second == id, checkpoint_exception,
                        "Checkpoint does not match for block number ${num}: expected: ${expected} actual: ${actual}",
                        ("num", blk->block_num())("expected", itr->second)("actual", id)
            );
         }

         my->pre_accepted_block_channel.publish(blk);
      });

      my->accepted_block_header_connection = my->chain->accepted_block_header.connect(
            [this]( const block_state_ptr& blk ) {
               my->accepted_block_header_channel.publish( blk );
            } );

      my->accepted_block_connection = my->chain->accepted_block.connect( [this]( const block_state_ptr& blk ) {
         my->accepted_block_channel.publish( blk );
      } );

      my->irreversible_block_connection = my->chain->irreversible_block.connect( [this]( const block_state_ptr& blk ) {
         my->irreversible_block_channel.publish( blk );
      } );

      my->accepted_transaction_connection = my->chain->accepted_transaction.connect(
            [this]( const transaction_metadata_ptr& meta ) {
               my->accepted_transaction_channel.publish( meta );
            } );

      my->applied_transaction_connection = my->chain->applied_transaction.connect(
            [this]( const transaction_trace_ptr& trace ) {
               my->applied_transaction_channel.publish( trace );
            } );

      my->accepted_confirmation_connection = my->chain->accepted_confirmation.connect(
            [this]( const header_confirmation& conf ) {
               my->accepted_confirmation_channel.publish( conf );
            } );

      my->chain->add_indices();
   } FC_LOG_AND_RETHROW()

}

void chain_plugin::plugin_startup()
{ try {
   try {
      if (my->snapshot_path) {
         auto infile = std::ifstream(my->snapshot_path->generic_string(), (std::ios::in | std::ios::binary));
         auto reader = std::make_shared<istream_snapshot_reader>(infile);
         my->chain->startup(reader);
         infile.close();
      } else {
         my->chain->startup();
      }
   } catch (const database_guard_exception& e) {
      log_guard_exception(e);
      // make sure to properly close the db
      my->chain.reset();
      throw;
   }

   if(!my->readonly) {
      ilog("starting chain in read/write mode");
   }

   ilog("Blockchain started; head block is #${num}, genesis timestamp is ${ts}",
        ("num", my->chain->head_block_num())("ts", (std::string)my->chain_config->genesis.initial_timestamp));

   my->chain_config.reset();
} FC_CAPTURE_AND_RETHROW() }

void chain_plugin::plugin_shutdown() {
   my->pre_accepted_block_connection.reset();
   my->accepted_block_header_connection.reset();
   my->accepted_block_connection.reset();
   my->irreversible_block_connection.reset();
   my->accepted_transaction_connection.reset();
   my->applied_transaction_connection.reset();
   my->accepted_confirmation_connection.reset();
   my->chain.reset();
}

chain_apis::read_write::read_write(controller& db, const fc::microseconds& abi_serializer_max_time)
: db(db)
, abi_serializer_max_time(abi_serializer_max_time)
{
}

void chain_apis::read_write::validate() const {
   EOS_ASSERT( db.get_read_mode() != chain::db_read_mode::READ_ONLY, missing_chain_api_plugin_exception, "Not allowed, node in read-only mode" );
}

chain_apis::read_write chain_plugin::get_read_write_api() {
   return chain_apis::read_write(chain(), get_abi_serializer_max_time());
}

void chain_plugin::accept_block(const signed_block_ptr& block ) {
   my->incoming_block_sync_method(block);
}

void chain_plugin::accept_transaction(const chain::packed_transaction& trx, next_function<chain::transaction_trace_ptr> next) {
   my->incoming_transaction_async_method(std::make_shared<packed_transaction>(trx), false, std::forward<decltype(next)>(next));
}

bool chain_plugin::block_is_on_preferred_chain(const block_id_type& block_id) {
   auto b = chain().fetch_block_by_number( block_header::num_from_id(block_id) );
   return b && b->id() == block_id;
}

bool chain_plugin::recover_reversible_blocks( const fc::path& db_dir, uint32_t cache_size,
                                              optional<fc::path> new_db_dir, uint32_t truncate_at_block ) {
   try {
      chainbase::database reversible( db_dir, database::read_only); // Test if dirty
      // If it reaches here, then the reversible database is not dirty

      if( truncate_at_block == 0 )
         return false;

      reversible.add_index<reversible_block_index>();
      const auto& ubi = reversible.get_index<reversible_block_index,by_num>();

      auto itr = ubi.rbegin();
      if( itr != ubi.rend() && itr->blocknum <= truncate_at_block )
         return false; // Because we are not going to be truncating the reversible database at all.
   } catch( const std::runtime_error& ) {
   } catch( ... ) {
      throw;
   }
   // Reversible block database is dirty. So back it up (unless already moved) and then create a new one.

   auto reversible_dir = fc::canonical( db_dir );
   if( reversible_dir.filename().generic_string() == "." ) {
      reversible_dir = reversible_dir.parent_path();
   }
   fc::path backup_dir;

   auto now = fc::time_point::now();

   if( new_db_dir ) {
      backup_dir = reversible_dir;
      reversible_dir = *new_db_dir;
   } else {
      auto reversible_dir_name = reversible_dir.filename().generic_string();
      EOS_ASSERT( reversible_dir_name != ".", invalid_reversible_blocks_dir, "Invalid path to reversible directory" );
      backup_dir = reversible_dir.parent_path() / reversible_dir_name.append("-").append( now );

      EOS_ASSERT( !fc::exists(backup_dir),
                  reversible_blocks_backup_dir_exist,
                 "Cannot move existing reversible directory to already existing directory '${backup_dir}'",
                 ("backup_dir", backup_dir) );

      fc::rename( reversible_dir, backup_dir );
      ilog( "Moved existing reversible directory to backup location: '${new_db_dir}'", ("new_db_dir", backup_dir) );
   }

   fc::create_directories( reversible_dir );

   ilog( "Reconstructing '${reversible_dir}' from backed up reversible directory", ("reversible_dir", reversible_dir) );

   chainbase::database  old_reversible( backup_dir, database::read_only, 0, true );
   chainbase::database  new_reversible( reversible_dir, database::read_write, cache_size );
   std::fstream         reversible_blocks;
   reversible_blocks.open( (reversible_dir.parent_path() / std::string("portable-reversible-blocks-").append( now ) ).generic_string().c_str(),
                           std::ios::out | std::ios::binary );

   uint32_t num = 0;
   uint32_t start = 0;
   uint32_t end = 0;
   old_reversible.add_index<reversible_block_index>();
   new_reversible.add_index<reversible_block_index>();
   const auto& ubi = old_reversible.get_index<reversible_block_index,by_num>();
   auto itr = ubi.begin();
   if( itr != ubi.end() ) {
      start = itr->blocknum;
      end = start - 1;
   }
   if( truncate_at_block > 0 && start > truncate_at_block ) {
      ilog( "Did not recover any reversible blocks since the specified block number to stop at (${stop}) is less than first block in the reversible database (${start}).", ("stop", truncate_at_block)("start", start) );
      return true;
   }
   try {
      for( ; itr != ubi.end(); ++itr ) {
         EOS_ASSERT( itr->blocknum == end + 1, gap_in_reversible_blocks_db,
                     "gap in reversible block database between ${end} and ${blocknum}",
                     ("end", end)("blocknum", itr->blocknum)
                   );
         reversible_blocks.write( itr->packedblock.data(), itr->packedblock.size() );
         new_reversible.create<reversible_block_object>( [&]( auto& ubo ) {
            ubo.blocknum = itr->blocknum;
            ubo.set_block( itr->get_block() ); // get_block and set_block rather than copying the packed data acts as additional validation
         });
         end = itr->blocknum;
         ++num;
         if( end == truncate_at_block )
            break;
      }
   } catch( const gap_in_reversible_blocks_db& e ) {
      wlog( "${details}", ("details", e.to_detail_string()) );
   } catch( ... ) {}

   if( end == truncate_at_block )
      ilog( "Stopped recovery of reversible blocks early at specified block number: ${stop}", ("stop", truncate_at_block) );

   if( num == 0 )
      ilog( "There were no recoverable blocks in the reversible block database" );
   else if( num == 1 )
      ilog( "Recovered 1 block from reversible block database: block ${start}", ("start", start) );
   else
      ilog( "Recovered ${num} blocks from reversible block database: blocks ${start} to ${end}",
            ("num", num)("start", start)("end", end) );

   return true;
}

bool chain_plugin::import_reversible_blocks( const fc::path& reversible_dir,
                                             uint32_t cache_size,
                                             const fc::path& reversible_blocks_file ) {
   std::fstream         reversible_blocks;
   chainbase::database  new_reversible( reversible_dir, database::read_write, cache_size );
   reversible_blocks.open( reversible_blocks_file.generic_string().c_str(), std::ios::in | std::ios::binary );

   reversible_blocks.seekg( 0, std::ios::end );
   uint64_t end_pos = reversible_blocks.tellg();
   reversible_blocks.seekg( 0 );

   uint32_t num = 0;
   uint32_t start = 0;
   uint32_t end = 0;
   new_reversible.add_index<reversible_block_index>();
   try {
      while( reversible_blocks.tellg() < end_pos ) {
         signed_block tmp;
         fc::raw::unpack(reversible_blocks, tmp);
         num = tmp.block_num();

         if( start == 0 ) {
            start = num;
         } else {
            EOS_ASSERT( num == end + 1, gap_in_reversible_blocks_db,
                        "gap in reversible block database between ${end} and ${num}",
                        ("end", end)("num", num)
                      );
         }

         new_reversible.create<reversible_block_object>( [&]( auto& ubo ) {
            ubo.blocknum = num;
            ubo.set_block( std::make_shared<signed_block>(tmp) );
         });
         end = num;
      }
   } catch( gap_in_reversible_blocks_db& e ) {
      wlog( "${details}", ("details", e.to_detail_string()) );
      FC_RETHROW_EXCEPTION( e, warn, "rethrow" );
   } catch( ... ) {}

   ilog( "Imported blocks ${start} to ${end}", ("start", start)("end", end));

   if( num == 0 || end != num )
      return false;

   return true;
}

bool chain_plugin::export_reversible_blocks( const fc::path& reversible_dir,
                                             const fc::path& reversible_blocks_file ) {
   chainbase::database  reversible( reversible_dir, database::read_only, 0, true );
   std::fstream         reversible_blocks;
   reversible_blocks.open( reversible_blocks_file.generic_string().c_str(), std::ios::out | std::ios::binary );

   uint32_t num = 0;
   uint32_t start = 0;
   uint32_t end = 0;
   reversible.add_index<reversible_block_index>();
   const auto& ubi = reversible.get_index<reversible_block_index,by_num>();
   auto itr = ubi.begin();
   if( itr != ubi.end() ) {
      start = itr->blocknum;
      end = start - 1;
   }
   try {
      for( ; itr != ubi.end(); ++itr ) {
         EOS_ASSERT( itr->blocknum == end + 1, gap_in_reversible_blocks_db,
                     "gap in reversible block database between ${end} and ${blocknum}",
                     ("end", end)("blocknum", itr->blocknum)
                   );
         signed_block tmp;
         fc::datastream<const char *> ds( itr->packedblock.data(), itr->packedblock.size() );
         fc::raw::unpack(ds, tmp); // Verify that packed block has not been corrupted.
         reversible_blocks.write( itr->packedblock.data(), itr->packedblock.size() );
         end = itr->blocknum;
         ++num;
      }
   } catch( const gap_in_reversible_blocks_db& e ) {
      wlog( "${details}", ("details", e.to_detail_string()) );
   } catch( ... ) {}

   if( num == 0 ) {
      ilog( "There were no recoverable blocks in the reversible block database" );
      return false;
   }
   else if( num == 1 )
      ilog( "Exported 1 block from reversible block database: block ${start}", ("start", start) );
   else
      ilog( "Exported ${num} blocks from reversible block database: blocks ${start} to ${end}",
            ("num", num)("start", start)("end", end) );

   return (end >= start) && ((end - start + 1) == num);
}

controller& chain_plugin::chain() { return *my->chain; }
const controller& chain_plugin::chain() const { return *my->chain; }

chain::chain_id_type chain_plugin::get_chain_id()const {
   EOS_ASSERT( my->chain_id.valid(), chain_id_type_exception, "chain ID has not been initialized yet" );
   return *my->chain_id;
}

fc::microseconds chain_plugin::get_abi_serializer_max_time() const {
   return my->abi_serializer_max_time_ms;
}

void chain_plugin::log_guard_exception(const chain::guard_exception&e ) const {
   if (e.code() == chain::database_guard_exception::code_value) {
      elog("Database has reached an unsafe level of usage, shutting down to avoid corrupting the database.  "
           "Please increase the value set for \"chain-state-db-size-mb\" and restart the process!");
   } else if (e.code() == chain::reversible_guard_exception::code_value) {
      elog("Reversible block database has reached an unsafe level of usage, shutting down to avoid corrupting the database.  "
           "Please increase the value set for \"reversible-blocks-db-size-mb\" and restart the process!");
   }

   dlog("Details: ${details}", ("details", e.to_detail_string()));
}

void chain_plugin::handle_guard_exception(const chain::guard_exception& e) const {
   log_guard_exception(e);

   // quit the app
   app().quit();
}

void chain_plugin::handle_db_exhaustion() {
   elog("database memory exhausted: increase chain-state-db-size-mb and/or reversible-blocks-db-size-mb");
   //return 1 -- it's what programs/nodeos/main.cpp considers "BAD_ALLOC"
   std::_Exit(1);
}

namespace chain_apis {

const string read_only::KEYi64 = "i64";

read_only::get_info_results read_only::get_info(const read_only::get_info_params&) const {
   const auto& rm = db.get_resource_limits_manager();
   return {
      eosio::utilities::common::itoh(static_cast<uint32_t>(app().version())),
      db.get_chain_id(),
      db.fork_db_head_block_num(),
      db.last_irreversible_block_num(),
      db.last_irreversible_block_id(),
      db.fork_db_head_block_id(),
      db.fork_db_head_block_time(),
      db.fork_db_head_block_producer(),
      rm.get_virtual_block_cpu_limit(),
      rm.get_virtual_block_net_limit(),
      rm.get_block_cpu_limit(),
      rm.get_block_net_limit(),
      //std::bitset<64>(db.get_dynamic_global_properties().recent_slots_filled).to_string(),
      //__builtin_popcountll(db.get_dynamic_global_properties().recent_slots_filled) / 64.0,
      app().version_string(),
   };
}

uint64_t read_only::get_table_index_name(const read_only::get_table_rows_params& p, bool& primary) {
   using boost::algorithm::starts_with;
   // see multi_index packing of index name
   const uint64_t table = p.table;
   uint64_t index = table & 0xFFFFFFFFFFFFFFF0ULL;
   EOS_ASSERT( index == table, chain::contract_table_query_exception, "Unsupported table name: ${n}", ("n", p.table) );

   primary = false;
   uint64_t pos = 0;
   if (p.index_position.empty() || p.index_position == "first" || p.index_position == "primary" || p.index_position == "one") {
      primary = true;
   } else if (starts_with(p.index_position, "sec") || p.index_position == "two") { // second, secondary
   } else if (starts_with(p.index_position , "ter") || starts_with(p.index_position, "th")) { // tertiary, ternary, third, three
      pos = 1;
   } else if (starts_with(p.index_position, "fou")) { // four, fourth
      pos = 2;
   } else if (starts_with(p.index_position, "fi")) { // five, fifth
      pos = 3;
   } else if (starts_with(p.index_position, "six")) { // six, sixth
      pos = 4;
   } else if (starts_with(p.index_position, "sev")) { // seven, seventh
      pos = 5;
   } else if (starts_with(p.index_position, "eig")) { // eight, eighth
      pos = 6;
   } else if (starts_with(p.index_position, "nin")) { // nine, ninth
      pos = 7;
   } else if (starts_with(p.index_position, "ten")) { // ten, tenth
      pos = 8;
   } else {
      try {
         pos = fc::to_uint64( p.index_position );
      } catch(...) {
         EOS_ASSERT( false, chain::contract_table_query_exception, "Invalid index_position: ${p}", ("p", p.index_position));
      }
      if (pos < 2) {
         primary = true;
         pos = 0;
      } else {
         pos -= 2;
      }
   }
   index |= (pos & 0x000000000000000FULL);
   return index;
}

template<>
uint64_t convert_to_type(const string& str, const string& desc) {
   uint64_t value = 0;
   try {
      value = boost::lexical_cast<uint64_t>(str.c_str(), str.size());
   } catch( ... ) {
      try {
         auto trimmed_str = str;
         boost::trim(trimmed_str);
         name s(trimmed_str);
         value = s.value;
      } catch( ... ) {
         try {
            auto symb = eosio::chain::symbol::from_string(str);
            value = symb.value();
         } catch( ... ) {
            try {
               value = ( eosio::chain::string_to_symbol( 0, str.c_str() ) >> 8 );
            } catch( ... ) {
               EOS_ASSERT( false, chain_type_exception, "Could not convert ${desc} string '${str}' to any of the following: "
                                 "uint64_t, valid name, or valid symbol (with or without the precision)",
                          ("desc", desc)("str", str));
            }
         }
      }
   }
   return value;
}

abi_def get_abi( const controller& db, const name& account ) {
   const auto &d = db.db();
   const account_object *code_accnt = d.find<account_object, by_name>(account);
   EOS_ASSERT(code_accnt != nullptr, chain::account_query_exception, "Fail to retrieve account for ${account}", ("account", account) );
   abi_def abi;
   abi_serializer::to_abi(code_accnt->abi, abi);
   return abi;
}

string get_table_type( const abi_def& abi, const name& table_name ) {
   for( const auto& t : abi.tables ) {
      if( t.name == table_name ){
         return t.index_type;
      }
   }
   EOS_ASSERT( false, chain::contract_table_query_exception, "Table ${table} is not specified in the ABI", ("table",table_name) );
}



/***** FIO API BEGIN *****/
/*************************/


// Used by fio_name_lookup
enum  class chain_type {
    FIO=0, EOS=1, BTC=2, ETH=3, XMR=4, BRD=5, BCH=6, NONE=7
};
const std::vector<std::string> chain_str {"FIO", "EOS", "BTC", "ETH", "XMR", "BRD", "BCH"};

// Convert of chain to chain type
inline chain_type str_to_chain_type(const string &chain) {

   for (int i = 0; i < chain_str.size(); i++) {
	  if (chain == chain_str[i]) {
		 return static_cast<chain_type>(i);
	  }
   }
   return chain_type::NONE;
}

/***
 * Lookup address by FIO name.
 * @param p Input is FIO name(.fio_name) and chain name(.chain). .chain is allowed to be null/empty, in which case this will bea domain only lookup.
 * @return .is_registered will be true if a match is found, else false. .is_domain is true upon domain match. .address is set if found. .expiration is set upon match.
 */
read_only::fio_name_lookup_result read_only::fio_name_lookup( const read_only::fio_name_lookup_params& p )const {

   // assert if empty fio name
   EOS_ASSERT( !p.fio_name.empty(), chain::contract_table_query_exception,"Invalid empty name");

   // Split the fio name and domain portions
   string fio_domain = "";
   string fio_user_name = "";

   int pos = p.fio_name.find('.');
   if (pos == string::npos) {
      fio_domain = p.fio_name;
   } else {
      fio_user_name = p.fio_name.substr(0, pos);
      fio_domain = p.fio_name.substr(pos + 1, string::npos);
   }

   //declare variables.
   const name code = ::eosio::string_to_name(fio_system_code.c_str());
   const abi_def abi = eosio::chain_apis::get_abi( db, code );

   const uint64_t name_hash = ::eosio::string_to_uint64_t(p.fio_name.c_str());
   const uint64_t domain_hash = ::eosio::string_to_uint64_t(fio_domain.c_str());

   //these are the results for the table searches for domain ansd fio name
   get_table_rows_result domain_result;
   get_table_rows_result fioname_result;
   //this is the table rows result to use after domain/fioname specific processing
   get_table_rows_result name_result;
   //this is the result returned to the user
   fio_name_lookup_result result;

   //process the domain first, see if it exists aand then check the expiration.
   //we return an empty result if the domain is expired.

   //get the domain, check if the domain is expired.
   get_table_rows_params table_row_params = get_table_rows_params{.json=true,
           .code=code,
           .scope=fio_system_scope,
           .table=fio_domains_table,
           .lower_bound=boost::lexical_cast<string>(domain_hash),
           .upper_bound=boost::lexical_cast<string>(domain_hash + 1),
           .encode_type="dec"};

   domain_result = get_table_rows_ex<key_value_index>(table_row_params, abi);

   // If no matchs, then domain not found, return empty result
   if (domain_result.rows.empty()) {
      return result;
   }

   uint32_t domain_expiration = (uint32_t)(domain_result.rows[0]["expiration"].as_uint64());
   //This is not the local computer time, it is in fact the block time.
   uint32_t present_time = (uint32_t)time(0);

   //if the domain is expired then return an empty result.
   if (present_time > domain_expiration) {
     return result;
   }

   //set name result to be the domain results.
   name_result = domain_result;

    if (!fio_user_name.empty()){
        //query the names table and check if the name is expired
        get_table_rows_params name_table_row_params = get_table_rows_params{.json=true,
                .code=code,
                .scope=fio_system_scope,
                .table=fio_address_table,
                .lower_bound=boost::lexical_cast<string>(name_hash),
                .upper_bound=boost::lexical_cast<string>(name_hash + 1),
                .encode_type="dec"};

        fioname_result = get_table_rows_ex<key_value_index>(name_table_row_params, abi);


        // If no matchs, the name does not exist, return empty result
        if (fioname_result.rows.empty()) {
            return result;
        }

        uint32_t name_expiration = (uint32_t)fioname_result.rows[0]["expiration"].as_uint64();

        //if the name is expired then return an empty result.
        if (present_time > domain_expiration) {
            return result;
        }

        //set the result to the name results
        name_result = fioname_result;
    }

   //if we get this far then the name is registered, so set this in the result
   result.is_registered = "true";

   // If we found match and fio_name was a domain, indicate "domain found" in result
   if (fio_user_name.empty()) {
      result.is_domain="true";
      result.expiration = name_result.rows[0]["expiration"].as_string();
      return result;
   }

    // chain support check
    string my_chain=p.chain;
    transform(my_chain.begin(), my_chain.end(), my_chain.begin(), ::toupper);
    chain_type c_type= str_to_chain_type(my_chain);

   // validate keys vector size is expected size.
   EOS_ASSERT(chain_str.size() == name_result.rows[0]["addresses"].size(), chain::contract_table_query_exception,"Invalid keys container size.");

   if (c_type != chain_type::NONE) {
      // Pick out chain specific key and populate result
      result.address = name_result.rows[0]["addresses"][static_cast<int>(c_type)].as_string();
   }
   result.expiration = name_result.rows[0]["expiration"].as_string();
   return result;
} // fioname_lookup

/***
 * Checks if a FIO Address or FIO Domain is available for registration.
 * @param p
 * @return result.fio_name, result.is_registered
 */
read_only::avail_check_result read_only::avail_check( const read_only::avail_check_params& p ) const {

   avail_check_result result;

   // assert if empty fio name
   if(p.fio_name.empty()){
      FIO_400_ASSERT(!p.fio_name.empty(), "fio_name", p.fio_name, "Invalid fio_name", fioio::ErrorInvalidFioNameFormat);
      return result;
   }

   // Split the fio name and domain portions
   string fio_name = "";
   string fio_domain = "";

   int pos = p.fio_name.find('.');
   bool domainOnly = false;

   //Lower Case
   result.fio_name = boost::algorithm::to_lower_copy(p.fio_name); //WARNING: Fails for non-ASCII-7

   if (pos == string::npos) {
       fio_domain = result.fio_name;
   } else {
       fio_name = result.fio_name.substr(0, pos);
       fio_domain = result.fio_name.substr(pos + 1, string::npos);

       if(fio_name.size() < 1){
           domainOnly = true;
       }
   }

   //declare variables.
   const name code = ::eosio::string_to_name(fio_system_code.c_str());
   const abi_def abi = eosio::chain_apis::get_abi( db, code );

   const uint64_t name_hash = ::eosio::string_to_uint64_t(p.fio_name.c_str());
   const uint64_t domain_hash = ::eosio::string_to_uint64_t(fio_domain.c_str());

   get_table_rows_result fioname_result;
   get_table_rows_result name_result;
   get_table_rows_result domain_result;

   get_table_rows_params table_row_params = get_table_rows_params{.json=true,
           .code=code,
           .scope=fio_system_scope,
           .table=fio_domains_table,
           .lower_bound=boost::lexical_cast<string>(domain_hash),
           .upper_bound=boost::lexical_cast<string>(domain_hash + 1),
           .encode_type="dec"};

   domain_result = get_table_rows_ex<key_value_index>(table_row_params, abi);

    //Domain validation.
    if (fio_domain.size() >= 1 && fio_domain.size() <= 50 && !domainOnly){
        if(fio_domain.find_first_not_of("abcdefghijklmnopqrstuvwxyz01234567890-") != std::string::npos) {
            result.is_registered = "Invalid fio_name format";
            FIO_400_ASSERT(false, "fio_name", p.fio_name, "Invalid fio_name", fioio::ErrorInvalidFioNameFormat);
            return result;
        }
        else if(boost::algorithm::equals(fio_domain, "-") || fio_domain.at(0) == '-' ||
        fio_domain.at(fio_domain.size() - 1) == '-' )
        {
            result.is_registered = "Invalid fio_name format";
            FIO_400_ASSERT(false, "fio_name", p.fio_name, "Invalid fio_name", fioio::ErrorInvalidFioNameFormat);
            return result;
        }
    } else {
        result.is_registered = "Invalid fio_name";
        FIO_400_ASSERT(false, "fio_name", p.fio_name, "Invalid fio_name", fioio::ErrorInvalidFioNameFormat);
        return result;
    }

   if(!fio_name.empty()) {
      get_table_rows_params name_table_row_params = get_table_rows_params{.json=true,
              .code=code,
              .scope=fio_system_scope,
              .table=fio_address_table,
              .lower_bound=boost::lexical_cast<string>(name_hash),
              .upper_bound=boost::lexical_cast<string>(name_hash + 1),
              .encode_type="dec"};

      fioname_result = get_table_rows_ex<key_value_index>(name_table_row_params, abi);

      int totalsize = fio_name.size() + fio_domain.size();

      if( totalsize >= 100){
          result.is_registered = "Invalid fio_name format";
          FIO_400_ASSERT(false, "fio_name", p.fio_name, "Invalid fio_name", fioio::ErrorInvalidFioNameFormat);
          return result;
      }

      //Name validation.
      if (fio_name.size() >= 1 && fio_name.size() <= 50){
         if(fio_name.find_first_not_of("abcdefghijklmnopqrstuvwxyz01234567890-") != std::string::npos) {
            result.is_registered = "Invalid fio_name format";
            FIO_400_ASSERT(false, "fio_name", p.fio_name, "Invalid fio_name", fioio::ErrorInvalidFioNameFormat);
            return result;
         }
         else if(fio_name.at(fio_name.size() - 1) == '.' || fio_name.at(fio_name.size() - 1) == '-' ||
            boost::algorithm::equals(fio_name, "-") || fio_name.at(0) == '-')
         {
             result.is_registered = "Invalid fio_name format";
             FIO_400_ASSERT(false, "fio_name", p.fio_name, "Invalid fio_name", fioio::ErrorInvalidFioNameFormat);
             return result;
         }
      } else {
          result.is_registered = "Invalid fio_name";
          FIO_400_ASSERT(false, "fio_name", p.fio_name, "Invalid fio_name", fioio::ErrorInvalidFioNameFormat);
          return result;
      }

      if (fioname_result.rows.empty()) {
          return result;
      }

      uint32_t name_expiration = (uint32_t)(fioname_result.rows[0]["expiration"].as_uint64());
      //This is not the local computer time, it is in fact the block time.
      uint32_t present_time = (uint32_t) time(0);

      //if the domain is expired then return an empty result.
      if (present_time > name_expiration) {
         return result;
      }
   }

   if (domain_result.rows.empty()) {
       return result;
   }

   uint32_t domain_expiration = (uint32_t)(domain_result.rows[0]["expiration"].as_uint64());
   uint32_t present_time = (uint32_t)time(0);

   if (present_time > domain_expiration) {
       return result;
   }

   // name checked and set
   result.is_registered = true;

   return result;
}

/***
 * Lookup the fio address by public address
 * @param p
 * @return
 */
read_only::fio_key_lookup_result read_only::fio_key_lookup( const read_only::fio_key_lookup_params& p )const {
   // assert if empty chain key
   EOS_ASSERT( !p.key.empty(), chain::contract_table_query_exception,"Empty key string");

   EOS_ASSERT( !p.chain.empty(), chain::contract_table_query_exception,"Empty chain string");

   // chain support check
   string my_chain = p.chain;
   transform(my_chain.begin(), my_chain.end(), my_chain.begin(), ::toupper);
   chain_type c_type = str_to_chain_type(my_chain);
   EOS_ASSERT(c_type != chain_type::NONE, chain::contract_table_query_exception,"Supplied chain isn't supported.");

   const name code = ::eosio::string_to_name(fio_system_code.c_str());
   const abi_def abi = eosio::chain_apis::get_abi( db, code );

   string fio_key_lookup_table = "keynames";   // table name
   const uint64_t key_hash = ::eosio::string_to_uint64_t(p.key.c_str()); // hash of block key

   dlog( "Lookup using key hash: '${key_hash}'", ("key_hash", key_hash) );
   get_table_rows_params table_row_params = get_table_rows_params{
           .json        = true,
           .code        = code,
           .scope       = fio_system_scope,
           .table       = fio_key_lookup_table,
           .lower_bound = boost::lexical_cast<string>(key_hash),
           .upper_bound = boost::lexical_cast<string>(key_hash + 1),
           .key_type       = "i64",
           .index_position ="2"};
   // Do secondary key lookup
   get_table_rows_result table_rows_result = get_table_rows_by_seckey<index64_index, uint64_t>(table_row_params, abi, [](uint64_t v)->uint64_t {
       return v;
   });
   dlog( "Lookup row count: '${size}'", ("size", table_rows_result.rows.size()) );
   EOS_ASSERT(!table_rows_result.rows.empty(),chain::contract_table_query_exception,"No matches found.");

   // iterate through results for key and chain match
   size_t pos=0;
   for (; pos < table_rows_result.rows.size(); pos++) {
       uint64_t a = table_rows_result.rows[pos]["chaintype"].as_int64();
       uint64_t  b = static_cast<int64_t >(c_type);
      if (table_rows_result.rows[pos]["chaintype"].as_int64() == static_cast<int64_t >(c_type)) {
         break;
      }
   }

   EOS_ASSERT(table_rows_result.rows[pos]["chaintype"].as_int64() == static_cast<int64_t >(c_type), chain::contract_table_query_exception,"key not found.");

   dlog( "Lookup matched row: '${pos}'.", ("pos", pos) );

   fio_key_lookup_result result;
   result.name = table_rows_result.rows[pos]["name"].as_string();
   result.expiration = table_rows_result.rows[pos]["expiration"].as_string();
   return result;
} // fio_key_lookup


/*****************End of FIO API******************************/
/*************************************************************/

read_only::get_table_rows_result read_only::get_table_rows( const read_only::get_table_rows_params& p )const {
   const abi_def abi = eosio::chain_apis::get_abi( db, p.code );

   bool primary = false;
   auto table_with_index = get_table_index_name( p, primary );
   if( primary ) {
      EOS_ASSERT( p.table == table_with_index, chain::contract_table_query_exception, "Invalid table name ${t}", ( "t", p.table ));
      auto table_type = get_table_type( abi, p.table );
      if( table_type == KEYi64 || p.key_type == "i64" || p.key_type == "name" ) {
         return get_table_rows_ex<key_value_index>(p,abi);
      }
      EOS_ASSERT( false, chain::contract_table_query_exception,  "Invalid table type ${type}", ("type",table_type)("abi",abi));
   } else {
      EOS_ASSERT( !p.key_type.empty(), chain::contract_table_query_exception, "key type required for non-primary index" );

      if (p.key_type == chain_apis::i64 || p.key_type == "name") {
         return get_table_rows_by_seckey<index64_index, uint64_t>(p, abi, [](uint64_t v)->uint64_t {
            return v;
         });
      }
      else if (p.key_type == chain_apis::i128) {
         return get_table_rows_by_seckey<index128_index, uint128_t>(p, abi, [](uint128_t v)->uint128_t {
            return v;
         });
      }
      else if (p.key_type == chain_apis::i256) {
         if ( p.encode_type == chain_apis::hex) {
            using  conv = keytype_converter<chain_apis::sha256,chain_apis::hex>;
            return get_table_rows_by_seckey<conv::index_type, conv::input_type>(p, abi, conv::function());
         }
         using  conv = keytype_converter<chain_apis::i256>;
         return get_table_rows_by_seckey<conv::index_type, conv::input_type>(p, abi, conv::function());
      }
      else if (p.key_type == chain_apis::float64) {
         return get_table_rows_by_seckey<index_double_index, double>(p, abi, [](double v)->float64_t {
            float64_t f = *(float64_t *)&v;
            return f;
         });
      }
      else if (p.key_type == chain_apis::float128) {
         return get_table_rows_by_seckey<index_long_double_index, double>(p, abi, [](double v)->float128_t{
            float64_t f = *(float64_t *)&v;
            float128_t f128;
            f64_to_f128M(f, &f128);
            return f128;
         });
      }
      else if (p.key_type == chain_apis::sha256) {
         using  conv = keytype_converter<chain_apis::sha256,chain_apis::hex>;
         return get_table_rows_by_seckey<conv::index_type, conv::input_type>(p, abi, conv::function());
      }
      else if(p.key_type == chain_apis::ripemd160) {
         using  conv = keytype_converter<chain_apis::ripemd160,chain_apis::hex>;
         return get_table_rows_by_seckey<conv::index_type, conv::input_type>(p, abi, conv::function());
      }
      EOS_ASSERT(false, chain::contract_table_query_exception,  "Unsupported secondary index type: ${t}", ("t", p.key_type));
   }
} //get_table_rows

read_only::get_table_by_scope_result read_only::get_table_by_scope( const read_only::get_table_by_scope_params& p )const {
   const auto& d = db.db();
   const auto& idx = d.get_index<chain::table_id_multi_index, chain::by_code_scope_table>();
   decltype(idx.lower_bound(boost::make_tuple(0, 0, 0))) lower;
   decltype(idx.upper_bound(boost::make_tuple(0, 0, 0))) upper;

   if (p.lower_bound.size()) {
      uint64_t scope = convert_to_type<uint64_t>(p.lower_bound, "lower_bound scope");
      lower = idx.lower_bound( boost::make_tuple(p.code, scope, p.table));
   } else {
      lower = idx.lower_bound(boost::make_tuple(p.code, 0, p.table));
   }
   if (p.upper_bound.size()) {
      uint64_t scope = convert_to_type<uint64_t>(p.upper_bound, "upper_bound scope");
      upper = idx.lower_bound( boost::make_tuple(p.code, scope, 0));
   } else {
      upper = idx.lower_bound(boost::make_tuple((uint64_t)p.code + 1, 0, 0));
   }

   auto end = fc::time_point::now() + fc::microseconds(1000 * 10); /// 10ms max time
   unsigned int count = 0;
   auto itr = lower;
   read_only::get_table_by_scope_result result;
   for (; itr != upper; ++itr) {
      if (p.table && itr->table != p.table) {
         if (fc::time_point::now() > end) {
            break;
         }
         continue;
      }
      result.rows.push_back({itr->code, itr->scope, itr->table, itr->payer, itr->count});
      if (++count == p.limit || fc::time_point::now() > end) {
         ++itr;
         break;
      }
   }
   if (itr != upper) {
      result.more = (string)itr->scope;
   }
   return result;
}

vector<asset> read_only::get_currency_balance( const read_only::get_currency_balance_params& p )const {

   const abi_def abi = eosio::chain_apis::get_abi( db, p.code );
   auto table_type = get_table_type( abi, "accounts" );

   vector<asset> results;
   walk_key_value_table(p.code, p.account, N(accounts), [&](const key_value_object& obj){
      EOS_ASSERT( obj.value.size() >= sizeof(asset), chain::asset_type_exception, "Invalid data on table");

      asset cursor;
      fc::datastream<const char *> ds(obj.value.data(), obj.value.size());
      fc::raw::unpack(ds, cursor);

      EOS_ASSERT( cursor.get_symbol().valid(), chain::asset_type_exception, "Invalid asset");

      if( !p.symbol || boost::iequals(cursor.symbol_name(), *p.symbol) ) {
        results.emplace_back(cursor);
      }

      // return false if we are looking for one and found it, true otherwise
      return !(p.symbol && boost::iequals(cursor.symbol_name(), *p.symbol));
   });

   return results;
} //get_currency_balance

fc::variant read_only::get_currency_stats( const read_only::get_currency_stats_params& p )const {
   fc::mutable_variant_object results;

   const abi_def abi = eosio::chain_apis::get_abi( db, p.code );
   auto table_type = get_table_type( abi, "stat" );

   uint64_t scope = ( eosio::chain::string_to_symbol( 0, boost::algorithm::to_upper_copy(p.symbol).c_str() ) >> 8 );

   walk_key_value_table(p.code, scope, N(stat), [&](const key_value_object& obj){
      EOS_ASSERT( obj.value.size() >= sizeof(read_only::get_currency_stats_result), chain::asset_type_exception, "Invalid data on table");

      fc::datastream<const char *> ds(obj.value.data(), obj.value.size());
      read_only::get_currency_stats_result result;

      fc::raw::unpack(ds, result.supply);
      fc::raw::unpack(ds, result.max_supply);
      fc::raw::unpack(ds, result.issuer);

      results[result.supply.symbol_name()] = result;
      return true;
   });

   return results;
} //get_currency_stats

// TODO: move this and similar functions to a header. Copied from wasm_interface.cpp.
// TODO: fix strict aliasing violation
static float64_t to_softfloat64( double d ) {
   return *reinterpret_cast<float64_t*>(&d);
}

fc::variant get_global_row( const database& db, const abi_def& abi, const abi_serializer& abis, const fc::microseconds& abi_serializer_max_time_ms, bool shorten_abi_errors ) {
   const auto table_type = get_table_type(abi, N(global));
   EOS_ASSERT(table_type == read_only::KEYi64, chain::contract_table_query_exception, "Invalid table type ${type} for table global", ("type",table_type));

   const auto* const table_id = db.find<chain::table_id_object, chain::by_code_scope_table>(boost::make_tuple(config::system_account_name, config::system_account_name, N(global)));
   EOS_ASSERT(table_id, chain::contract_table_query_exception, "Missing table global");

   const auto& kv_index = db.get_index<key_value_index, by_scope_primary>();
   const auto it = kv_index.find(boost::make_tuple(table_id->id, N(global)));
   EOS_ASSERT(it != kv_index.end(), chain::contract_table_query_exception, "Missing row in table global");

   vector<char> data;
   read_only::copy_inline_row(*it, data);
   return abis.binary_to_variant(abis.get_table_type(N(global)), data, abi_serializer_max_time_ms, shorten_abi_errors );
}

read_only::get_producers_result read_only::get_producers( const read_only::get_producers_params& p ) const {
   const abi_def abi = eosio::chain_apis::get_abi(db, config::system_account_name);
   const auto table_type = get_table_type(abi, N(producers));
   const abi_serializer abis{ abi, abi_serializer_max_time };
   EOS_ASSERT(table_type == KEYi64, chain::contract_table_query_exception, "Invalid table type ${type} for table producers", ("type",table_type));

   const auto& d = db.db();
   const auto lower = name{p.lower_bound};

   static const uint8_t secondary_index_num = 0;
   const auto* const table_id = d.find<chain::table_id_object, chain::by_code_scope_table>(
           boost::make_tuple(config::system_account_name, config::system_account_name, N(producers)));
   const auto* const secondary_table_id = d.find<chain::table_id_object, chain::by_code_scope_table>(
           boost::make_tuple(config::system_account_name, config::system_account_name, N(producers) | secondary_index_num));
   EOS_ASSERT(table_id && secondary_table_id, chain::contract_table_query_exception, "Missing producers table");

   const auto& kv_index = d.get_index<key_value_index, by_scope_primary>();
   const auto& secondary_index = d.get_index<index_double_index>().indices();
   const auto& secondary_index_by_primary = secondary_index.get<by_primary>();
   const auto& secondary_index_by_secondary = secondary_index.get<by_secondary>();

   read_only::get_producers_result result;
   const auto stopTime = fc::time_point::now() + fc::microseconds(1000 * 10); // 10ms
   vector<char> data;

   auto it = [&]{
      if(lower.value == 0)
         return secondary_index_by_secondary.lower_bound(
            boost::make_tuple(secondary_table_id->id, to_softfloat64(std::numeric_limits<double>::lowest()), 0));
      else
         return secondary_index.project<by_secondary>(
            secondary_index_by_primary.lower_bound(
               boost::make_tuple(secondary_table_id->id, lower.value)));
   }();

   for( ; it != secondary_index_by_secondary.end() && it->t_id == secondary_table_id->id; ++it ) {
      if (result.rows.size() >= p.limit || fc::time_point::now() > stopTime) {
         result.more = name{it->primary_key}.to_string();
         break;
      }
      copy_inline_row(*kv_index.find(boost::make_tuple(table_id->id, it->primary_key)), data);
      if (p.json)
         result.rows.emplace_back( abis.binary_to_variant( abis.get_table_type(N(producers)), data, abi_serializer_max_time, shorten_abi_errors ) );
      else
         result.rows.emplace_back(fc::variant(data));
   }

   result.total_producer_vote_weight = get_global_row(d, abi, abis, abi_serializer_max_time, shorten_abi_errors)["total_producer_vote_weight"].as_double();
   return result;
}

read_only::get_producer_schedule_result read_only::get_producer_schedule( const read_only::get_producer_schedule_params& p ) const {
   read_only::get_producer_schedule_result result;
   to_variant(db.active_producers(), result.active);
   if(!db.pending_producers().producers.empty())
      to_variant(db.pending_producers(), result.pending);
   auto proposed = db.proposed_producers();
   if(proposed && !proposed->producers.empty())
      to_variant(*proposed, result.proposed);
   return result;
}

template<typename Api>
struct resolver_factory {
   static auto make(const Api* api, const fc::microseconds& max_serialization_time) {
      return [api, max_serialization_time](const account_name &name) -> optional<abi_serializer> {
         const auto* accnt = api->db.db().template find<account_object, by_name>(name);
         if (accnt != nullptr) {
            abi_def abi;
            if (abi_serializer::to_abi(accnt->abi, abi)) {
               return abi_serializer(abi, max_serialization_time);
            }
         }

         return optional<abi_serializer>();
      };
   }
};

template<typename Api>
auto make_resolver(const Api* api, const fc::microseconds& max_serialization_time) {
   return resolver_factory<Api>::make(api, max_serialization_time);
}

read_only::get_scheduled_transactions_result
read_only::get_scheduled_transactions( const read_only::get_scheduled_transactions_params& p ) const {
   const auto& d = db.db();

   const auto& idx_by_delay = d.get_index<generated_transaction_multi_index,by_delay>();
   auto itr = ([&](){
      if (!p.lower_bound.empty()) {
         try {
            auto when = time_point::from_iso_string( p.lower_bound );
            return idx_by_delay.lower_bound(boost::make_tuple(when));
         } catch (...) {
            try {
               auto txid = transaction_id_type(p.lower_bound);
               const auto& by_txid = d.get_index<generated_transaction_multi_index,by_trx_id>();
               auto itr = by_txid.find( txid );
               if (itr == by_txid.end()) {
                  EOS_THROW(transaction_exception, "Unknown Transaction ID: ${txid}", ("txid", txid));
               }

               return d.get_index<generated_transaction_multi_index>().indices().project<by_delay>(itr);

            } catch (...) {
               return idx_by_delay.end();
            }
         }
      } else {
         return idx_by_delay.begin();
      }
   })();

   read_only::get_scheduled_transactions_result result;

   auto resolver = make_resolver(this, abi_serializer_max_time);

   uint32_t remaining = p.limit;
   auto time_limit = fc::time_point::now() + fc::microseconds(1000 * 10); /// 10ms max time
   while (itr != idx_by_delay.end() && remaining > 0 && time_limit > fc::time_point::now()) {
      auto row = fc::mutable_variant_object()
              ("trx_id", itr->trx_id)
              ("sender", itr->sender)
              ("sender_id", itr->sender_id)
              ("payer", itr->payer)
              ("delay_until", itr->delay_until)
              ("expiration", itr->expiration)
              ("published", itr->published)
      ;

      if (p.json) {
         fc::variant pretty_transaction;

         transaction trx;
         fc::datastream<const char*> ds( itr->packed_trx.data(), itr->packed_trx.size() );
         fc::raw::unpack(ds,trx);

         abi_serializer::to_variant(trx, pretty_transaction, resolver, abi_serializer_max_time);
         row("transaction", pretty_transaction);
      } else {
         auto packed_transaction = bytes(itr->packed_trx.begin(), itr->packed_trx.end());
         row("transaction", packed_transaction);
      }

      result.transactions.emplace_back(std::move(row));
      ++itr;
      remaining--;
   }

   if (itr != idx_by_delay.end()) {
      result.more = string(itr->trx_id);
   }

   return result;
}

fc::variant read_only::get_block(const read_only::get_block_params& params) const {
   signed_block_ptr block;
   EOS_ASSERT(!params.block_num_or_id.empty() && params.block_num_or_id.size() <= 64, chain::block_id_type_exception, "Invalid Block number or ID, must be greater than 0 and less than 64 characters" );
   try {
      block = db.fetch_block_by_id(fc::variant(params.block_num_or_id).as<block_id_type>());
      if (!block) {
         block = db.fetch_block_by_number(fc::to_uint64(params.block_num_or_id));
      }

   } EOS_RETHROW_EXCEPTIONS(chain::block_id_type_exception, "Invalid block ID: ${block_num_or_id}", ("block_num_or_id", params.block_num_or_id))

   EOS_ASSERT( block, unknown_block_exception, "Could not find block: ${block}", ("block", params.block_num_or_id));

   fc::variant pretty_output;
   abi_serializer::to_variant(*block, pretty_output, make_resolver(this, abi_serializer_max_time), abi_serializer_max_time);

   uint32_t ref_block_prefix = block->id()._hash[1];

   return fc::mutable_variant_object(pretty_output.get_object())
           ("id", block->id())
           ("block_num",block->block_num())
           ("ref_block_prefix", ref_block_prefix);
}

fc::variant read_only::get_block_header_state(const get_block_header_state_params& params) const {
   block_state_ptr b;
   optional<uint64_t> block_num;
   std::exception_ptr e;
   try {
      block_num = fc::to_uint64(params.block_num_or_id);
   } catch( ... ) {}

   if( block_num.valid() ) {
      b = db.fetch_block_state_by_number(*block_num);
   } else {
      try {
         b = db.fetch_block_state_by_id(fc::variant(params.block_num_or_id).as<block_id_type>());
      } EOS_RETHROW_EXCEPTIONS(chain::block_id_type_exception, "Invalid block ID: ${block_num_or_id}", ("block_num_or_id", params.block_num_or_id))
   }

   EOS_ASSERT( b, unknown_block_exception, "Could not find reversible block: ${block}", ("block", params.block_num_or_id));

   fc::variant vo;
   fc::to_variant( static_cast<const block_header_state&>(*b), vo );
   return vo;
}

void read_write::push_block(const read_write::push_block_params& params, next_function<read_write::push_block_results> next) {
   try {
      app().get_method<incoming::methods::block_sync>()(std::make_shared<signed_block>(params));
      next(read_write::push_block_results{});
   } catch ( boost::interprocess::bad_alloc& ) {
      chain_plugin::handle_db_exhaustion();
   } CATCH_AND_CALL(next);
}

void read_write::push_transaction(const read_write::push_transaction_params& params, next_function<read_write::push_transaction_results> next) {

   try {
      auto pretty_input = std::make_shared<packed_transaction>();
      auto resolver = make_resolver(this, abi_serializer_max_time);
      try {
         abi_serializer::from_variant(params, *pretty_input, resolver, abi_serializer_max_time);
      } EOS_RETHROW_EXCEPTIONS(chain::packed_transaction_type_exception, "Invalid packed transaction")

      app().get_method<incoming::methods::transaction_async>()(pretty_input, true, [this, next](const fc::static_variant<fc::exception_ptr, transaction_trace_ptr>& result) -> void{
         if (result.contains<fc::exception_ptr>()) {
            next(result.get<fc::exception_ptr>());
         } else {
            auto trx_trace_ptr = result.get<transaction_trace_ptr>();

            try {
               chain::transaction_id_type id = trx_trace_ptr->id;
               fc::variant output;
               try {
                  output = db.to_variant_with_abi( *trx_trace_ptr, abi_serializer_max_time );
               } catch( chain::abi_exception& ) {
                  output = *trx_trace_ptr;
               }

               next(read_write::push_transaction_results{id, output});
            } CATCH_AND_CALL(next);
         }
      });


   } catch ( boost::interprocess::bad_alloc& ) {
      chain_plugin::handle_db_exhaustion();
   } CATCH_AND_CALL(next);
}

//static void push_next_transaction(const std::shared_ptr<std::vector<signed_transaction>>& trxs, size_t index, const std::function<void(const fc::exception_ptr&)>& next ) {
static void push_next_transaction(const std::shared_ptr<std::vector<signed_transaction>>& trxs, size_t index, const std::function<void(const fc::exception_ptr&)>& next ) {

    chain_plugin& cp = app().get_plugin<chain_plugin>();
    cp.accept_transaction( packed_transaction(trxs->at(index)), [=](const fc::static_variant<fc::exception_ptr, transaction_trace_ptr>& result){
        if (result.contains<fc::exception_ptr>()) {
            next(result.get<fc::exception_ptr>());
        } else {
            if (index + 1 < trxs->size()) {
                push_next_transaction(trxs, index + 1, next);
            }
        }
    });
}

void push_transactions( std::vector<signed_transaction>&& trxs, const std::function<void(fc::exception_ptr)>& next ) {
    auto trxs_copy = std::make_shared<std::decay_t<decltype(trxs)>>(std::move(trxs));
    push_next_transaction(trxs_copy, 0, next);
}

void create_account(std::string&  new_account, std::string& new_account_pub_key, const std::string& init_name, const std::string& init_priv_key, const std::function<void(const fc::exception_ptr&)>& next) {
    std::vector<signed_transaction> trxs;
    trxs.reserve(1);

    try {
        name newaccountA(new_account);
        name creator(init_name);

        controller& cc = app().get_plugin<chain_plugin>().chain();
        auto chainid = app().get_plugin<chain_plugin>().get_chain_id();
        auto abi_serializer_max_time = app().get_plugin<chain_plugin>().get_abi_serializer_max_time();

        abi_serializer eosio_system_serializer{fc::json::from_string(eosio_system_abi).as<abi_def>(), abi_serializer_max_time};

        fc::crypto::private_key creator_priv_key = fc::crypto::private_key(init_priv_key);

        fc::crypto::public_key  txn_text_receiver_A_pub_key;
        fc::from_variant(new_account_pub_key, txn_text_receiver_A_pub_key);

        //create some test accounts
        {
            signed_transaction trx;

            //create "A" account
            {
                auto owner_auth = eosio::chain::authority{1, {{txn_text_receiver_A_pub_key, 1}}, {}};
                auto active_auth = eosio::chain::authority{1, {{txn_text_receiver_A_pub_key, 1}}, {}};

                trx.actions.emplace_back(vector<chain::permission_level>{{creator, "active"}},
                                         newaccount{creator, newaccountA, owner_auth, active_auth});
            }
            {
                action act;
                act.account = N(eosio);
                act.name = N(buyram);
                act.authorization = vector<permission_level>{{creator,config::active_name}};
                act.data = eosio_system_serializer.variant_to_binary("buyram", fc::json::from_string("{\"payer\":\""+init_name+"\",\"receiver\":\""+new_account+"\",\"quant\":\"10000.0000 FIO\"}}"), abi_serializer_max_time);
                trx.actions.push_back(act);
            }
            {
                action act;
                act.account = N(eosio);
                act.name = N(delegatebw);
                act.authorization = vector<permission_level>{{creator,config::active_name}};
                act.data = eosio_system_serializer.variant_to_binary("delegatebw", fc::json::from_string("{\"from\":\""+init_name+"\",\"receiver\":\""+new_account+"\",\"quant\":\"10000.0000 FIO\",\"stake_net_quantity\":\"10000.0000 FIO\",\"stake_cpu_quantity\":\"10000.0000 FIO\",\"transfer\":\"1\"}}"), abi_serializer_max_time);
                trx.actions.push_back(act);
            }

            trx.expiration = cc.head_block_time() + fc::seconds(30);
            trx.set_reference_block(cc.head_block_id());
            trx.sign(creator_priv_key, chainid);
            trxs.emplace_back(std::move(trx));
        }

    } catch (const fc::exception& e) {
        next(e.dynamic_copy_exception());
        return;
    }

    push_transactions(std::move(trxs), next);
}


void add_pub_address(std::string&  pub_address, std::string& pub_key, const std::string& init_name, const std::string& init_priv_key, const std::function<void(const fc::exception_ptr&)>& next) {
    std::vector<signed_transaction> trxs;
    trxs.reserve(1);

    try {
        name principal(init_name);

        controller& cc = app().get_plugin<chain_plugin>().chain();
        auto chainid = app().get_plugin<chain_plugin>().get_chain_id();
        auto abi_serializer_max_time = app().get_plugin<chain_plugin>().get_abi_serializer_max_time();

        abi_serializer eosio_system_serializer{fc::json::from_string(eosio_system_abi).as<abi_def>(), abi_serializer_max_time};

        fc::crypto::private_key creator_priv_key = fc::crypto::private_key(init_priv_key);
        fc::crypto::public_key  txn_text_receiver_A_pub_key;

        //create some test accounts
        {
            signed_transaction trx;

            //create "A" account
            {
                auto owner_auth = eosio::chain::authority{1, {{txn_text_receiver_A_pub_key, 1}}, {}};
                auto active_auth = eosio::chain::authority{1, {{txn_text_receiver_A_pub_key, 1}}, {}};

                trx.actions.emplace_back(vector<chain::permission_level>{{principal, "active"}},
                                         newaccount{principal, principal, owner_auth, active_auth});
            }
            {
                action act;
                act.account = N(fio_system_code);
                act.name = N(addfiopubadd);
                act.authorization = vector<permission_level>{{principal,config::active_name}};
                act.data = eosio_system_serializer.variant_to_binary("addfiopubadd", fc::json::from_string("{\"pub_address\":\""+pub_address+"\",\"pub_key\":\""+pub_key+"\"}}"), abi_serializer_max_time);
                trx.actions.push_back(act);
            }

            trx.expiration = cc.head_block_time() + fc::seconds(30);
            trx.set_reference_block(cc.head_block_id());
            trx.sign(creator_priv_key, chainid);
            trxs.emplace_back(std::move(trx));
        }

    } catch (const fc::exception& e) {
        next(e.dynamic_copy_exception());
        return;
    }

    push_transactions(std::move(trxs), next);
}








void static gen_random(char *s, const int len) {

  static const char alphanum[] = "12345abcdefghijklmnopqrstuvwxyz";

  for (int i = 0; i < len; ++i) {
     s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
  }

  s[len] = 0;
}

static string generate_name () {
   const size_t len = 12;
   char nameArr[len];
   gen_random(nameArr, len);
   string name (nameArr);
   return name;
}

//Temporary to register_fio_name_params
#define TEMPPRIVATEKEY "5KBX1dwHME4VyuUss2sYM25D5ZTDvyYrbEz37UJqwAVAsR4tGuY"
#define CREATORACCOUNT "FIO.SYSTEM"

/***
 * Register_fio_name - Register a fio_address or fio_domain into the fionames (fioaddresses) or fiodomains tables respectively
 * @param p Accepts a variant object of from a pushed fio transaction that contains a public key in packed actions
 * @return result, result.transaction_id (chain::transaction_id_type), result.processed (fc::variant)
 */
void read_write::register_fio_name(const read_write::register_fio_name_params& params, next_function<read_write::register_fio_name_results> next) {

    string new_account_pub_key;
    try {
        auto pretty_input = std::make_shared<packed_transaction>();
        auto resolver = make_resolver(this, abi_serializer_max_time);
        try {
            abi_serializer::from_variant(params, *pretty_input, resolver, abi_serializer_max_time);
        } EOS_RETHROW_EXCEPTIONS(chain::packed_transaction_type_exception, "Invalid packed transaction")

       try {
            // Full received transaction
            const variant& v = params;
            const variant_object& vo = v.get_object();
            if( vo.contains("packed_trx") && vo["packed_trx"].is_string() && !vo["packed_trx"].as_string().empty() ) {
                auto pretty_input2 = std::make_shared<packed_transaction>();

                // Unpack the transaction
                from_variant(vo["packed_trx"], pretty_input2->packed_trx);

                //Set transaction type
                pretty_input2->set_fio_transaction(true);

                transaction trx = pretty_input2->get_transaction();
                //Unpack the fio_actions
                vector<fio_action> fio_actions = trx.fio_actions;
                //Check if it is a fio_action (as opposed to regular action)
                EOS_ASSERT(fio_actions.size() > 0, packed_transaction_type_exception, "Missing fio_action");
                //Use the fio_pub_key in the first fio_action element
                new_account_pub_key = fio_actions[0].fio_pub_key;
            }
        } EOS_RETHROW_EXCEPTIONS(chain::packed_transaction_type_exception, "Invalid packed FIO transaction")


       EOS_ASSERT(!new_account_pub_key.empty(), packed_transaction_type_exception, "Missing FIO public key.");

       // TBD: check fio_pub_key against MAS-114 table if new account needs to be created.
       bool createFioAccount = true;
       std::string new_account;
       //Create the internal FIOAccount)
       if (createFioAccount) {
          // TBD: Retrieve private key & creator account from the chain instance specific config.ini
          std::string creator = CREATORACCOUNT;
          std::string creator_private_key = TEMPPRIVATEKEY;
          size_t maxAccountCreationAttempts = 3;

          bool accountCreated = false;
          for (int count = 0; count < maxAccountCreationAttempts; count++) {
             try {
                new_account = generate_name();

                dlog("Attempting to create account ${name}.",("name",new_account));
                create_account(new_account, new_account_pub_key, creator, creator_private_key, next);
             } catch (...) {
                continue; // account creation failed, try again
             }
             dlog("New account created.");
             accountCreated = true;
             break; // account creation succeeded, break out of the loop
          }
          EOS_ASSERT(accountCreated, packed_transaction_type_exception, "Failed to create new FIO account.");


          try {
            dlog("Attempting to populate public_address and public_key references: ${public_key}.",("public_key",new_account_pub_key));


            //Public address generation (currently just base58 encoding)
            std::string pub_address = string(fc::to_base58(new_account_pub_key.c_str(),new_account_pub_key.length()));
            add_pub_address(pub_address, new_account_pub_key, creator, creator_private_key, next);
          }catch (...){
            dlog("Some bullshit happened...");
          }

       }

      //Return the transaction_id and results
        app().get_method<incoming::methods::transaction_async>()(pretty_input, true, [this, next](const fc::static_variant<fc::exception_ptr, transaction_trace_ptr>& result) -> void{
            if (result.contains<fc::exception_ptr>()) {
                next(result.get<fc::exception_ptr>());
            } else {
                auto trx_trace_ptr = result.get<transaction_trace_ptr>();

                try {
                    chain::transaction_id_type id = trx_trace_ptr->id;
                    fc::variant output;
                    try {
                        output = db.to_variant_with_abi( *trx_trace_ptr, abi_serializer_max_time );
                    } catch( chain::abi_exception& ) {
                        output = *trx_trace_ptr;
                    }

                    next(read_write::register_fio_name_results{id, output});
                } CATCH_AND_CALL(next);
            }
        });


    } catch ( boost::interprocess::bad_alloc& ) {
        chain_plugin::handle_db_exhaustion();
    } CATCH_AND_CALL(next);
}

static void push_recurse(read_write* rw, int index, const std::shared_ptr<read_write::push_transactions_params>& params, const std::shared_ptr<read_write::push_transactions_results>& results, const next_function<read_write::push_transactions_results>& next) {
   auto wrapped_next = [=](const fc::static_variant<fc::exception_ptr, read_write::push_transaction_results>& result) {
      if (result.contains<fc::exception_ptr>()) {
         const auto& e = result.get<fc::exception_ptr>();
         results->emplace_back( read_write::push_transaction_results{ transaction_id_type(), fc::mutable_variant_object( "error", e->to_detail_string() ) } );
      } else {
         const auto& r = result.get<read_write::push_transaction_results>();
         results->emplace_back( r );
      }

      int next_index = index + 1;
      if (next_index < params->size()) {
         push_recurse(rw, next_index, params, results, next );
      } else {
         next(*results);
      }
   };

   rw->push_transaction(params->at(index), wrapped_next);
}

void read_write::push_transactions(const read_write::push_transactions_params& params, next_function<read_write::push_transactions_results> next) {
   try {
      EOS_ASSERT( params.size() <= 1000, too_many_tx_at_once, "Attempt to push too many transactions at once" );
      auto params_copy = std::make_shared<read_write::push_transactions_params>(params.begin(), params.end());
      auto result = std::make_shared<read_write::push_transactions_results>();
      result->reserve(params.size());

      push_recurse(this, 0, params_copy, result, next);

   } CATCH_AND_CALL(next);
}

read_only::get_abi_results read_only::get_abi( const get_abi_params& params )const {
   get_abi_results result;
   result.account_name = params.account_name;
   const auto& d = db.db();
   const auto& accnt  = d.get<account_object,by_name>( params.account_name );

   abi_def abi;
   if( abi_serializer::to_abi(accnt.abi, abi) ) {
      result.abi = std::move(abi);
   }

   return result;
}

read_only::get_code_results read_only::get_code( const get_code_params& params )const {
   get_code_results result;
   result.account_name = params.account_name;
   const auto& d = db.db();
   const auto& accnt  = d.get<account_object,by_name>( params.account_name );

   EOS_ASSERT( params.code_as_wasm, unsupported_feature, "Returning WAST from get_code is no longer supported" );

   if( accnt.code.size() ) {
      if (params.code_as_wasm) {
         result.wasm = string(accnt.code.begin(), accnt.code.end());
      } else {
         result.wast = wasm_to_wast( (const uint8_t*)accnt.code.data(), accnt.code.size(), true );
      }
      result.code_hash = fc::sha256::hash( accnt.code.data(), accnt.code.size() );
   }

   abi_def abi;
   if( abi_serializer::to_abi(accnt.abi, abi) ) {

      result.abi = std::move(abi);
   }

   return result;
}

read_only::get_code_hash_results read_only::get_code_hash( const get_code_hash_params& params )const {
   get_code_hash_results result;
   result.account_name = params.account_name;
   const auto& d = db.db();
   const auto& accnt  = d.get<account_object,by_name>( params.account_name );

   if( accnt.code.size() ) {
      result.code_hash = fc::sha256::hash( accnt.code.data(), accnt.code.size() );
   }

   return result;
}

read_only::get_raw_code_and_abi_results read_only::get_raw_code_and_abi( const get_raw_code_and_abi_params& params)const {
   get_raw_code_and_abi_results result;
   result.account_name = params.account_name;

   const auto& d = db.db();
   const auto& accnt = d.get<account_object,by_name>(params.account_name);
   result.wasm = blob{{accnt.code.begin(), accnt.code.end()}};
   result.abi = blob{{accnt.abi.begin(), accnt.abi.end()}};

   return result;
}

read_only::get_raw_abi_results read_only::get_raw_abi( const get_raw_abi_params& params )const {
   get_raw_abi_results result;
   result.account_name = params.account_name;

   const auto& d = db.db();
   const auto& accnt = d.get<account_object,by_name>(params.account_name);
   result.abi_hash = fc::sha256::hash( accnt.abi.data(), accnt.abi.size() );
   result.code_hash = fc::sha256::hash( accnt.code.data(), accnt.code.size() );
   if( !params.abi_hash || *params.abi_hash != result.abi_hash )
      result.abi = blob{{accnt.abi.begin(), accnt.abi.end()}};

   return result;
}

read_only::get_account_results read_only::get_account( const get_account_params& params )const {
   get_account_results result;
   result.account_name = params.account_name;

   const auto& d = db.db();
   const auto& rm = db.get_resource_limits_manager();

   result.head_block_num  = db.head_block_num();
   result.head_block_time = db.head_block_time();

   rm.get_account_limits( result.account_name, result.ram_quota, result.net_weight, result.cpu_weight );

   const auto& a = db.get_account(result.account_name);

   result.privileged       = a.privileged;
   result.last_code_update = a.last_code_update;
   result.created          = a.creation_date;

   bool grelisted = db.is_resource_greylisted(result.account_name);
   result.net_limit = rm.get_account_net_limit_ex( result.account_name, !grelisted);
   result.cpu_limit = rm.get_account_cpu_limit_ex( result.account_name, !grelisted);
   result.ram_usage = rm.get_account_ram_usage( result.account_name );

   const auto& permissions = d.get_index<permission_index,by_owner>();
   auto perm = permissions.lower_bound( boost::make_tuple( params.account_name ) );
   while( perm != permissions.end() && perm->owner == params.account_name ) {
      /// TODO: lookup perm->parent name
      name parent;

      // Don't lookup parent if null
      if( perm->parent._id ) {
         const auto* p = d.find<permission_object,by_id>( perm->parent );
         if( p ) {
            EOS_ASSERT(perm->owner == p->owner, invalid_parent_permission, "Invalid parent permission");
            parent = p->name;
         }
      }

      result.permissions.push_back( permission{ perm->name, parent, perm->auth.to_authority() } );
      ++perm;
   }

   const auto& code_account = db.db().get<account_object,by_name>( config::system_account_name );

   abi_def abi;
   if( abi_serializer::to_abi(code_account.abi, abi) ) {
      abi_serializer abis( abi, abi_serializer_max_time );

      const auto token_code = N(eosio.token);

      auto core_symbol = extract_core_symbol();

      if (params.expected_core_symbol.valid())
         core_symbol = *(params.expected_core_symbol);

      const auto* t_id = d.find<chain::table_id_object, chain::by_code_scope_table>(boost::make_tuple( token_code, params.account_name, N(accounts) ));
      if( t_id != nullptr ) {
         const auto &idx = d.get_index<key_value_index, by_scope_primary>();
         auto it = idx.find(boost::make_tuple( t_id->id, core_symbol.to_symbol_code() ));
         if( it != idx.end() && it->value.size() >= sizeof(asset) ) {
            asset bal;
            fc::datastream<const char *> ds(it->value.data(), it->value.size());
            fc::raw::unpack(ds, bal);

            if( bal.get_symbol().valid() && bal.get_symbol() == core_symbol ) {
               result.core_liquid_balance = bal;
            }
         }
      }

      t_id = d.find<chain::table_id_object, chain::by_code_scope_table>(boost::make_tuple( config::system_account_name, params.account_name, N(userres) ));
      if (t_id != nullptr) {
         const auto &idx = d.get_index<key_value_index, by_scope_primary>();
         auto it = idx.find(boost::make_tuple( t_id->id, params.account_name ));
         if ( it != idx.end() ) {
            vector<char> data;
            copy_inline_row(*it, data);
            result.total_resources = abis.binary_to_variant( "user_resources", data, abi_serializer_max_time, shorten_abi_errors );
         }
      }

      t_id = d.find<chain::table_id_object, chain::by_code_scope_table>(boost::make_tuple( config::system_account_name, params.account_name, N(delband) ));
      if (t_id != nullptr) {
         const auto &idx = d.get_index<key_value_index, by_scope_primary>();
         auto it = idx.find(boost::make_tuple( t_id->id, params.account_name ));
         if ( it != idx.end() ) {
            vector<char> data;
            copy_inline_row(*it, data);
            result.self_delegated_bandwidth = abis.binary_to_variant( "delegated_bandwidth", data, abi_serializer_max_time, shorten_abi_errors );
         }
      }

      t_id = d.find<chain::table_id_object, chain::by_code_scope_table>(boost::make_tuple( config::system_account_name, params.account_name, N(refunds) ));
      if (t_id != nullptr) {
         const auto &idx = d.get_index<key_value_index, by_scope_primary>();
         auto it = idx.find(boost::make_tuple( t_id->id, params.account_name ));
         if ( it != idx.end() ) {
            vector<char> data;
            copy_inline_row(*it, data);
            result.refund_request = abis.binary_to_variant( "refund_request", data, abi_serializer_max_time, shorten_abi_errors );
         }
      }

      t_id = d.find<chain::table_id_object, chain::by_code_scope_table>(boost::make_tuple( config::system_account_name, config::system_account_name, N(voters) ));
      if (t_id != nullptr) {
         const auto &idx = d.get_index<key_value_index, by_scope_primary>();
         auto it = idx.find(boost::make_tuple( t_id->id, params.account_name ));
         if ( it != idx.end() ) {
            vector<char> data;
            copy_inline_row(*it, data);
            result.voter_info = abis.binary_to_variant( "voter_info", data, abi_serializer_max_time, shorten_abi_errors );
         }
      }
   }
   return result;
}

static variant action_abi_to_variant( const abi_def& abi, type_name action_type ) {
   variant v;
   auto it = std::find_if(abi.structs.begin(), abi.structs.end(), [&](auto& x){return x.name == action_type;});
   if( it != abi.structs.end() )
      to_variant( it->fields,  v );
   return v;
};

read_only::abi_json_to_bin_result read_only::abi_json_to_bin( const read_only::abi_json_to_bin_params& params )const try {
   abi_json_to_bin_result result;
   const auto code_account = db.db().find<account_object,by_name>( params.code );
   EOS_ASSERT(code_account != nullptr, contract_query_exception, "Contract can't be found ${contract}", ("contract", params.code));

   abi_def abi;
   if( abi_serializer::to_abi(code_account->abi, abi) ) {
      abi_serializer abis( abi, abi_serializer_max_time );
      auto action_type = abis.get_action_type(params.action);
      EOS_ASSERT(!action_type.empty(), action_validate_exception, "Unknown action ${action} in contract ${contract}", ("action", params.action)("contract", params.code));
      try {
         result.binargs = abis.variant_to_binary( action_type, params.args, abi_serializer_max_time, shorten_abi_errors );
      } EOS_RETHROW_EXCEPTIONS(chain::invalid_action_args_exception,
                                "'${args}' is invalid args for action '${action}' code '${code}'. expected '${proto}'",
                                ("args", params.args)("action", params.action)("code", params.code)("proto", action_abi_to_variant(abi, action_type)))
   } else {
      EOS_ASSERT(false, abi_not_found_exception, "No ABI found for ${contract}", ("contract", params.code));
   }
   return result;
} FC_RETHROW_EXCEPTIONS( warn, "code: ${code}, action: ${action}, args: ${args}",
                         ("code", params.code)( "action", params.action )( "args", params.args ))

read_only::abi_bin_to_json_result read_only::abi_bin_to_json( const read_only::abi_bin_to_json_params& params )const {
   abi_bin_to_json_result result;
   const auto& code_account = db.db().get<account_object,by_name>( params.code );
   abi_def abi;
   if( abi_serializer::to_abi(code_account.abi, abi) ) {
      abi_serializer abis( abi, abi_serializer_max_time );
      result.args = abis.binary_to_variant( abis.get_action_type( params.action ), params.binargs, abi_serializer_max_time, shorten_abi_errors );
   } else {
      EOS_ASSERT(false, abi_not_found_exception, "No ABI found for ${contract}", ("contract", params.code));
   }
   return result;
}

read_only::serialize_json_result read_only::serialize_json( const read_only::serialize_json_params& params )const try {
   serialize_json_result result;

   name code = 0;
   if (params.action.to_string() == "registername" || params.action.to_string() == "addaddress")
	   	code = ::eosio::string_to_name("fio.system");
   else if (params.action.to_string() == "requestfunds")
	  	code = ::eosio::string_to_name("fio.finance");
   else
		code = ::eosio::string_to_name("eosio");

   const auto code_account = db.db().find<account_object,by_name>( code );
   EOS_ASSERT(code_account != nullptr, contract_query_exception, "Contract can't be found ${contract}", ("contract", code));

   abi_def abi;
   if( abi_serializer::to_abi(code_account->abi, abi) ) {
      abi_serializer abis( abi, abi_serializer_max_time );
      auto action_type = abis.get_action_type(params.action);
      EOS_ASSERT(!action_type.empty(), action_validate_exception, "Unknown action ${action} in contract ${contract}", ("action", params.action)("contract", code));
      try {
         result.serialized_json = abis.variant_to_binary( action_type, params.json, abi_serializer_max_time, shorten_abi_errors );
      } EOS_RETHROW_EXCEPTIONS(chain::invalid_action_args_exception,
                                "'${args}' is invalid args for action '${action}' code '${code}'. expected '${proto}'",
                                ("args", params.json)("action", params.action)("code", code)("proto", action_abi_to_variant(abi, action_type)))
   } else {
      EOS_ASSERT(false, abi_not_found_exception, "No ABI found for ${contract}", ("contract", code));
   }
   return result;
} FC_RETHROW_EXCEPTIONS( warn, "action: ${action}, args: ${args}",
                         ( "action", params.action )( "args", params.json ))

read_only::get_required_keys_result read_only::get_required_keys( const get_required_keys_params& params )const {
   transaction pretty_input;
   auto resolver = make_resolver(this, abi_serializer_max_time);
   try {
      abi_serializer::from_variant(params.transaction, pretty_input, resolver, abi_serializer_max_time);
   } EOS_RETHROW_EXCEPTIONS(chain::transaction_type_exception, "Invalid transaction")

   auto required_keys_set = db.get_authorization_manager().get_required_keys( pretty_input, params.available_keys, fc::seconds( pretty_input.delay_sec ));
   get_required_keys_result result;
   result.required_keys = required_keys_set;
   return result;
}

read_only::get_transaction_id_result read_only::get_transaction_id( const read_only::get_transaction_id_params& params)const {
   return params.id();
}

namespace detail {
   struct ram_market_exchange_state_t {
      asset  ignore1;
      asset  ignore2;
      double ignore3;
      asset  core_symbol;
      double ignore4;
   };
}

chain::symbol read_only::extract_core_symbol()const {
   symbol core_symbol(0);

   // The following code makes assumptions about the contract deployed on eosio account (i.e. the system contract) and how it stores its data.
   const auto& d = db.db();
   const auto* t_id = d.find<chain::table_id_object, chain::by_code_scope_table>(boost::make_tuple( N(eosio), N(eosio), N(rammarket) ));
   if( t_id != nullptr ) {
      const auto &idx = d.get_index<key_value_index, by_scope_primary>();
      auto it = idx.find(boost::make_tuple( t_id->id, eosio::chain::string_to_symbol_c(4,"RAMCORE") ));
      if( it != idx.end() ) {
         detail::ram_market_exchange_state_t ram_market_exchange_state;

         fc::datastream<const char *> ds( it->value.data(), it->value.size() );

         try {
            fc::raw::unpack(ds, ram_market_exchange_state);
         } catch( ... ) {
            return core_symbol;
         }

         if( ram_market_exchange_state.core_symbol.get_symbol().valid() ) {
            core_symbol = ram_market_exchange_state.core_symbol.get_symbol();
         }
      }
   }

   return core_symbol;
}

} // namespace chain_apis
} // namespace eosio

FC_REFLECT( eosio::chain_apis::detail::ram_market_exchange_state_t, (ignore1)(ignore2)(ignore3)(core_symbol)(ignore4) )
