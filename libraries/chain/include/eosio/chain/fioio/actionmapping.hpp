/** FioError definitions file
 *  Description: Mapping of actions to account names for use in processing FIO transactions.
 *  @author Casey Gardiner
 *  @file actionmapping.hpp
 *  @copyright Dapix
 *
 *  Changes:
 */

#pragma once
#include <string>
#include <fc/reflect/reflect.hpp>
namespace fioio {

    using namespace std;

    //map<string, string> ctType;
    vector<string> eosioActions;
    vector<string> fiosystemActions;
    vector<string> fioFinanceActions;

    static void Set_map(void){
        //eosio actions
        eosioActions.push_back("default");

        //fio.system actions
        fiosystemActions.push_back("registername");
        fiosystemActions.push_back("addaddress");

        //fio.finance actions
        fioFinanceActions.push_back("requestfunds");
    }

    static string map_to_contract( string t ){
        if (find(fiosystemActions.begin(), fiosystemActions.end(), t) != fiosystemActions.end()){
            return "fio.system";
        }
        if (find(fioFinanceActions.begin(), fioFinanceActions.end(), t) != fioFinanceActions.end()){
            return "fio.finance";
        }
        return "eosio";
    }

    inline string returncontract(string incomingaction) {
        Set_map();

        string contract = map_to_contract( incomingaction );

        return contract;
    }

   struct registername {
      string name;
      uint64_t requestor;
   };


}

FC_REFLECT( fioio::registername, (name)(requestor) )
